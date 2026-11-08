#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NOT_FOUND = 0x105;

using gpio_num_t = int;
constexpr gpio_num_t GPIO_NUM_NC = -1;
constexpr gpio_num_t GPIO_NUM_1 = 1;
constexpr gpio_num_t GPIO_NUM_2 = 2;
constexpr gpio_num_t GPIO_NUM_4 = 4;
constexpr gpio_num_t GPIO_NUM_5 = 5;
constexpr gpio_num_t GPIO_NUM_6 = 6;
constexpr gpio_num_t GPIO_NUM_7 = 7;
constexpr gpio_num_t GPIO_NUM_8 = 8;
constexpr gpio_num_t GPIO_NUM_9 = 9;
constexpr gpio_num_t GPIO_NUM_10 = 10;
constexpr gpio_num_t GPIO_NUM_11 = 11;
constexpr gpio_num_t GPIO_NUM_12 = 12;
constexpr gpio_num_t GPIO_NUM_13 = 13;
constexpr gpio_num_t GPIO_NUM_14 = 14;
constexpr gpio_num_t GPIO_NUM_15 = 15;
constexpr gpio_num_t GPIO_NUM_16 = 16;
constexpr gpio_num_t GPIO_NUM_17 = 17;
constexpr gpio_num_t GPIO_NUM_38 = 38;
constexpr gpio_num_t GPIO_NUM_40 = 40;
constexpr gpio_num_t GPIO_NUM_41 = 41;
constexpr gpio_num_t GPIO_NUM_42 = 42;
constexpr gpio_num_t GPIO_NUM_47 = 47;
constexpr int GPIO_MODE_INPUT = 1;
constexpr int GPIO_PULLUP_DISABLE = 0;
constexpr int GPIO_PULLDOWN_DISABLE = 0;

struct gpio_config_t {
    uint64_t pin_bit_mask{};
    int mode{};
    int pull_up_en{};
    int pull_down_en{};
};

using spi_host_device_t = int;
constexpr spi_host_device_t SPI2_HOST = 2;
constexpr int SPI_DMA_CH_AUTO = 0;

struct spi_bus_config_t {
    int mosi_io_num{};
    int miso_io_num{};
    int sclk_io_num{};
    int quadwp_io_num{};
    int quadhd_io_num{};
    int max_transfer_sz{};
};

struct spi_device_interface_config_t {
    int mode{};
    int clock_speed_hz{};
    int spics_io_num{};
    int queue_size{};
    int cs_ena_pretrans{};
    int cs_ena_posttrans{};
};

namespace fake_sensors {

constexpr size_t kMax31856Count = 2;
constexpr size_t kGpioCount = 64;

struct Device {
    std::array<uint8_t, 16> registers{};
    size_t index{};
    gpio_num_t chip_select{GPIO_NUM_NC};
    bool physical_fault{};
    uint8_t physical_fault_status{0x01};
    uint8_t latched_status{};
    bool conversion_started{};
    bool conversion_complete{};
    bool hold_drdy_low_on_start{};
    bool reassert_fault_immediately{};
    bool fail_next_fault_clear{};
    bool fail_next_read{};
};

struct Write {
    size_t device{};
    uint8_t address{};
    uint8_t value{};
    bool succeeded{};
};

struct Ds18b20Model {
    bool available{true};
    bool reset_ok{true};
    bool write_ok{true};
    bool conversion_done{true};
    bool iterator_returned{};
    uint8_t last_command{};
    std::array<uint8_t, 8> rom{};
    std::array<uint8_t, 9> scratchpad{};
};

struct FakeBus {};
struct FakeIter {
    bool returned{};
};

inline std::vector<std::unique_ptr<Device>> owned_devices;
inline std::vector<Device*> devices;
inline std::vector<Write> writes;
inline std::array<int, kGpioCount> gpio_levels{};
inline uint64_t clock_us{};
inline esp_err_t gpio_config_result{ESP_OK};
inline esp_err_t spi_bus_initialize_result{ESP_OK};
inline spi_bus_config_t bus_config{};
inline std::vector<spi_device_interface_config_t> device_configs;
inline Ds18b20Model ds18b20{};
inline std::vector<std::unique_ptr<FakeBus>> owned_buses;
inline std::vector<std::unique_ptr<FakeIter>> owned_iters;

inline uint8_t ds18b20_crc8(const uint8_t* data, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t value = data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint8_t mix = static_cast<uint8_t>((crc ^ value) & 1U);
            crc >>= 1U;
            if (mix != 0U) crc ^= 0x8CU;
            value >>= 1U;
        }
    }
    return crc;
}

