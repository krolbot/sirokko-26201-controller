#include "local_display.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "local_display";
constexpr gpio_num_t kSda = GPIO_NUM_47;
constexpr gpio_num_t kScl = GPIO_NUM_48;
constexpr uint16_t kAddress = 0x3C;

using Frame = std::array<uint8_t, 128 * 8>;

std::array<uint8_t, 5> glyph(char character) {
    if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    switch (character) {
        case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
        case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
        case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
        case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
        case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'W': return {0x3F, 0x40, 0x38, 0x40, 0x3F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
        case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
        case ' ': return {0, 0, 0, 0, 0};
        default: return {0x02, 0x01, 0x51, 0x09, 0x06};
    }
}

void draw_text(Frame& frame, unsigned row, const char* text) {
    if (row >= 8) return;
    unsigned x = 1;
    for (; *text != '\0' && x + 5 < 128; ++text, x += 6) {
        const auto columns = glyph(*text);
        for (unsigned i = 0; i < columns.size(); ++i) frame[row * 128 + x + i] = columns[i];
    }
}

void format_temperature(char* output, size_t output_size, const char* label,
                        const sirokko::SensorSample& sample) {
    if (sample.quality != sirokko::SensorQuality::OK) {
        std::snprintf(output, output_size, "%s INVALID", label);
        return;
    }
    const int32_t absolute = sample.milli_c < 0 ? -sample.milli_c : sample.milli_c;
    std::snprintf(output, output_size, "%s %s%ld.%01ldC", label,
                  sample.milli_c < 0 ? "-" : "",
                  static_cast<long>(absolute / 1000), static_cast<long>((absolute % 1000) / 100));
}

bool transmit(i2c_master_dev_handle_t device, const uint8_t* bytes, size_t size) {
    return i2c_master_transmit(device, bytes, size, 100) == ESP_OK;
}
}  // namespace

bool LocalDisplay::begin() {
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = kSda;
    bus_config.scl_io_num = kScl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = false;
    i2c_master_bus_handle_t bus{};
    if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) return false;

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = kAddress;
    device_config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t device{};
    if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK) return false;

    bus_ = bus;
    device_ = device;
    const uint8_t init[] = {
        0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81,
        0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    healthy_ = transmit(device, init, sizeof(init));
    ESP_LOGI(kTag, "SSD1306 init=%d address=0x%02x", healthy_, kAddress);
    return healthy_;
}

bool LocalDisplay::render(int32_t setpoint_milli_c,
                          const sirokko::SensorSample& room,
                          const sirokko::SensorSample& exhaust,
                          const sirokko::SensorSample& hot_air,
                          const sirokko::Status& status) {
    if (!healthy_ || !device_) return false;
    Frame frame{};
    char line[32]{};
    const int32_t setpoint_abs = setpoint_milli_c < 0 ? -setpoint_milli_c : setpoint_milli_c;
    std::snprintf(line, sizeof(line), "SET %s%ld.%01ldC", setpoint_milli_c < 0 ? "-" : "",
                  static_cast<long>(setpoint_abs / 1000),
                  static_cast<long>((setpoint_abs % 1000) / 100));
    draw_text(frame, 0, line);
    format_temperature(line, sizeof(line), "ROOM", room);
    draw_text(frame, 1, line);
    format_temperature(line, sizeof(line), "EXH", exhaust);
    draw_text(frame, 2, line);
    format_temperature(line, sizeof(line), "AIR", hot_air);
    draw_text(frame, 3, line);
    std::snprintf(line, sizeof(line), "STATE %s", sirokko::state_name(status.state));
    draw_text(frame, 5, line);
    std::snprintf(line, sizeof(line), "FAULT %08lX", static_cast<unsigned long>(status.faults));
    draw_text(frame, 7, line);

    auto device = static_cast<i2c_master_dev_handle_t>(device_);
    const uint8_t address_window[] = {0x00, 0x21, 0, 127, 0x22, 0, 7};
    if (!transmit(device, address_window, sizeof(address_window))) {
        healthy_ = false;
        return false;
    }
    std::array<uint8_t, 17> chunk{};
    chunk[0] = 0x40;
    for (size_t offset = 0; offset < frame.size(); offset += 16) {
        std::memcpy(chunk.data() + 1, frame.data() + offset, 16);
        if (!transmit(device, chunk.data(), chunk.size())) {
            healthy_ = false;
            return false;
        }
    }
    return true;
}
