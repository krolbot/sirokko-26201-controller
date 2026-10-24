#include "local_display.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "local_display";
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kOrange = 0xFD20;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kBlue = 0x041F;
constexpr uint16_t kDarkBlue = 0x0010;
constexpr uint16_t kDarkRed = 0x3000;
constexpr uint16_t kDarkGreen = 0x0180;
constexpr int kTextScale = 2;
constexpr int64_t kTransferTimeoutUs = 50'000;

using Glyph = std::array<uint8_t, 5>;

uint16_t wire_color(uint16_t rgb565) {
    return static_cast<uint16_t>((rgb565 << 8) | (rgb565 >> 8));
}

uint32_t next_codepoint(const char*& text) {
    const uint8_t first = static_cast<uint8_t>(*text++);
    if (first < 0x80) return first;
    if ((first & 0xE0) == 0xC0 && *text != '\0') {
        const uint8_t second = static_cast<uint8_t>(*text++);
        return static_cast<uint32_t>((first & 0x1F) << 6) | (second & 0x3F);
    }
    return '?';
}

Glyph glyph(uint32_t character) {
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
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
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        case '%': return {0x63, 0x13, 0x08, 0x64, 0x63};
        case ' ': return {0, 0, 0, 0, 0};
        case 0x410: return {0x7E, 0x11, 0x11, 0x11, 0x7E};  // А
        case 0x411: return {0x7F, 0x49, 0x49, 0x49, 0x31};  // Б
        case 0x412: return {0x7F, 0x49, 0x49, 0x49, 0x36};  // В
        case 0x413: return {0x7F, 0x01, 0x01, 0x01, 0x01};  // Г
        case 0x414: return {0x60, 0x3E, 0x21, 0x3E, 0x60};  // Д
        case 0x415: return {0x7F, 0x49, 0x49, 0x49, 0x41};  // Е
        case 0x416: return {0x63, 0x14, 0x7F, 0x14, 0x63};  // Ж
        case 0x417: return {0x22, 0x41, 0x49, 0x49, 0x36};  // З
        case 0x418: return {0x7F, 0x10, 0x08, 0x04, 0x7F};  // И
        case 0x419: return {0x7D, 0x12, 0x0A, 0x05, 0x7D};  // Й
        case 0x41A: return {0x7F, 0x08, 0x14, 0x22, 0x41};  // К
        case 0x41B: return {0x60, 0x1F, 0x01, 0x01, 0x7F};  // Л
        case 0x41C: return {0x7F, 0x02, 0x0C, 0x02, 0x7F};  // М
        case 0x41D: return {0x7F, 0x08, 0x08, 0x08, 0x7F};  // Н
        case 0x41E: return {0x3E, 0x41, 0x41, 0x41, 0x3E};  // О
        case 0x41F: return {0x7F, 0x01, 0x01, 0x01, 0x7F};  // П
        case 0x420: return {0x7F, 0x09, 0x09, 0x09, 0x06};  // Р
        case 0x421: return {0x3E, 0x41, 0x41, 0x41, 0x22};  // С
        case 0x422: return {0x01, 0x01, 0x7F, 0x01, 0x01};  // Т
        case 0x423: return {0x07, 0x48, 0x48, 0x48, 0x3F};  // У
        case 0x424: return {0x1C, 0x22, 0x7F, 0x22, 0x1C};  // Ф
        case 0x425: return {0x63, 0x14, 0x08, 0x14, 0x63};  // Х
        case 0x426: return {0x3F, 0x20, 0x20, 0x3F, 0x60};  // Ц
        case 0x427: return {0x0F, 0x08, 0x08, 0x08, 0x7F};  // Ч
        case 0x428: return {0x7F, 0x40, 0x7F, 0x40, 0x7F};  // Ш
        case 0x429: return {0x3F, 0x20, 0x3F, 0x20, 0x7F};  // Щ
        case 0x42A: return {0x01, 0x7F, 0x48, 0x48, 0x30};  // Ъ
        case 0x42B: return {0x7F, 0x48, 0x30, 0x00, 0x7F};  // Ы
        case 0x42C: return {0x7F, 0x48, 0x48, 0x48, 0x30};  // Ь
        case 0x42D: return {0x22, 0x41, 0x49, 0x49, 0x3E};  // Э
        case 0x42E: return {0x7F, 0x08, 0x3E, 0x41, 0x3E};  // Ю
        case 0x42F: return {0x46, 0x29, 0x19, 0x09, 0x7F};  // Я
        default: return {0x02, 0x01, 0x51, 0x09, 0x06};
    }
}