inline void prepare_valid_ds18b20() {
    ds18b20 = {};
    ds18b20.rom = {0x28, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x00};
    ds18b20.rom[7] = ds18b20_crc8(ds18b20.rom.data(), 7);
    ds18b20.scratchpad = {0x78, 0x01, 0x00, 0x00, 0x7F, 0xFF, 0x0C, 0x10, 0x00};  // 23.5 C, 12-bit.
    ds18b20.scratchpad[8] = ds18b20_crc8(ds18b20.scratchpad.data(), 8);
}

inline void reset() {
    devices.clear();
    writes.clear();
    device_configs.clear();
    gpio_levels.fill(1);
    clock_us = 0;
    gpio_config_result = ESP_OK;
    spi_bus_initialize_result = ESP_OK;
    bus_config = {};
    prepare_valid_ds18b20();
}

inline Device& device(size_t index) { return *devices.at(index); }

inline gpio_num_t drdy_pin(size_t index) {
    return index == 0 ? GPIO_NUM_1 : GPIO_NUM_2;
}

inline void set_ready(size_t index, bool ready) {
    auto& value = device(index);
    gpio_levels.at(static_cast<size_t>(drdy_pin(index))) = ready ? 0 : 1;
    if (ready) value.conversion_complete = true;
}

inline void set_drdy_stuck_low(size_t index, bool stuck) {
    auto& value = device(index);
    value.hold_drdy_low_on_start = stuck;
    gpio_levels.at(static_cast<size_t>(drdy_pin(index))) = stuck ? 0 : 1;
}

inline void set_temperature_milli_c(size_t index, int32_t milli_c) {
    auto& value = device(index);
    const int32_t raw19 = static_cast<int32_t>((static_cast<int64_t>(milli_c) * 128) / 1000);
    const uint32_t raw24 = (static_cast<uint32_t>(raw19) & 0x7FFFFU) << 5U;
    value.registers[0x0C] = static_cast<uint8_t>(raw24 >> 16U);
    value.registers[0x0D] = static_cast<uint8_t>(raw24 >> 8U);
    value.registers[0x0E] = static_cast<uint8_t>(raw24);
}

inline void set_physical_fault(size_t index, bool present, uint8_t status = 0x01) {
    auto& value = device(index);
    value.physical_fault = present;
    value.physical_fault_status = status;
}

inline void set_fault_reassert_immediately(size_t index, bool enabled) {
    device(index).reassert_fault_immediately = enabled;
}

inline void fail_next_fault_clear(size_t index) {
    device(index).fail_next_fault_clear = true;
}

inline void corrupt_ds18b20_crc() {
    ds18b20.scratchpad[8] ^= 0x01U;
}

inline size_t cr0_write_count(size_t index, uint8_t value, bool only_successful = true) {
    size_t result = 0;
    for (const auto& write : writes) {
        if (write.device == index && write.address == 0x00 && write.value == value &&
            (!only_successful || write.succeeded)) {
            ++result;
        }
    }
    return result;
}

inline const Write* last_cr0_write(size_t index) {
    for (auto it = writes.rbegin(); it != writes.rend(); ++it) {
        if (it->device == index && it->address == 0x00) return &*it;
    }
    return nullptr;
}

}  // namespace fake_sensors

