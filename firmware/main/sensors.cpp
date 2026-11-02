#include "sensors.hpp"

#include <array>
#include <cstring>
#include <new>

#include "board.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_device.h"

namespace {
constexpr char kTag[] = "sensors";
constexpr spi_host_device_t kSpiHost = SPI2_HOST;

class Max31856Channel {
public:
    bool begin(gpio_num_t cs, gpio_num_t drdy) {
        drdy_ = drdy;
        gpio_config_t input{};
        input.pin_bit_mask = 1ULL << static_cast<unsigned>(drdy);
        input.mode = GPIO_MODE_INPUT;
        input.pull_up_en = GPIO_PULLUP_DISABLE;
        input.pull_down_en = GPIO_PULLDOWN_DISABLE;
        if (gpio_config(&input) != ESP_OK) return false;

        spi_device_interface_config_t device{};
        device.mode = 3;
        device.clock_speed_hz = board::kSpiClockHz;
        device.spics_io_num = cs;
        device.queue_size = 1;
        device.cs_ena_pretrans = 1;   // one 1 MHz clock period = 1 us, above the 100 ns minimum
        device.cs_ena_posttrans = 1;
        if (spi_bus_add_device(kSpiHost, &device, &handle_) != ESP_OK) return false;

        // One-shot, OCFAULT=01, CJ enabled, 50 Hz; type K; expose all useful SR bits.
        if (!write_register(0x00, 0x15) || !write_register(0x01, 0x03) ||
            !write_register(0x02, 0x00) || !configuration_ok()) return false;
        next_start_ms_ = 1;
        return true;
    }

    void poll(uint64_t now_ms) {
        if (!handle_) {
            invalidate(0xFF);
            return;
        }
        if (!waiting_) {
            if (now_ms < next_start_ms_) return;
            if (gpio_get_level(drdy_) == 0) {
                std::array<uint8_t, 6> stale{};
                read_registers(0x0A, stale.data(), stale.size());  // clear an old DRDY without publishing it
            }
            if (!configuration_ok() || !write_register(0x00, 0x55)) {
                invalidate(0xFE);
                next_start_ms_ = now_ms + 250;
                return;
            }
            started_ms_ = now_ms;
            next_start_ms_ = now_ms + 250;
            saw_high_ = gpio_get_level(drdy_) != 0;
            waiting_ = true;
            return;
        }

        const int level = gpio_get_level(drdy_);
        if (level != 0) saw_high_ = true;
        if (level == 0 && saw_high_) {
            uint8_t before{};
            std::array<uint8_t, 6> packet{};
            if (!read_registers(0x0F, &before, 1) ||
                !read_registers(0x0A, packet.data(), packet.size())) {
                invalidate(0xFD);
            } else {
                const uint8_t after = packet[5];
                sample_.status = before | after;
                sample_.valid = sample_.status == 0;
                sample_.milli_c = sirokko::max31856_milli_c(packet[2], packet[3], packet[4]);
                sample_.sample_ms = now_ms;
            }
            waiting_ = false;
        } else if (now_ms - started_ms_ >= 210) {
            invalidate(0xFC);  // DRDY did not complete a new HIGH->LOW cycle by its deadline
            waiting_ = false;
        }
    }

    sirokko::SensorSample sample() const { return sample_; }

private:
    void invalidate(uint8_t status) {
        sample_.valid = false;
        sample_.status = status;
    }

    bool configuration_ok() {
        uint8_t config[2]{};
        return read_registers(0x00, config, sizeof(config)) &&
               (config[0] & 0xBDU) == 0x15U && (config[1] & 0x7FU) == 0x03U;
    }

    bool write_register(uint8_t address, uint8_t value) {
        uint8_t tx[2]{static_cast<uint8_t>(address | 0x80U), value};
        spi_transaction_t transaction{};
        transaction.length = sizeof(tx) * 8;
        transaction.tx_buffer = tx;
        return spi_device_transmit(handle_, &transaction) == ESP_OK;
    }

    bool read_registers(uint8_t address, uint8_t* output, size_t size) {
        if (!output || size == 0 || size > 6) return false;
        uint8_t tx[7]{};
        uint8_t rx[7]{};
        tx[0] = address & 0x7FU;
        spi_transaction_t transaction{};
        transaction.length = (size + 1) * 8;
        transaction.rxlength = transaction.length;
        transaction.tx_buffer = tx;
        transaction.rx_buffer = rx;
        if (spi_device_transmit(handle_, &transaction) != ESP_OK) return false;
        std::memcpy(output, rx + 1, size);
        return true;
    }

