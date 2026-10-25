#include "sirokko/ui.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace sirokko {
namespace {

constexpr uint32_t kMenuTimeoutMs = 30'000;
constexpr uint32_t kMenuChordMs = 800;
constexpr uint32_t kResetCountersChordMs = 2'000;
constexpr int kTextScale = 2;
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6;
constexpr int kMaxGlyphsPerLine = kUiWidth / (kGlyphAdvance * kTextScale);

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xffff;
constexpr uint16_t kCyan = 0x07ff;
constexpr uint16_t kGreen = 0x07e0;
constexpr uint16_t kYellow = 0xffe0;
constexpr uint16_t kOrange = 0xfd20;
constexpr uint16_t kRed = 0xf800;
constexpr uint16_t kBlue = 0x041f;
constexpr uint16_t kDarkBlue = 0x0010;
constexpr uint16_t kDarkRed = 0x3000;
constexpr uint16_t kDarkGreen = 0x0180;

using Glyph = std::array<uint8_t, kGlyphWidth>;

uint16_t wire_color(uint16_t rgb565) {
    return static_cast<uint16_t>((rgb565 << 8) | (rgb565 >> 8));
}

uint32_t next_codepoint(const char*& text) {
    const uint8_t first = static_cast<uint8_t>(*text++);
    if (first < 0x80) return first;
    if ((first & 0xe0) == 0xc0 && *text != '\0') {
        const uint8_t second = static_cast<uint8_t>(*text++);
        return static_cast<uint32_t>((first & 0x1f) << 6) | (second & 0x3f);
    }
    return '?';
}

Glyph glyph(uint32_t character) {
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character >= 0x430 && character <= 0x44f) character -= 0x20;  // Russian lowercase.
    if (character == 0x451) character = 0x401;  // ё -> Ё

    switch (character) {
        case '0': return {0x3e, 0x51, 0x49, 0x45, 0x3e};
        case '1': return {0x00, 0x42, 0x7f, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4b, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7f, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3c, 0x4a, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1e};
        case 'A': return {0x7e, 0x11, 0x11, 0x11, 0x7e};
        case 'B': return {0x7f, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3e, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7f, 0x41, 0x41, 0x22, 0x1c};
        case 'E': return {0x7f, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7f, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3e, 0x41, 0x49, 0x49, 0x7a};
        case 'H': return {0x7f, 0x08, 0x08, 0x08, 0x7f};
        case 'I': return {0x00, 0x41, 0x7f, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3f, 0x01};
        case 'K': return {0x7f, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7f, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7f, 0x02, 0x0c, 0x02, 0x7f};
        case 'N': return {0x7f, 0x04, 0x08, 0x10, 0x7f};
        case 'O': return {0x3e, 0x41, 0x41, 0x41, 0x3e};
        case 'P': return {0x7f, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3e, 0x41, 0x51, 0x21, 0x5e};
        case 'R': return {0x7f, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7f, 0x01, 0x01};
        case 'U': return {0x3f, 0x40, 0x40, 0x40, 0x3f};
        case 'V': return {0x1f, 0x20, 0x40, 0x20, 0x1f};
        case 'W': return {0x3f, 0x40, 0x38, 0x40, 0x3f};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '+': return {0x08, 0x08, 0x3e, 0x08, 0x08};
        case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        case '/': return {0x60, 0x10, 0x08, 0x04, 0x03};
        case '%': return {0x63, 0x13, 0x08, 0x64, 0x63};
        case '=': return {0x14, 0x14, 0x14, 0x14, 0x14};
        case '>': return {0x00, 0x41, 0x22, 0x14, 0x08};
        case '?': return {0x02, 0x01, 0x51, 0x09, 0x06};
        case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00};
        case 0x401: return {0x7d, 0x54, 0x54, 0x54, 0x44};  // Ё
        case 0x410: return {0x7e, 0x11, 0x11, 0x11, 0x7e};  // А
        case 0x411: return {0x7f, 0x49, 0x49, 0x49, 0x31};  // Б
        case 0x412: return {0x7f, 0x49, 0x49, 0x49, 0x36};  // В
        case 0x413: return {0x7f, 0x01, 0x01, 0x01, 0x01};  // Г
        case 0x414: return {0x60, 0x3e, 0x21, 0x3e, 0x60};  // Д
        case 0x415: return {0x7f, 0x49, 0x49, 0x49, 0x41};  // Е
        case 0x416: return {0x63, 0x14, 0x7f, 0x14, 0x63};  // Ж
        case 0x417: return {0x22, 0x41, 0x49, 0x49, 0x36};  // З
        case 0x418: return {0x7f, 0x10, 0x08, 0x04, 0x7f};  // И
        case 0x419: return {0x7d, 0x12, 0x0a, 0x05, 0x7d};  // Й
        case 0x41a: return {0x7f, 0x08, 0x14, 0x22, 0x41};  // К
        case 0x41b: return {0x60, 0x1f, 0x01, 0x01, 0x7f};  // Л
        case 0x41c: return {0x7f, 0x02, 0x0c, 0x02, 0x7f};  // М
        case 0x41d: return {0x7f, 0x08, 0x08, 0x08, 0x7f};  // Н
        case 0x41e: return {0x3e, 0x41, 0x41, 0x41, 0x3e};  // О
        case 0x41f: return {0x7f, 0x01, 0x01, 0x01, 0x7f};  // П
        case 0x420: return {0x7f, 0x09, 0x09, 0x09, 0x06};  // Р
        case 0x421: return {0x3e, 0x41, 0x41, 0x41, 0x22};  // С
        case 0x422: return {0x01, 0x01, 0x7f, 0x01, 0x01};  // Т
        case 0x423: return {0x07, 0x48, 0x48, 0x48, 0x3f};  // У
        case 0x424: return {0x1c, 0x22, 0x7f, 0x22, 0x1c};  // Ф
        case 0x425: return {0x63, 0x14, 0x08, 0x14, 0x63};  // Х
        case 0x426: return {0x3f, 0x20, 0x20, 0x3f, 0x60};  // Ц
        case 0x427: return {0x0f, 0x08, 0x08, 0x08, 0x7f};  // Ч
        case 0x428: return {0x7f, 0x40, 0x7f, 0x40, 0x7f};  // Ш
        case 0x429: return {0x3f, 0x20, 0x3f, 0x20, 0x7f};  // Щ
        case 0x42a: return {0x01, 0x7f, 0x48, 0x48, 0x30};  // Ъ
        case 0x42b: return {0x7f, 0x48, 0x30, 0x00, 0x7f};  // Ы
        case 0x42c: return {0x7f, 0x48, 0x48, 0x48, 0x30};  // Ь
        case 0x42d: return {0x22, 0x41, 0x49, 0x49, 0x3e};  // Э
        case 0x42e: return {0x7f, 0x08, 0x3e, 0x41, 0x3e};  // Ю
        case 0x42f: return {0x46, 0x29, 0x19, 0x09, 0x7f};  // Я
        default: return {0x02, 0x01, 0x51, 0x09, 0x06};
    }
}