using spi_device_handle_t = fake_sensors::Device*;

struct spi_transaction_t {
    size_t length{};
    size_t rxlength{};
    const void* tx_buffer{};
    void* rx_buffer{};
};

inline esp_err_t gpio_config(const gpio_config_t*) {
    return fake_sensors::gpio_config_result;
}

inline int gpio_get_level(gpio_num_t pin) {
    if (pin < 0 || static_cast<size_t>(pin) >= fake_sensors::gpio_levels.size()) return 1;
    return fake_sensors::gpio_levels[static_cast<size_t>(pin)];
}

inline esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t* config, int) {
    if (config != nullptr) fake_sensors::bus_config = *config;
    return fake_sensors::spi_bus_initialize_result;
}

inline esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t* config,
                                    spi_device_handle_t* output) {
    if (config == nullptr || output == nullptr) return ESP_FAIL;
    const size_t index = fake_sensors::devices.size();
    auto value = std::make_unique<fake_sensors::Device>();
    value->index = index;
    value->chip_select = config->spics_io_num;
    fake_sensors::Device* raw = value.get();
    fake_sensors::owned_devices.push_back(std::move(value));
    fake_sensors::devices.push_back(raw);
    fake_sensors::device_configs.push_back(*config);
    *output = raw;
    return ESP_OK;
}

inline esp_err_t spi_device_transmit(spi_device_handle_t device, spi_transaction_t* transaction) {
    if (device == nullptr || transaction == nullptr || transaction->tx_buffer == nullptr) return ESP_FAIL;
    const auto* tx = static_cast<const uint8_t*>(transaction->tx_buffer);
    const size_t tx_bytes = transaction->length / 8U;
    if (tx_bytes == 0) return ESP_FAIL;

    if ((tx[0] & 0x80U) != 0U) {
        if (tx_bytes < 2) return ESP_FAIL;
        const uint8_t address = static_cast<uint8_t>(tx[0] & 0x7FU);
        const uint8_t value = tx[1];
        const bool fault_clear = address == 0x00 && (value & 0x02U) != 0U;
        const bool fail = fault_clear && device->fail_next_fault_clear;
        fake_sensors::writes.push_back({device->index, address, value, !fail});
        if (fail) {
            device->fail_next_fault_clear = false;
            return ESP_FAIL;
        }
        if (address >= device->registers.size()) return ESP_FAIL;
        if (address == 0x00) {
            // 1SHOT and FAULTCLR are self-clearing; the remaining CR0 bits are retained.
            device->registers[0x00] = static_cast<uint8_t>(value & 0xBDU);
            if (fault_clear) {
                device->latched_status = device->reassert_fault_immediately && device->physical_fault
                    ? device->physical_fault_status : 0;
            }
            if ((value & 0x40U) != 0U) {
                device->conversion_started = true;
                device->conversion_complete = false;
                fake_sensors::gpio_levels.at(static_cast<size_t>(fake_sensors::drdy_pin(device->index))) =
                    (device->hold_drdy_low_on_start ||
                     (device->reassert_fault_immediately && device->physical_fault)) ? 0 : 1;
            }
        } else {
            device->registers[address] = value;
        }
        return ESP_OK;
    }

    if (device->fail_next_read) {
        device->fail_next_read = false;
        return ESP_FAIL;
    }
    if (transaction->rx_buffer == nullptr) return ESP_FAIL;
    auto* rx = static_cast<uint8_t*>(transaction->rx_buffer);
    const size_t rx_bytes = transaction->rxlength / 8U;
    if (rx_bytes == 0) return ESP_FAIL;
    std::memset(rx, 0, rx_bytes);
    const uint8_t address = static_cast<uint8_t>(tx[0] & 0x7FU);
    for (size_t i = 1; i < rx_bytes; ++i) {
        const uint8_t reg = static_cast<uint8_t>(address + i - 1U);
        if (reg == 0x0FU) {
            if (device->conversion_complete && device->physical_fault) {
                device->latched_status = static_cast<uint8_t>(device->latched_status | device->physical_fault_status);
            }
            rx[i] = device->latched_status;
        } else if (reg < device->registers.size()) {
            rx[i] = device->registers[reg];
        }
    }
    return ESP_OK;
}