    spi_device_handle_t handle_{};
    gpio_num_t drdy_{GPIO_NUM_NC};
    sirokko::SensorSample sample_{};
    uint64_t started_ms_{};
    uint64_t next_start_ms_{};
    bool waiting_{};
    bool saw_high_{};
};

class RoomDs18b20 {
public:
    bool begin() {
        onewire_bus_config_t bus_config{};
        bus_config.bus_gpio_num = board::kRoomDq;
        bus_config.flags.en_pull_up = false;  // R1=4.7 kOhm is on the carrier PCB.
        onewire_bus_rmt_config_t rmt_config{};
        rmt_config.max_rx_bytes = 10;
        if (onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_) != ESP_OK) return false;

        onewire_device_iter_handle_t iterator{};
        if (onewire_new_device_iter(bus_, &iterator) != ESP_OK) return false;
        unsigned count = 0;
        esp_err_t result{};
        do {
            onewire_device_t device{};
            result = onewire_device_iter_get_next(iterator, &device);
            if (result == ESP_OK && (device.address & 0xFFU) == 0x28U) {
                address_ = device.address;
                ++count;
            }
        } while (result == ESP_OK);
        onewire_del_device_iter(iterator);
        if (result != ESP_ERR_NOT_FOUND || count != 1) return false;

        // Volatile scratchpad configuration only: 12 bit, no EEPROM write/copy.
        if (!reset_and_command(0x4E)) return false;
        const uint8_t config[3]{0x00, 0x00, 0x7F};
        if (onewire_bus_write_bytes(bus_, config, sizeof(config)) != ESP_OK) return false;
        return true;
    }

    void poll(uint64_t now_ms) {
        if (!bus_) {
            invalidate(0xFF);
            return;
        }
        if (!conversion_pending_) {
            if (now_ms < next_start_ms_) return;
            if (!reset_and_command(0x44)) {
                invalidate(0xFE);
                next_start_ms_ = now_ms + 1000;
                return;
            }
            conversion_started_ms_ = now_ms;
            conversion_pending_ = true;
            return;
        }
        if (now_ms - conversion_started_ms_ < 800) return;

        uint8_t frame[9]{};
        if (!reset_and_command(0xBE) || onewire_bus_read_bytes(bus_, frame, sizeof(frame)) != ESP_OK) {
            invalidate(0xFD);
        } else {
            int32_t milli_c{};
            if (sirokko::decode_ds18b20_scratchpad(frame, 12, true, &milli_c)) {
                sample_ = {milli_c, now_ms, 0, true};
            } else {
                invalidate(0xFC);
            }
        }
        conversion_pending_ = false;
        next_start_ms_ = now_ms + 200;
    }

    sirokko::SensorSample sample() const { return sample_; }

private:
    bool reset_and_command(uint8_t command) {
        if (onewire_bus_reset(bus_) != ESP_OK) return false;
        uint8_t bytes[10]{0x55};  // MATCH ROM, then the sole enumerated device and command.
        std::memcpy(bytes + 1, &address_, sizeof(address_));
        bytes[9] = command;
        return onewire_bus_write_bytes(bus_, bytes, sizeof(bytes)) == ESP_OK;
    }

    void invalidate(uint8_t status) {
        sample_.valid = false;
        sample_.status = status;
    }

    onewire_bus_handle_t bus_{};
    onewire_device_address_t address_{};
    sirokko::SensorSample sample_{};
    uint64_t conversion_started_ms_{};
    uint64_t next_start_ms_{};
    bool conversion_pending_{};
};
}  // namespace

struct SensorSuite::Impl {
    Max31856Channel exhaust;
    Max31856Channel air;
    RoomDs18b20 room;
};

bool SensorSuite::begin() {
    impl_ = new (std::nothrow) Impl{};
    if (!impl_) return false;

    spi_bus_config_t bus{};
    bus.mosi_io_num = board::kMosi;
    bus.miso_io_num = board::kMiso;
    bus.sclk_io_num = board::kSck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 16;
    if (spi_bus_initialize(kSpiHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    const bool exhaust = impl_->exhaust.begin(board::kExhaustCs, board::kExhaustDrdy);
    const bool air = impl_->air.begin(board::kAirCs, board::kAirDrdy);
    const bool room = impl_->room.begin();
    ESP_LOGI(kTag, "MAX31856 exhaust=%d air=%d DS18B20=%d", exhaust, air, room);
    return exhaust && air && room;
}

void SensorSuite::poll(uint64_t now_ms) {
    if (!impl_) return;
    impl_->exhaust.poll(now_ms);
    impl_->air.poll(now_ms);
    impl_->room.poll(now_ms);
}

sirokko::Sensors SensorSuite::readings() const {
    if (!impl_) return {};
    return {impl_->exhaust.sample(), impl_->air.sample(), impl_->room.sample()};
}