void set_pixel(uint16_t* pixels, int x, int y, uint16_t color) {
    if (x < 0 || x >= kUiWidth || y < 0 || y >= kUiBandRows) return;
    pixels[y * kUiWidth + x] = wire_color(color);
}

void draw_text(uint16_t* pixels, int x, int y, const char* text, uint16_t color) {
    int characters = 0;
    while (*text != '\0' && characters < kMaxGlyphsPerLine &&
           x + kGlyphWidth * kTextScale <= kUiWidth) {
        const Glyph columns = glyph(next_codepoint(text));
        for (int column = 0; column < kGlyphWidth; ++column) {
            for (int row = 0; row < kGlyphHeight; ++row) {
                if ((columns[column] & (1u << row)) == 0) continue;
                for (int dx = 0; dx < kTextScale; ++dx) {
                    for (int dy = 0; dy < kTextScale; ++dy) {
                        set_pixel(pixels, x + column * kTextScale + dx,
                                  y + row * kTextScale + dy, color);
                    }
                }
            }
        }
        x += kGlyphAdvance * kTextScale;
        ++characters;
    }
}

void clear_band(uint16_t* pixels, uint16_t background) {
    const uint16_t wire_background = wire_color(background);
    for (int i = 0; i < kUiWidth * kUiBandRows; ++i) pixels[i] = wire_background;
}

void draw_lines(uint16_t* pixels, const char* first, const char* second, uint16_t foreground) {
    draw_text(pixels, 0, 3, first, foreground);
    draw_text(pixels, 0, 22, second, foreground);
}

bool elapsed_at_least(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
    return now_ms - since_ms >= duration_ms;
}

const char* yes_no(bool value) {
    return value ? "ДА" : "НЕТ";
}

const char* sensor_quality_label(SensorQuality quality) {
    switch (quality) {
        case SensorQuality::OK: return "ОК";
        case SensorQuality::MISSING: return "НЕТ";
        case SensorQuality::HARDWARE_FAULT: return "ОБРЫВ";
        case SensorQuality::CRC_ERROR: return "CRC";
        case SensorQuality::TIMEOUT: return "ТАЙМ";
        case SensorQuality::IMPLAUSIBLE: return "НЕРЕАЛ";
        case SensorQuality::POWER_ON_DEFAULT: return "СБРОС";
    }
    return "?";
}

const char* zone_label(RoomZone zone) {
    switch (zone) {
        case RoomZone::BELOW: return "НИЖЕ";
        case RoomZone::BAND: return "ПОЛОСА";
        case RoomZone::ABOVE: return "ВЫШЕ";
        case RoomZone::INVALID: return "НЕТ";
    }
    return "НЕТ";
}

