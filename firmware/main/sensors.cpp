#include "sensors.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "ds18b20.h"
#include "esp_err.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "onewire_device.h"

namespace {
constexpr char kTag[] = "sensors";
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr gpio_num_t kMosi = GPIO_NUM_11;
constexpr gpio_num_t kSck = GPIO_NUM_12;
constexpr gpio_num_t kMiso = GPIO_NUM_13;
constexpr gpio_num_t kExhaustCs = GPIO_NUM_10;
constexpr gpio_num_t kAirCs = GPIO_NUM_9;
constexpr gpio_num_t kExhaustDrdy = GPIO_NUM_1;
constexpr gpio_num_t kAirDrdy = GPIO_NUM_2;
constexpr gpio_num_t kRoomDq = GPIO_NUM_4;

class Max31856 {
public:
    bool begin(gpio_num_t chip_select, gpio_num_t drdy) {
        drdy_ = drdy;
        gpio_config_t input{};
        input.pin_bit_mask = 1ULL << static_cast<unsigned>(drdy);
        input.mode = GPIO_MODE_INPUT;
        input.pull_up_en = GPIO_PULLUP_ENABLE;
        input.pull_down_en = GPIO_PULLDOWN_DISABLE;
        if (gpio_config(&input) != ESP_OK) return false;

        spi_device_interface_config_t device{};
        device.command_bits = 0;
        device.address_bits = 0;
        device.dummy_bits = 0;
        device.mode = 1;
        device.clock_speed_hz = 5 * 1000 * 1000;
        device.spics_io_num = chip_select;
        device.queue_size = 1;
        if (spi_bus_add_device(kSpiHost, &device, &handle_) != ESP_OK) return false;

        // CR0: continuous conversion, open-circuit detection, 50 Hz rejection.
        // CR1: K thermocouple. MASK=0 exposes every status fault.
        if (!write_register(0x00, 0x91) || !write_register(0x01, 0x03) ||
            !write_register(0x02, 0x00)) {
            return false;
        }
        uint8_t verify[3]{};
        if (!read_registers(0x00, verify, sizeof(verify))) return false;
        if (verify[0] != 0x91 || (verify[1] & 0x0F) != 0x03 || verify[2] != 0x00) return false;
        sample_.quality = sirokko::SensorQuality::MISSING;
        return true;
    }

    void poll(uint32_t now_ms) {
        if (!handle_) {
            sample_.quality = sirokko::SensorQuality::HARDWARE_FAULT;
            return;
        }
        if (gpio_get_level(drdy_) != 0 || static_cast<uint32_t>(now_ms - last_read_ms_) < 20) return;
        uint8_t data[4]{};
        if (!read_registers(0x0C, data, sizeof(data))) {
            sample_.quality = sirokko::SensorQuality::TIMEOUT;
            return;
        }
        last_read_ms_ = now_ms;
        raw_status_ = data[3];
        if (raw_status_ != 0) {
            sample_.quality = sirokko::SensorQuality::HARDWARE_FAULT;
            return;
        }
        int32_t raw19 = static_cast<int32_t>((static_cast<uint32_t>(data[0]) << 16) |
                                             (static_cast<uint32_t>(data[1]) << 8) | data[2]) >> 5;
        if ((raw19 & 0x40000) != 0) raw19 |= ~0x7FFFF;
        sample_.milli_c = static_cast<int32_t>((static_cast<int64_t>(raw19) * 1000) / 128);
        sample_.sample_ms = now_ms;
        sample_.quality = sirokko::SensorQuality::OK;
    }

    sirokko::SensorSample sample() const { return sample_; }
    uint8_t status() const { return raw_status_; }

private:
    bool write_register(uint8_t address, uint8_t value) {
        spi_transaction_t transaction{};
        transaction.flags = SPI_TRANS_USE_TXDATA;
        transaction.length = 16;
        transaction.tx_data[0] = address | 0x80;
        transaction.tx_data[1] = value;
        return spi_device_transmit(handle_, &transaction) == ESP_OK;
    }

    bool read_registers(uint8_t address, uint8_t* output, size_t length) {
        if (length > 4) return false;
        uint8_t tx[5]{};
        uint8_t rx[5]{};
        tx[0] = address & 0x7F;
        spi_transaction_t transaction{};
        transaction.length = (length + 1) * 8;
        transaction.rxlength = (length + 1) * 8;
        transaction.tx_buffer = tx;
        transaction.rx_buffer = rx;
        if (spi_device_transmit(handle_, &transaction) != ESP_OK) return false;
        std::memcpy(output, rx + 1, length);
        return true;
    }

    spi_device_handle_t handle_{};
    gpio_num_t drdy_{GPIO_NUM_NC};
    uint32_t last_read_ms_{};
    uint8_t raw_status_{};
    sirokko::SensorSample sample_{};
};

class RoomSensor {
public:
    bool begin() {
        onewire_bus_config_t bus_config{};
        bus_config.bus_gpio_num = kRoomDq;
        bus_config.flags.en_pull_up = false;  // The hardware contract requires an external 4.7 kOhm pull-up.
        onewire_bus_rmt_config_t rmt_config{};
        rmt_config.max_rx_bytes = 10;
        if (onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_) != ESP_OK) return false;