void set_pixel(uint16_t* pixels, int x, int y, uint16_t color) {
    if (x < 0 || x >= sirokko::board::kTftWidth ||
        y < 0 || y >= sirokko::board::kTftBandRows) {
        return;
    }
    pixels[y * sirokko::board::kTftWidth + x] = wire_color(color);
}

void draw_text(uint16_t* pixels, int x, int y, const char* text, uint16_t color) {
    while (*text != '\0' && x + 5 * kTextScale <= sirokko::board::kTftWidth) {
        const Glyph columns = glyph(next_codepoint(text));
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((columns[column] & (1u << row)) == 0) continue;
                for (int dx = 0; dx < kTextScale; ++dx) {
                    for (int dy = 0; dy < kTextScale; ++dy) {
                        set_pixel(pixels, x + column * kTextScale + dx,
                                  y + row * kTextScale + dy, color);
                    }
                }
            }
        }
        x += 6 * kTextScale;
    }
}

void format_temperature(char* output, size_t output_size, const char* label,
                        const sirokko::SensorSample& sample) {
    if (sample.quality != sirokko::SensorQuality::OK) {
        std::snprintf(output, output_size, "%s: НЕТ ДАННЫХ", label);
        return;
    }
    const int64_t value = sample.milli_c;
    const int64_t absolute = value < 0 ? -value : value;
    std::snprintf(output, output_size, "%s: %s%lld.%01lld C", label,
                  value < 0 ? "-" : "", static_cast<long long>(absolute / 1000),
                  static_cast<long long>((absolute % 1000) / 100));
}

const char* state_label(sirokko::State state) {
    switch (state) {
        case sirokko::State::BOOT_SELF_TEST: return "САМОТЕСТ";
        case sirokko::State::RECOVERY_PURGE: return "ВОССТ. ПРОДУВКА";
        case sirokko::State::WAIT_COOLDOWN: return "ОЖИДАНИЕ";
        case sirokko::State::PREPURGE: return "ПРЕДПРОДУВКА";
        case sirokko::State::PREHEAT: return "НАКАЛ";
        case sirokko::State::SPINUP: return "РАЗГОН";
        case sirokko::State::IGNITION_TRIAL: return "РОЗЖИГ";
        case sirokko::State::FLAME_CONFIRM: return "ПРОВЕРКА ПЛАМЕНИ";
        case sirokko::State::RUN_HIGH: return "РАБОТА 100%";
        case sirokko::State::RUN_LOW: return "РАБОТА МАЛАЯ";
        case sirokko::State::POST_PURGE: return "ПРОДУВКА";
        case sirokko::State::LOCKOUT: return "БЛОКИРОВКА";
    }
    return "НЕИЗВЕСТНО";
}

const char* fault_label(uint32_t faults) {
    if (faults == sirokko::FAULT_NONE) return "НЕТ";
    if (faults & sirokko::FAULT_CONFIG) return "КОНФИГ";
    if (faults & sirokko::FAULT_SAFETY_OPEN) return "ЗАЩИТА";
    if (faults & sirokko::FAULT_EXHAUST_SENSOR) return "ДАТЧИК ВЫХЛОПА";
    if (faults & sirokko::FAULT_AIR_SENSOR) return "ДАТЧИК ВОЗДУХА";
    if (faults & sirokko::FAULT_ROOM_SENSOR) return "ДАТЧИК КОМНАТЫ";
    if (faults & sirokko::FAULT_EXHAUST_OVERTEMP) return "ПЕРЕГРЕВ ВЫХЛОПА";
    if (faults & sirokko::FAULT_AIR_OVERTEMP) return "ПЕРЕГРЕВ ВОЗДУХА";
    if (faults & sirokko::FAULT_IGNITION_NOT_PROVEN) return "НЕТ РОЗЖИГА";
    if (faults & sirokko::FAULT_FLAME_LOST) return "ПОТЕРЯ ПЛАМЕНИ";
    if (faults & sirokko::FAULT_GLOW_TIMEOUT) return "ТАЙМАУТ НАКАЛА";
    if (faults & sirokko::FAULT_BROWNOUT_RESET) return "СБОЙ ПИТАНИЯ";
    if (faults & sirokko::FAULT_INTERNAL_INVARIANT) return "ОШИБКА ЛОГИКИ";
    if (faults & sirokko::FAULT_SELF_TEST_TIMEOUT) return "САМОТЕСТ";
    if (faults & sirokko::FAULT_LOCAL_UI) return "ЭКРАН";
    return "НЕИЗВЕСТНО";
}