const char* stop_reason_label(StopReason reason) {
    switch (reason) {
        case StopReason::NONE: return "НЕТ";
        case StopReason::LOCAL_STOP: return "СТОП";
        case StopReason::REMOTE_STOP: return "УДАЛЕННЫЙ";
        case StopReason::THERMOSTAT: return "ТЕРМОСТАТ";
        case StopReason::FAULT: return "АВАРИЯ";
        case StopReason::RESET: return "СБРОС";
        case StopReason::BOOT: return "ЗАГРУЗКА";
    }
    return "НЕТ";
}

void format_temperature(char* output, size_t output_size, const char* label, int32_t milli_c) {
    const int64_t value = milli_c;
    const int64_t absolute = value < 0 ? -value : value;
    std::snprintf(output, output_size, "%s%s%lld.%01lldC", label, value < 0 ? "-" : "",
                  static_cast<long long>(absolute / 1000),
                  static_cast<long long>((absolute % 1000) / 100));
}

void format_sample(char* output, size_t output_size, const char* label, const SensorSample& sample) {
    if (sample.quality != SensorQuality::OK) {
        std::snprintf(output, output_size, "%s%s", label, sensor_quality_label(sample.quality));
        return;
    }
    format_temperature(output, output_size, label, sample.milli_c);
}

void format_quality_age(char* output, size_t output_size, const SensorSample& sample, uint32_t now_ms) {
    const uint32_t age_seconds = (now_ms - sample.sample_ms) / 1000u;
    std::snprintf(output, output_size, "К:%s В:%luС", sensor_quality_label(sample.quality),
                  static_cast<unsigned long>(age_seconds));
}

void format_percent(char* output, size_t output_size, uint16_t permille) {
    if (permille >= 1000) {
        std::snprintf(output, output_size, "100%%");
        return;
    }
    std::snprintf(output, output_size, "%u.%u%%", static_cast<unsigned>(permille / 10u),
                  static_cast<unsigned>(permille % 10u));
}

void format_duration(char* output, size_t output_size, uint64_t milliseconds) {
    const uint64_t minutes = milliseconds / 60'000u;
    const uint64_t hours = minutes / 60u;
    std::snprintf(output, output_size, "%lluh%llum", static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes % 60u));
}

bool status_stage_timed(const Status& status) { return status.stage_timed; }
uint32_t status_stage_remaining_ms(const Status& status) { return status.stage_remaining_ms; }
bool profile_run_low_qualified(const Config& config) { return config.run_low_qualified; }
uint32_t profile_revision(const Config& config) { return config.profile_revision; }

uint8_t normalized_selection(uint8_t selection) {
    return static_cast<uint8_t>(selection % 7u);
}

const char* setting_label(uint8_t selection) {
    switch (normalized_selection(selection)) {
        case 0: return "УСТАВКА";
        case 1: return "ГИСТЕРЕЗИС";
        case 2: return "СМЕЩЕНИЕ КОМН";
        case 3: return "ДИАГНОСТИКА";
        case 4: return "ПРОФИЛЬ";
        case 5: return "СЧЕТЧИКИ";
        case 6: return "ВЕРСИЯ";
    }
    return "УСТАВКА";
}

UiPage selected_page(uint8_t selection) {
    switch (normalized_selection(selection)) {
        case 0: return UiPage::SETPOINT;
        case 1: return UiPage::HYSTERESIS;
        case 2: return UiPage::ROOM_OFFSET;
        case 3: return UiPage::DIAGNOSTICS;
        case 4: return UiPage::PROFILE;
        case 5: return UiPage::COUNTERS;
        case 6: return UiPage::VERSION;
    }
    return UiPage::SETTINGS;
}

bool adjust_bounded(int32_t& value, int32_t step, int32_t minimum, int32_t maximum, bool up) {
    if (minimum > maximum) return false;
    const int64_t candidate = static_cast<int64_t>(value) + (up ? step : -step);
    int64_t bounded = candidate;
    if (bounded < minimum) bounded = minimum;
    if (bounded > maximum) bounded = maximum;
    const int32_t changed = static_cast<int32_t>(bounded);
    if (changed == value) return false;
    value = changed;
    return true;
}