inline int64_t esp_timer_get_time() {
    return static_cast<int64_t>(fake_sensors::clock_us);
}

using onewire_bus_handle_t = fake_sensors::FakeBus*;
using onewire_device_iter_handle_t = fake_sensors::FakeIter*;
using onewire_device_address_t = uint64_t;

struct onewire_bus_config_t {
    int bus_gpio_num{};
    struct {
        bool en_pull_up{};
    } flags;
};

struct onewire_bus_rmt_config_t {
    size_t max_rx_bytes{};
};

struct onewire_device_t {
    onewire_device_address_t address{};
};

inline esp_err_t onewire_new_bus_rmt(const onewire_bus_config_t*, const onewire_bus_rmt_config_t*,
                                     onewire_bus_handle_t* output) {
    if (output == nullptr || !fake_sensors::ds18b20.available) return ESP_FAIL;
    auto value = std::make_unique<fake_sensors::FakeBus>();
    *output = value.get();
    fake_sensors::owned_buses.push_back(std::move(value));
    return ESP_OK;
}

inline esp_err_t onewire_new_device_iter(onewire_bus_handle_t bus, onewire_device_iter_handle_t* output) {
    if (bus == nullptr || output == nullptr) return ESP_FAIL;
    auto value = std::make_unique<fake_sensors::FakeIter>();
    *output = value.get();
    fake_sensors::owned_iters.push_back(std::move(value));
    return ESP_OK;
}

inline esp_err_t onewire_device_iter_get_next(onewire_device_iter_handle_t iterator, onewire_device_t* output) {
    if (iterator == nullptr || output == nullptr || iterator->returned) return ESP_ERR_NOT_FOUND;
    iterator->returned = true;
    onewire_device_address_t address = 0;
    for (unsigned i = 0; i < fake_sensors::ds18b20.rom.size(); ++i) {
        address |= static_cast<onewire_device_address_t>(fake_sensors::ds18b20.rom[i]) << (i * 8U);
    }
    output->address = address;
    return ESP_OK;
}

inline esp_err_t onewire_del_device_iter(onewire_device_iter_handle_t) {
    return ESP_OK;
}

inline esp_err_t onewire_bus_reset(onewire_bus_handle_t bus) {
    return bus != nullptr && fake_sensors::ds18b20.reset_ok ? ESP_OK : ESP_FAIL;
}

inline esp_err_t onewire_bus_write_bytes(onewire_bus_handle_t bus, const uint8_t* data, size_t size) {
    if (bus == nullptr || data == nullptr || !fake_sensors::ds18b20.write_ok) return ESP_FAIL;
    if (size == 10 && data[0] == 0x55U) fake_sensors::ds18b20.last_command = data[9];
    return ESP_OK;
}

inline esp_err_t onewire_bus_read_bytes(onewire_bus_handle_t bus, uint8_t* output, size_t size) {
    if (bus == nullptr || output == nullptr) return ESP_FAIL;
    if (fake_sensors::ds18b20.last_command == 0x44U && size == 1) {
        output[0] = fake_sensors::ds18b20.conversion_done ? 0xFFU : 0x00U;
        return ESP_OK;
    }
    if (fake_sensors::ds18b20.last_command == 0xBEU && size == fake_sensors::ds18b20.scratchpad.size()) {
        std::memcpy(output, fake_sensors::ds18b20.scratchpad.data(), size);
        return ESP_OK;
    }
    return ESP_FAIL;
}

#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