bool on_color_transfer_done(esp_lcd_panel_io_handle_t,
                            esp_lcd_panel_io_event_data_t*, void* user_context) {
    static_cast<std::atomic<bool>*>(user_context)->store(false, std::memory_order_release);
    return false;
}
}  // namespace

bool LocalDisplay::begin() {
    auto fail = [this](const char* step, esp_err_t error) {
        ESP_LOGE(kTag, "%s failed: %s", step, esp_err_to_name(error));
        healthy_ = false;
        if (panel_) {
            esp_lcd_panel_del(static_cast<esp_lcd_panel_handle_t>(panel_));
            panel_ = nullptr;
        }
        if (io_) {
            esp_lcd_panel_io_del(static_cast<esp_lcd_panel_io_handle_t>(io_));
            io_ = nullptr;
        }
        if (pixels_) {
            heap_caps_free(pixels_);
            pixels_ = nullptr;
        }
        return false;
    };

    pixels_ = static_cast<uint16_t*>(
        spi_bus_dma_memory_alloc(kSpiHost, sirokko::board::kTftBandBytes, 0));
    if (!pixels_) return fail("DMA buffer allocation", ESP_ERR_NO_MEM);

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = sirokko::board::kTftCs;
    io_config.dc_gpio_num = sirokko::board::kTftDc;
    io_config.spi_mode = sirokko::board::kTftSpiMode;
    io_config.pclk_hz = sirokko::board::kTftSpiClockHz;
    io_config.trans_queue_depth = 1;
    io_config.on_color_trans_done = on_color_transfer_done;
    io_config.user_ctx = &transfer_in_progress_;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;

    esp_lcd_panel_io_handle_t io{};
    esp_err_t error = esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &io_config, &io);
    if (error != ESP_OK) return fail("ST7789 SPI IO", error);
    io_ = io;

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = sirokko::board::kTftReset;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;

    esp_lcd_panel_handle_t panel{};
    error = esp_lcd_new_panel_st7789(io, &panel_config, &panel);
    if (error != ESP_OK) return fail("ST7789 panel", error);
    panel_ = panel;

    // reset_gpio_num=-1 makes the IDF driver issue SWRESET on every MCU boot,
    // including software resets where the display's hardware RST/EN did not toggle.
    if ((error = esp_lcd_panel_reset(panel)) != ESP_OK) return fail("ST7789 software reset", error);
    if ((error = esp_lcd_panel_init(panel)) != ESP_OK) return fail("ST7789 init", error);

    std::fill_n(pixels_, sirokko::board::kTftWidth * sirokko::board::kTftBandRows,
                wire_color(kBlack));
    healthy_ = true;
    for (uint8_t band = 0; band < sirokko::board::kTftBandCount; ++band) {
        if (!submit_band(band)) return fail("ST7789 clear", ESP_FAIL);
        while (transfer_in_progress_.load(std::memory_order_acquire)) {
            if (esp_timer_get_time() - transfer_started_us_ > kTransferTimeoutUs) {
                // Do not delete the SPI panel IO while a transaction is stuck: ESP-IDF waits
                // forever for in-flight transactions during deletion. Keep the handles alive,
                // report failed initialization, and let the control core enter safe recovery.
                ESP_LOGE(kTag, "ST7789 clear timeout");
                healthy_ = false;
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if ((error = esp_lcd_panel_disp_on_off(panel, true)) != ESP_OK) {
        return fail("ST7789 display on", error);
    }

    next_band_ = 0;
    ESP_LOGI(kTag, "ST7789 init=1 %dx%d SPI=%luHz CS=%d DC=%d shared_bus=SPI2",
             sirokko::board::kTftWidth, sirokko::board::kTftHeight,
             static_cast<unsigned long>(sirokko::board::kTftSpiClockHz),
             sirokko::board::kTftCs, sirokko::board::kTftDc);
    return true;
}

bool LocalDisplay::submit_band(uint8_t band) {
    if (!healthy_ || !panel_ || !pixels_ || band >= sirokko::board::kTftBandCount) return false;
    transfer_in_progress_.store(true, std::memory_order_release);
    transfer_started_us_ = esp_timer_get_time();
    const int y_start = band * sirokko::board::kTftBandRows;
    const esp_err_t error = esp_lcd_panel_draw_bitmap(
        static_cast<esp_lcd_panel_handle_t>(panel_), 0, y_start,
        sirokko::board::kTftWidth, y_start + sirokko::board::kTftBandRows, pixels_);
    if (error != ESP_OK) {
        transfer_in_progress_.store(false, std::memory_order_release);
        healthy_ = false;
        ESP_LOGE(kTag, "ST7789 transfer failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

void LocalDisplay::compose_band(uint8_t band,
                                int32_t setpoint_milli_c,
                                const sirokko::SensorSample& room,
                                const sirokko::SensorSample& exhaust,
                                const sirokko::SensorSample& hot_air,
                                const sirokko::Status& status) {
    uint16_t background = kBlack;
    uint16_t foreground = kWhite;
    char line[64]{};

    switch (band) {
        case 0:
            background = kDarkBlue;
            foreground = kCyan;
            std::snprintf(line, sizeof(line), "СИРОККО TFT");
            break;
        case 1:
            foreground = status.state == sirokko::State::LOCKOUT ? kRed : kYellow;
            std::snprintf(line, sizeof(line), "%s", state_label(status.state));
            break;
        case 2:
            format_temperature(line, sizeof(line), "КОМН", room);
            break;
        case 3: {
            foreground = kGreen;
            const int64_t value = setpoint_milli_c;
            const int64_t absolute = value < 0 ? -value : value;
            std::snprintf(line, sizeof(line), "УСТ: %s%lld.%01lld C", value < 0 ? "-" : "",
                          static_cast<long long>(absolute / 1000),
                          static_cast<long long>((absolute % 1000) / 100));
            break;
        }
        case 4:
            format_temperature(line, sizeof(line), "ВЫХЛОП", exhaust);
            break;
        case 5:
            format_temperature(line, sizeof(line), "ВОЗДУХ", hot_air);
            break;
        case 6:
            background = status.faults == sirokko::FAULT_NONE ? kDarkGreen : kDarkRed;
            foreground = status.faults == sirokko::FAULT_NONE ? kGreen : kRed;
            std::snprintf(line, sizeof(line), "АВАРИЯ: %s", fault_label(status.faults));
            break;
        case 7:
            if (status.faults != sirokko::FAULT_NONE) {
                foreground = kOrange;
                std::snprintf(line, sizeof(line), "КОД: %08lX",
                              static_cast<unsigned long>(status.faults));
            } else if (status.state == sirokko::State::RECOVERY_PURGE ||
                       status.state == sirokko::State::PREPURGE ||
                       status.state == sirokko::State::POST_PURGE) {
                foreground = kBlue;
                std::snprintf(line, sizeof(line), "ПРОДУВКА: %lu СЕК",
                              static_cast<unsigned long>(status.state_age_ms / 1000u));
            } else {
                std::snprintf(line, sizeof(line), "ВРЕМЯ: %lu СЕК",
                              static_cast<unsigned long>(status.state_age_ms / 1000u));
            }
            break;
        default:
            return;
    }

    std::fill_n(pixels_, sirokko::board::kTftWidth * sirokko::board::kTftBandRows,
                wire_color(background));
    draw_text(pixels_, 6, 12, line, foreground);
}

bool LocalDisplay::render(int32_t setpoint_milli_c,
                          const sirokko::SensorSample& room,
                          const sirokko::SensorSample& exhaust,
                          const sirokko::SensorSample& hot_air,
                          const sirokko::Status& status) {
    if (!healthy_ || !panel_ || !pixels_) return false;
    if (transfer_in_progress_.load(std::memory_order_acquire)) {
        if (esp_timer_get_time() - transfer_started_us_ > kTransferTimeoutUs) {
            healthy_ = false;
            ESP_LOGE(kTag, "ST7789 transfer timeout");
            return false;
        }
        return true;
    }

    compose_band(next_band_, setpoint_milli_c, room, exhaust, hot_air, status);
    if (!submit_band(next_band_)) return false;
    next_band_ = static_cast<uint8_t>((next_band_ + 1u) % sirokko::board::kTftBandCount);
    return true;
}