void render_home(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    char first[96]{};
    char second[96]{};
    uint16_t background = kBlack;
    uint16_t foreground = kWhite;

    switch (band) {
        case 0:
            background = kDarkBlue;
            foreground = kCyan;
            format_sample(first, sizeof(first), "КОМН ", frame.room);
            format_temperature(second, sizeof(second), "УСТ ", frame.settings.setpoint_milli_c);
            break;
        case 1:
            format_temperature(first, sizeof(first), "ГИСТ ", frame.settings.hysteresis_milli_c);
            std::snprintf(second, sizeof(second), "ЗОНА:%s",
                          zone_label(room_zone(frame.room, frame.settings, frame.now_ms,
                                               frame.config.sensor_stale_ms)));
            break;
        case 2:
            format_sample(first, sizeof(first), "ВЫХ RAW ", frame.exhaust);
            format_sample(second, sizeof(second), "ВОЗД RAW ", frame.hot_air);
            break;
        case 3:
            foreground = frame.status.state == State::LOCKOUT ? kRed : kYellow;
            std::snprintf(first, sizeof(first), "%s", ui_state_label(frame.status.state));
            if (status_stage_timed(frame.status)) {
                std::snprintf(second, sizeof(second), "ОСТ:%luС",
                              static_cast<unsigned long>(status_stage_remaining_ms(frame.status) / 1000u));
            } else if (frame.status.state == State::WAIT_COOLDOWN) {
                std::snprintf(second, sizeof(second), "%s",
                              frame.status.outputs.fan_request ? "ЖДУ ОХЛАЖДЕНИЯ" : "ЖДУ ЗАПУСКА");
            } else if (frame.status.state == State::LOCKOUT) {
                std::snprintf(second, sizeof(second), "СБРОС НЕ В UI");
            } else {
                std::snprintf(second, sizeof(second), "БЕЗ ТАЙМЕРА");
            }
            break;
        case 4: {
            char percent[16]{};
            format_percent(percent, sizeof(percent), frame.status.outputs.speed_permille);
            foreground = kGreen;
            std::snprintf(first, sizeof(first), "ВЕНТ КОМ:%s", percent);
            std::snprintf(second, sizeof(second), "ТОПЛ:%s РАЗР:%s",
                          yes_no(frame.status.outputs.fuel_command),
                          yes_no(frame.status.outputs.fuel_permit));
            break;
        }
        case 5: {
            const bool service_gate = frame.safety_ok && frame.settings_ok && frame.storage_ok &&
                                      frame.status.commissioning_ready;
            std::snprintf(first, sizeof(first), "НАКАЛ КОМ:%s", yes_no(frame.status.outputs.glow_command));
            std::snprintf(second, sizeof(second), "СЕРВИС:%s", service_gate ? "ГОТОВ" : "ЗАКР");
            break;
        }
        case 6:
            background = frame.status.faults == FAULT_NONE ? kDarkGreen : kDarkRed;
            foreground = frame.status.faults == FAULT_NONE ? kGreen : kRed;
            std::snprintf(first, sizeof(first), "АВАРИЯ КОД:%08lX",
                          static_cast<unsigned long>(frame.status.faults));
            std::snprintf(second, sizeof(second), "%s", ui_fault_label(frame.status.faults));
            break;
        case 7:
            foreground = kOrange;
            std::snprintf(first, sizeof(first), "BENCH ONLY");
            std::snprintf(second, sizeof(second), "СЕТЬ:%s", frame.network_connected ? "ONLINE" : "НЕТ");
            break;
        default:
            return;
    }

    clear_band(pixels, background);
    draw_lines(pixels, first, second, foreground);
}

void render_settings(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    char first[96]{};
    char second[96]{};
    uint16_t background = kBlack;
    uint16_t foreground = kWhite;
    const uint8_t selection = normalized_selection(frame.selection);

    if (band == 0) {
        std::snprintf(first, sizeof(first), "НАСТРОЙКИ");
        std::snprintf(second, sizeof(second), "UP/DN ВЫБОР");
    } else {
        const uint8_t item = static_cast<uint8_t>(band - 1u);
        if (item == selection) {
            background = kDarkBlue;
            foreground = kCyan;
        }
        std::snprintf(first, sizeof(first), "%s", setting_label(item));
        switch (item) {
            case 0: format_temperature(second, sizeof(second), "", frame.settings.setpoint_milli_c); break;
            case 1: format_temperature(second, sizeof(second), "", frame.settings.hysteresis_milli_c); break;
            case 2: format_temperature(second, sizeof(second), "", frame.settings.room_offset_milli_c); break;
            case 3: std::snprintf(second, sizeof(second), "RAW DS И MAX"); break;
            case 4: std::snprintf(second, sizeof(second), "ГЕЙТ И ШИМ"); break;
            case 5: std::snprintf(second, sizeof(second), "ХОРДА:СБРОС"); break;
            case 6: std::snprintf(second, sizeof(second), "0.1.6-DRAFT"); break;
            default: break;
        }
    }
    clear_band(pixels, background);
    draw_lines(pixels, first, second, foreground);
}