        onewire_device_iter_handle_t iterator{};
        if (onewire_new_device_iter(bus_, &iterator) != ESP_OK) return false;
        int ds_count = 0;
        esp_err_t result = ESP_OK;
        do {
            onewire_device_t found{};
            result = onewire_device_iter_get_next(iterator, &found);
            if (result == ESP_OK) {
                ds18b20_config_t config{};
                ds18b20_device_handle_t candidate{};
                if (ds18b20_new_device_from_enumeration(&found, &config, &candidate) == ESP_OK) {
                    ++ds_count;
                    if (ds_count == 1) device_ = candidate;
                    else ds18b20_del_device(candidate);
                }
            }
        } while (result == ESP_OK);
        onewire_del_device_iter(iterator);
        if (result != ESP_ERR_NOT_FOUND || ds_count != 1 || !device_) return false;
        if (ds18b20_set_resolution(device_, DS18B20_RESOLUTION_12B) != ESP_OK) return false;
        sample_.quality = sirokko::SensorQuality::MISSING;
        return true;
    }

    void poll(uint32_t now_ms) {
        if (!device_ || !bus_) {
            sample_.quality = sirokko::SensorQuality::MISSING;
            return;
        }
        if (!conversion_pending_) {
            const esp_err_t error = ds18b20_trigger_temperature_conversion_for_all(bus_);
            if (error != ESP_OK) {
                sample_.quality = error == ESP_ERR_TIMEOUT ? sirokko::SensorQuality::TIMEOUT
                                                          : sirokko::SensorQuality::HARDWARE_FAULT;
                return;
            }
            conversion_started_ms_ = now_ms;
            conversion_pending_ = true;
            return;
        }
        if (static_cast<uint32_t>(now_ms - conversion_started_ms_) < 800) return;

        float celsius = 0.0F;
        const esp_err_t error = ds18b20_get_temperature(device_, &celsius);
        conversion_pending_ = false;
        if (error == ESP_ERR_INVALID_CRC) {
            sample_.quality = sirokko::SensorQuality::CRC_ERROR;
        } else if (error == ESP_ERR_TIMEOUT) {
            sample_.quality = sirokko::SensorQuality::TIMEOUT;
        } else if (error != ESP_OK || !std::isfinite(celsius)) {
            sample_.quality = sirokko::SensorQuality::HARDWARE_FAULT;
        } else if (std::fabs(celsius - 85.0F) < 0.001F) {
            sample_.quality = sirokko::SensorQuality::POWER_ON_DEFAULT;
        } else {
            sample_.milli_c = static_cast<int32_t>(std::lround(celsius * 1000.0F));
            sample_.sample_ms = now_ms;
            sample_.quality = sirokko::SensorQuality::OK;
        }
    }

    sirokko::SensorSample sample() const { return sample_; }

private:
    onewire_bus_handle_t bus_{};
    ds18b20_device_handle_t device_{};
    uint32_t conversion_started_ms_{};
    bool conversion_pending_{};
    sirokko::SensorSample sample_{};
};
}  // namespace

struct SensorSuite::Impl {
    Max31856 exhaust;
    Max31856 hot_air;
    RoomSensor room;
};

bool SensorSuite::begin() {
    impl_ = new (std::nothrow) Impl{};
    if (!impl_) return false;

    spi_bus_config_t bus{};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = kMiso;
    bus.sclk_io_num = kSck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 8;
    if (spi_bus_initialize(kSpiHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(kTag, "SPI bus initialization failed");
        return false;
    }
    const bool exhaust_ok = impl_->exhaust.begin(kExhaustCs, kExhaustDrdy);
    const bool air_ok = impl_->hot_air.begin(kAirCs, kAirDrdy);
    const bool room_ok = impl_->room.begin();
    ESP_LOGI(kTag, "MAX exhaust=%d air=%d DS18B20=%d", exhaust_ok, air_ok, room_ok);
    return exhaust_ok && air_ok && room_ok;
}

void SensorSuite::poll(uint32_t now_ms) {
    if (!impl_) return;
    impl_->exhaust.poll(now_ms);
    impl_->hot_air.poll(now_ms);
    impl_->room.poll(now_ms);
}

sirokko::SensorSample SensorSuite::exhaust() const {
    return impl_ ? impl_->exhaust.sample() : sirokko::SensorSample{};
}

sirokko::SensorSample SensorSuite::hot_air() const {
    return impl_ ? impl_->hot_air.sample() : sirokko::SensorSample{};
}

sirokko::SensorSample SensorSuite::room() const {
    return impl_ ? impl_->room.sample() : sirokko::SensorSample{};
}

uint8_t SensorSuite::exhaust_status() const { return impl_ ? impl_->exhaust.status() : 0xFF; }
uint8_t SensorSuite::hot_air_status() const { return impl_ ? impl_->hot_air.status() : 0xFF; }