void render_edit(uint16_t* pixels, uint8_t band, const UiFrame& frame, UiPage page) {
    char first[96]{};
    char second[96]{};

    const char* title = "УСТАВКА";
    int32_t value = frame.settings.setpoint_milli_c;
    int32_t minimum = frame.config.setpoint_min_milli_c;
    int32_t maximum = frame.config.setpoint_max_milli_c;
    const char* increment = "UP/DN:0.5C";
    if (page == UiPage::HYSTERESIS) {
        title = "ГИСТЕРЕЗИС";
        value = frame.settings.hysteresis_milli_c;
        minimum = kHysteresisMinMilliC;
        maximum = kHysteresisMaxMilliC;
    } else if (page == UiPage::ROOM_OFFSET) {
        title = "СМЕЩЕНИЕ КОМН";
        value = frame.settings.room_offset_milli_c;
        minimum = -kRoomOffsetLimitMilliC;
        maximum = kRoomOffsetLimitMilliC;
        increment = "UP/DN:0.1C";
    }

    switch (band) {
        case 0:
            std::snprintf(first, sizeof(first), "%s", title);
            std::snprintf(second, sizeof(second), "%s", increment);
            break;
        case 1:
            format_temperature(first, sizeof(first), "ЗНАЧ ", value);
            std::snprintf(second, sizeof(second), "ИЗМЕНЯЕТСЯ СРАЗУ");
            break;
        case 2:
            format_temperature(first, sizeof(first), "МИН ", minimum);
            format_temperature(second, sizeof(second), "МАКС ", maximum);
            break;
        case 3:
            std::snprintf(first, sizeof(first), "ХОРДА: НАЗАД");
            std::snprintf(second, sizeof(second), "СТОП: ГЛАВНАЯ");
            break;
        case 7:
            std::snprintf(first, sizeof(first), "BENCH ONLY");
            break;
        default:
            break;
    }
    clear_band(pixels, kBlack);
    draw_lines(pixels, first, second, kWhite);
}

void render_diagnostics(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    char first[96]{};
    char second[96]{};
    switch (band) {
        case 0:
            std::snprintf(first, sizeof(first), "ДИАГНОСТИКА");
            std::snprintf(second, sizeof(second), "RAW DS18B20");
            break;
        case 1:
            format_sample(first, sizeof(first), "КОМН RAW ", frame.raw_room);
            format_quality_age(second, sizeof(second), frame.raw_room, frame.now_ms);
            break;
        case 2:
            format_sample(first, sizeof(first), "ВЫХ RAW ", frame.exhaust);
            format_quality_age(second, sizeof(second), frame.exhaust, frame.now_ms);
            break;
        case 3:
            format_sample(first, sizeof(first), "ВОЗД RAW ", frame.hot_air);
            format_quality_age(second, sizeof(second), frame.hot_air, frame.now_ms);
            break;
        case 4:
            std::snprintf(first, sizeof(first), "MAX ВЫХ:%02X", static_cast<unsigned>(frame.exhaust_fault));
            std::snprintf(second, sizeof(second), "ВОЗР:%luС",
                          static_cast<unsigned long>((frame.now_ms - frame.exhaust.sample_ms) / 1000u));
            break;
        case 5:
            std::snprintf(first, sizeof(first), "MAX ВОЗД:%02X", static_cast<unsigned>(frame.hot_air_fault));
            std::snprintf(second, sizeof(second), "ВОЗР:%luС",
                          static_cast<unsigned long>((frame.now_ms - frame.hot_air.sample_ms) / 1000u));
            break;
        case 6:
            format_sample(first, sizeof(first), "КОМН КАЛ ", frame.room);
            std::snprintf(second, sizeof(second), "СТОП:%s ЗАЩ:%s", yes_no(frame.stop_active),
                          yes_no(frame.safety_ok));
            break;
        case 7:
            std::snprintf(first, sizeof(first), "СТОП: НАЗАД");
            std::snprintf(second, sizeof(second), "BENCH ONLY");
            break;
        default:
            return;
    }
    clear_band(pixels, kBlack);
    draw_lines(pixels, first, second, kWhite);
}

void render_profile(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    char first[96]{};
    char second[96]{};
    switch (band) {
        case 0:
            std::snprintf(first, sizeof(first), "ПРОФИЛЬ");
            std::snprintf(second, sizeof(second), "ГЕЙТ:%s",
                          frame.config.commissioning_accepted && frame.status.commissioning_ready ? "ОТКР" : "ЗАКР");
            break;
        case 1:
            std::snprintf(first, sizeof(first), "ТОКЕН:%08lX", static_cast<unsigned long>(frame.profile_token));
            std::snprintf(second, sizeof(second), "РЕВ:%lu",
                          static_cast<unsigned long>(profile_revision(frame.config)));
            break;
        case 2:
            std::snprintf(first, sizeof(first), "ШИМ:%luГЦ",
                          static_cast<unsigned long>(frame.config.pwm_frequency_hz));
            std::snprintf(second, sizeof(second), "НИЗ:%s",
                          profile_run_low_qualified(frame.config) ? "ГОТОВ" : "НЕТ");
            break;
        case 3: {
            char low[16]{};
            char purge[16]{};
            format_percent(low, sizeof(low), frame.config.low_pwm_permille);
            format_percent(purge, sizeof(purge), frame.config.recovery_purge_pwm_permille);
            std::snprintf(first, sizeof(first), "НИЗ КОМ:%s", low);
            std::snprintf(second, sizeof(second), "ПРОДУВ:%s", purge);
            break;
        }
        case 4:
            format_temperature(first, sizeof(first), "УСТ МИН ", frame.config.setpoint_min_milli_c);
            format_temperature(second, sizeof(second), "УСТ МАКС ", frame.config.setpoint_max_milli_c);
            break;
        case 5:
            std::snprintf(first, sizeof(first), "ПАМЯТЬ:%s", yes_no(frame.storage_ok));
            std::snprintf(second, sizeof(second), "НАСТР:%s", yes_no(frame.settings_ok));
            break;
        case 7:
            std::snprintf(first, sizeof(first), "СТОП: НАЗАД");
            std::snprintf(second, sizeof(second), "BENCH ONLY");
            break;
        default:
            break;
    }
    clear_band(pixels, kBlack);
    draw_lines(pixels, first, second, kWhite);
}

void render_counters(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    char first[96]{};
    char second[96]{};
    char duration[32]{};
    switch (band) {
        case 0:
            std::snprintf(first, sizeof(first), "СЧЕТЧИКИ");
            std::snprintf(second, sizeof(second), "ВРЕМЯ КОМАНД");
            break;
        case 1:
            std::snprintf(first, sizeof(first), "СТАРТ:%llu",
                          static_cast<unsigned long long>(frame.counters.starts));
            std::snprintf(second, sizeof(second), "СБОЙ РОЗЖ:%llu",
                          static_cast<unsigned long long>(frame.counters.failed_ignitions));
            break;
        case 2:
            format_duration(duration, sizeof(duration), frame.counters.run_ms);
            std::snprintf(first, sizeof(first), "РАБОТА КОМАНДА");
            std::snprintf(second, sizeof(second), "%s", duration);
            break;
        case 3:
            format_duration(duration, sizeof(duration), frame.counters.glow_ms);
            std::snprintf(first, sizeof(first), "НАКАЛ КОМАНДА");
            std::snprintf(second, sizeof(second), "%s", duration);
            break;
        case 4:
            std::snprintf(first, sizeof(first), "СТ:%s", ui_state_label(frame.last_event.to));
            std::snprintf(second, sizeof(second), "ПРИЧ:%s", stop_reason_label(frame.last_event.reason));
            break;
        case 5:
            std::snprintf(first, sizeof(first), "BOOT:%lu", static_cast<unsigned long>(frame.boot_id));
            std::snprintf(second, sizeof(second), "СЕАНС:%lu", static_cast<unsigned long>(frame.session_id));
            break;
        case 6:
            std::snprintf(first, sizeof(first), "ХОРДА: СБРОС");
            std::snprintf(second, sizeof(second), "СТОП: НАЗАД");
            break;
        case 7:
            std::snprintf(first, sizeof(first), "BENCH ONLY");
            break;
        default:
            break;
    }
    clear_band(pixels, kBlack);
    draw_lines(pixels, first, second, kWhite);
}

void render_reset_counters(uint16_t* pixels, uint8_t band) {
    char first[96]{};
    char second[96]{};
    uint16_t foreground = kYellow;
    if (band == 0) {
        std::snprintf(first, sizeof(first), "СБРОС СЧЕТЧИКОВ");
        std::snprintf(second, sizeof(second), "ОТПУСТИТЕ КЛАВИШИ");
    } else if (band == 1) {
        foreground = kRed;
        std::snprintf(first, sizeof(first), "НОВАЯ ХОРДА 2 С");
        std::snprintf(second, sizeof(second), "UP+DOWN УДЕРЖ.");
    } else if (band == 2) {
        std::snprintf(first, sizeof(first), "ОДИН РАЗ ПОСЛЕ");
        std::snprintf(second, sizeof(second), "СБРОС: СЧЕТЧИКИ");
    } else if (band == 7) {
        std::snprintf(first, sizeof(first), "UP ИЛИ DN: ОТМЕНА");
        std::snprintf(second, sizeof(second), "СТОП: ГЛАВНАЯ");
    }
    clear_band(pixels, kDarkRed);
    draw_lines(pixels, first, second, foreground);
}

void render_version(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    char first[96]{};
    char second[96]{};
    switch (band) {
        case 0:
            std::snprintf(first, sizeof(first), "СИРОККО UI");
            std::snprintf(second, sizeof(second), "0.1.6-DRAFT");
            break;
        case 1:
            std::snprintf(first, sizeof(first), "BENCH ONLY");
            std::snprintf(second, sizeof(second), "НЕ ДЛЯ ЭКСПЛУАТ.");
            break;
        case 2:
            std::snprintf(first, sizeof(first), "BOOT:%lu", static_cast<unsigned long>(frame.boot_id));
            std::snprintf(second, sizeof(second), "СЕАНС:%lu", static_cast<unsigned long>(frame.session_id));
            break;
        case 3:
            std::snprintf(first, sizeof(first), "СЕТЬ:%s", frame.network_configured ? "НАСТР" : "НЕТ");
            std::snprintf(second, sizeof(second), "СВЯЗЬ:%s", frame.network_connected ? "ЕСТЬ" : "НЕТ");
            break;
        case 4:
            std::snprintf(first, sizeof(first), "ОБНОВЛ:%s", yes_no(frame.update_active));
            std::snprintf(second, sizeof(second), "ХОРДА: НАЗАД");
            break;
        case 7:
            std::snprintf(first, sizeof(first), "СТОП: ГЛАВНАЯ");
            break;
        default:
            break;
    }
    clear_band(pixels, kBlack);
    draw_lines(pixels, first, second, kWhite);
}

}  // namespace

const char* ui_state_label(State state) {
    switch (state) {
        case State::BOOT_SELF_TEST: return "САМОТЕСТ";
        case State::RECOVERY_PURGE: return "ВОССТ ПРОДУВКА";
        case State::WAIT_COOLDOWN: return "ОЖИДАНИЕ";
        case State::PREPURGE: return "ПРЕДПРОДУВКА";
        case State::PREHEAT: return "НАКАЛ";
        case State::SPINUP: return "РАЗГОН";
        case State::IGNITION_TRIAL: return "РОЗЖИГ";
        case State::FLAME_CONFIRM: return "ПРОВЕРКА ПЛАМЕНИ";
        case State::RUN_HIGH: return "РАБОТА ВЫСОКАЯ";
        case State::RUN_LOW: return "РАБОТА НИЗКАЯ";
        case State::POST_PURGE: return "ПРОДУВКА";
        case State::LOCKOUT: return "БЛОКИРОВКА";
    }
    return "НЕИЗВЕСТНО";
}

const char* ui_fault_label(uint32_t faults) {
    if (faults == FAULT_NONE) return "НЕТ";
    if (faults & FAULT_CONFIG) return "КОНФИГ";
    if (faults & FAULT_SAFETY_OPEN) return "ЗАЩИТА";
    if (faults & FAULT_EXHAUST_SENSOR) return "ДАТЧИК ВЫХЛОПА";
    if (faults & FAULT_AIR_SENSOR) return "ДАТЧИК ВОЗДУХА";
    if (faults & FAULT_ROOM_SENSOR) return "ДАТЧИК КОМНАТЫ";
    if (faults & FAULT_EXHAUST_OVERTEMP) return "ПЕРЕГРЕВ ВЫХЛОПА";
    if (faults & FAULT_AIR_OVERTEMP) return "ПЕРЕГРЕВ ВОЗДУХА";
    if (faults & FAULT_IGNITION_NOT_PROVEN) return "НЕТ РОЗЖИГА";
    if (faults & FAULT_FLAME_LOST) return "ПОТЕРЯ ПЛАМЕНИ";
    if (faults & FAULT_GLOW_TIMEOUT) return "ТАЙМЕР НАКАЛА";
    if (faults & FAULT_BROWNOUT_RESET) return "СБОЙ ПИТАНИЯ";
    if (faults & FAULT_INTERNAL_INVARIANT) return "ОШИБКА ЛОГИКИ";
    if (faults & FAULT_SELF_TEST_TIMEOUT) return "ТАЙМЕР САМОТЕСТА";
    if (faults & FAULT_LOCAL_UI) return "ЛОКАЛЬНЫЙ UI";
    if (faults & FAULT_OUTPUT) return "ВЫХОДЫ";
    return "НЕИЗВЕСТНО";
}

UiAction LocalMenu::tick(const UiKeys& keys, uint32_t now_ms, OperatorSettings& settings,
                         const Config& config) {
    UiAction action{};
    const bool both_held = keys.up_held && keys.down_held;

    // The fixed private state has no explicit initialization bit. UINT32_MAX is an inactive
    // sentinel, so the all-zero constructor state can reject a chord held at the first poll.
    if (chord_since_ == 0 && last_activity_ == 0 && !chord_active_ && !chord_fired_) {
        chord_since_ = std::numeric_limits<uint32_t>::max();
        last_activity_ = now_ms;
        if (both_held) {
            chord_active_ = true;
            chord_fired_ = true;
            chord_since_ = now_ms;
        }
    }

    if (keys.stop) {
        page_ = UiPage::HOME;
        selection_ = 0;
        last_activity_ = now_ms;
        chord_since_ = std::numeric_limits<uint32_t>::max();
        chord_active_ = keys.up_held || keys.down_held;
        chord_fired_ = chord_active_;
        return action;
    }

    if (chord_active_) {
        if (!keys.up_held && !keys.down_held) {
            chord_active_ = false;
            chord_fired_ = false;
            chord_since_ = std::numeric_limits<uint32_t>::max();
            last_activity_ = now_ms;
            return action;
        }
        if (page_ != UiPage::HOME && elapsed_at_least(now_ms, last_activity_, kMenuTimeoutMs)) {
            page_ = UiPage::HOME;
            selection_ = 0;
            last_activity_ = now_ms;
            // The held chord remains consumed until both keys are released.
            chord_fired_ = true;
            return action;
        }
        if (!both_held || chord_fired_) return action;

        const uint32_t minimum = page_ == UiPage::RESET_COUNTERS ? kResetCountersChordMs : kMenuChordMs;
        if (!elapsed_at_least(now_ms, chord_since_, minimum)) return action;

        chord_fired_ = true;
        last_activity_ = now_ms;
        switch (page_) {
            case UiPage::HOME:
                page_ = UiPage::SETTINGS;
                selection_ = 0;
                break;
            case UiPage::SETTINGS:
                page_ = selected_page(selection_);
                break;
            case UiPage::SETPOINT:
            case UiPage::HYSTERESIS:
            case UiPage::ROOM_OFFSET:
            case UiPage::DIAGNOSTICS:
            case UiPage::PROFILE:
            case UiPage::VERSION:
                page_ = UiPage::SETTINGS;
                break;
            case UiPage::COUNTERS:
                page_ = UiPage::RESET_COUNTERS;
                break;
            case UiPage::RESET_COUNTERS:
                action.reset_counters = true;
                page_ = UiPage::COUNTERS;
                break;
        }
        return action;
    }

    if (both_held) {
        chord_active_ = true;
        chord_fired_ = false;
        chord_since_ = now_ms;
        last_activity_ = now_ms;
        return action;
    }

    if (page_ != UiPage::HOME && elapsed_at_least(now_ms, last_activity_, kMenuTimeoutMs)) {
        page_ = UiPage::HOME;
        selection_ = 0;
        last_activity_ = now_ms;
        return action;
    }

    if (keys.up_press == keys.down_press) return action;
    const bool up = keys.up_press;
    last_activity_ = now_ms;

    switch (page_) {
        case UiPage::HOME:
            action.settings_changed = adjust_bounded(settings.setpoint_milli_c, 500,
                                                     config.setpoint_min_milli_c,
                                                     config.setpoint_max_milli_c, up);
            break;
        case UiPage::SETTINGS:
            selection_ = up ? static_cast<uint8_t>((normalized_selection(selection_) + 6u) % 7u)
                            : static_cast<uint8_t>((normalized_selection(selection_) + 1u) % 7u);
            break;
        case UiPage::SETPOINT:
            action.settings_changed = adjust_bounded(settings.setpoint_milli_c, 500,
                                                     config.setpoint_min_milli_c,
                                                     config.setpoint_max_milli_c, up);
            break;
        case UiPage::HYSTERESIS:
            action.settings_changed = adjust_bounded(settings.hysteresis_milli_c, 500,
                                                     kHysteresisMinMilliC,
                                                     kHysteresisMaxMilliC, up);
            break;
        case UiPage::ROOM_OFFSET:
            action.settings_changed = adjust_bounded(settings.room_offset_milli_c, 100,
                                                     -kRoomOffsetLimitMilliC,
                                                     kRoomOffsetLimitMilliC, up);
            break;
        case UiPage::RESET_COUNTERS:
            page_ = UiPage::COUNTERS;
            break;
        case UiPage::DIAGNOSTICS:
        case UiPage::PROFILE:
        case UiPage::COUNTERS:
        case UiPage::VERSION:
            page_ = UiPage::SETTINGS;
            break;
    }
    return action;
}

void render_ui_band(uint16_t* pixels, uint8_t band, const UiFrame& frame) {
    if (!pixels || band >= kUiHeight / kUiBandRows) return;
    switch (frame.page) {
        case UiPage::HOME: render_home(pixels, band, frame); break;
        case UiPage::SETTINGS: render_settings(pixels, band, frame); break;
        case UiPage::SETPOINT:
        case UiPage::HYSTERESIS:
        case UiPage::ROOM_OFFSET: render_edit(pixels, band, frame, frame.page); break;
        case UiPage::DIAGNOSTICS: render_diagnostics(pixels, band, frame); break;
        case UiPage::PROFILE: render_profile(pixels, band, frame); break;
        case UiPage::COUNTERS: render_counters(pixels, band, frame); break;
        case UiPage::RESET_COUNTERS: render_reset_counters(pixels, band); break;
        case UiPage::VERSION: render_version(pixels, band, frame); break;
    }
}

}  // namespace sirokko
