#include "panel.hpp"

#include "panel_font.hpp"
#include "panel_ui.hpp"
#include "runtime_api.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "board.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

#if defined(CONFIG_SIROKKO_PANEL_WIDTH)
constexpr int kNativeWidth = CONFIG_SIROKKO_PANEL_WIDTH;
#else
constexpr int kNativeWidth = 240;
#endif
#if defined(CONFIG_SIROKKO_PANEL_HEIGHT)
constexpr int kNativeHeight = CONFIG_SIROKKO_PANEL_HEIGHT;
#else
constexpr int kNativeHeight = 320;
#endif
#if defined(CONFIG_SIROKKO_PANEL_ROTATION)
constexpr int kRotation = CONFIG_SIROKKO_PANEL_ROTATION;
#else
constexpr int kRotation = 0;
#endif
#if defined(CONFIG_SIROKKO_PANEL_X_GAP)
constexpr int kXGap = CONFIG_SIROKKO_PANEL_X_GAP;
#else
constexpr int kXGap = 0;
#endif
#if defined(CONFIG_SIROKKO_PANEL_Y_GAP)
constexpr int kYGap = CONFIG_SIROKKO_PANEL_Y_GAP;
#else
constexpr int kYGap = 0;
#endif
#if defined(CONFIG_SIROKKO_PANEL_INVERT)
constexpr bool kInvertColors = CONFIG_SIROKKO_PANEL_INVERT;
#elif defined(CONFIG_SIROKKO_PANEL_WIDTH)
constexpr bool kInvertColors = false;
#else
constexpr bool kInvertColors = true;
#endif

constexpr char kTag[] = "panel";
constexpr uint32_t kPanelSpiHz = 20U * 1000U * 1000U;
constexpr int kTextRows = panel_font::kGlyphHeight * 3;
constexpr uint32_t kRefreshMs = 100;
constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kHeader = 0x0015;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kDimText = 0xBDF7;
constexpr uint16_t kAccent = 0x07FF;
constexpr uint16_t kSelected = 0x5A60;
constexpr uint16_t kWarning = 0xFD20;
constexpr uint16_t kError = 0xF800;

struct Display {
    esp_lcd_panel_io_handle_t io{};
    esp_lcd_panel_handle_t panel{};
    SemaphoreHandle_t color_done{};
    uint16_t* buffer{};
    int width{};
    int height{};
    bool spi_initialized{};
};

Display g_display;
panel_ui::Model g_model;
panel_ui::EncoderInput g_encoder;
bool g_started{};

uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

bool ok(esp_err_t result) {
    return result == ESP_OK;
}

bool color_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* context) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (context) xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context), &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

bool draw_bitmap_and_wait(int x_start, int y_start, int x_end, int y_end, const void* pixels) {
    if (!g_display.panel || !g_display.color_done) return false;
    if (!ok(esp_lcd_panel_draw_bitmap(g_display.panel, x_start, y_start, x_end, y_end, pixels))) return false;
    // The esp_lcd SPI color path is asynchronous. Do not reuse the single DMA buffer before this callback.
    return xSemaphoreTake(g_display.color_done, portMAX_DELAY) == pdTRUE;
}

uint64_t mix(uint64_t hash, uint64_t value) {
    return (hash ^ value) * 1099511628211ULL;
}

uint64_t settings_hash(const sirokko::Settings& settings) {
    uint64_t hash = 1469598103934665603ULL;
    hash = mix(hash, static_cast<uint64_t>(static_cast<uint32_t>(settings.room_setpoint_milli_c)));
    hash = mix(hash, static_cast<uint64_t>(static_cast<uint32_t>(settings.room_hysteresis_milli_c)));
    hash = mix(hash, settings.duration_ms);
    hash = mix(hash, static_cast<uint64_t>(settings.mode));
    hash = mix(hash, settings.manual_percent);
    return hash;
}

uint64_t command_hash(panel_ui::CommandKind kind, uint64_t delay_ms) {
    uint64_t hash = 1469598103934665603ULL;
    hash = mix(hash, static_cast<uint64_t>(kind));
    return mix(hash, delay_ms);
}

void display_release() {
    if (g_display.panel) esp_lcd_panel_del(g_display.panel);
    if (g_display.io) esp_lcd_panel_io_del(g_display.io);
    if (g_display.color_done) vSemaphoreDelete(g_display.color_done);
    if (g_display.buffer) heap_caps_free(g_display.buffer);
    if (g_display.spi_initialized) spi_bus_free(SPI3_HOST);
    g_display = {};
}

bool rotation_flags(bool* swap_xy, bool* mirror_x, bool* mirror_y) {
    if (!swap_xy || !mirror_x || !mirror_y) return false;
    switch (kRotation) {
        case 0:
            *swap_xy = false;
            *mirror_x = false;
            *mirror_y = false;
            return true;
        case 90:
            *swap_xy = true;
            *mirror_x = true;
            *mirror_y = false;
            return true;
        case 180:
            *swap_xy = false;
            *mirror_x = true;
            *mirror_y = true;
            return true;
        case 270:
            *swap_xy = true;
            *mirror_x = false;
            *mirror_y = true;
            return true;
        default:
            return false;
    }
}

bool display_begin() {
    if (kNativeWidth <= 0 || kNativeHeight <= 0) return false;

    bool swap_xy{};
    bool mirror_x{};
    bool mirror_y{};
    if (!rotation_flags(&swap_xy, &mirror_x, &mirror_y)) return false;

    spi_bus_config_t bus{};
    bus.sclk_io_num = board::kTftSck;
    bus.mosi_io_num = board::kTftMosi;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = std::max(kNativeWidth, kNativeHeight) * kTextRows * static_cast<int>(sizeof(uint16_t)) + 8;
    if (!ok(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO))) return false;
    g_display.spi_initialized = true;
    g_display.color_done = xSemaphoreCreateBinary();
    if (!g_display.color_done) {
        display_release();
        return false;
    }

    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = board::kTftCs;
    io.dc_gpio_num = board::kTftDc;
    io.spi_mode = 0;
    io.pclk_hz = kPanelSpiHz;
    io.trans_queue_depth = 1;
    io.on_color_trans_done = color_transfer_done;
    io.user_ctx = g_display.color_done;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    if (!ok(esp_lcd_new_panel_io_spi(reinterpret_cast<esp_lcd_spi_bus_handle_t>(static_cast<intptr_t>(SPI3_HOST)), &io, &g_display.io))) {
        display_release();
        return false;
    }

    esp_lcd_panel_dev_config_t panel{};
    panel.reset_gpio_num = board::kTftReset;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel.bits_per_pixel = 16;
    if (!ok(esp_lcd_new_panel_st7789(g_display.io, &panel, &g_display.panel)) ||
        !ok(esp_lcd_panel_reset(g_display.panel)) || !ok(esp_lcd_panel_init(g_display.panel)) ||
        !ok(esp_lcd_panel_invert_color(g_display.panel, kInvertColors)) ||
        !ok(esp_lcd_panel_swap_xy(g_display.panel, swap_xy)) ||
        !ok(esp_lcd_panel_mirror(g_display.panel, mirror_x, mirror_y)) ||
        !ok(esp_lcd_panel_set_gap(g_display.panel, kXGap, kYGap)) ||
        !ok(esp_lcd_panel_disp_on_off(g_display.panel, true))) {
        display_release();
        return false;
    }

    g_display.width = swap_xy ? kNativeHeight : kNativeWidth;
    g_display.height = swap_xy ? kNativeWidth : kNativeHeight;
    const size_t pixels = static_cast<size_t>(g_display.width) * kTextRows;
    g_display.buffer = static_cast<uint16_t*>(heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!g_display.buffer) {
        display_release();
        return false;
    }
    return true;
}

bool inputs_begin() {
    gpio_config_t input{};
    input.pin_bit_mask = (1ULL << board::kEncoderA) | (1ULL << board::kEncoderB) |
                         (1ULL << board::kEncoderPush);
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    return ok(gpio_config(&input));
}

void draw_solid(int x, int y, int width, int height, uint16_t color) {
    if (!g_display.panel || !g_display.buffer || width <= 0 || height <= 0) return;
    x = std::max(0, x);
    y = std::max(0, y);
    width = std::min(width, g_display.width - x);
    height = std::min(height, g_display.height - y);
    for (int offset = 0; offset < height; offset += kTextRows) {
        const int rows = std::min(kTextRows, height - offset);
        std::fill_n(g_display.buffer, width * rows, color);
        if (!draw_bitmap_and_wait(x, y + offset, x + width, y + offset + rows, g_display.buffer)) return;
    }
}

void pixel_block(int x, int y, int scale, uint16_t color) {
    for (int row = 0; row < scale; ++row) {
        const int py = y + row;
        if (py < 0 || py >= kTextRows) continue;
        for (int column = 0; column < scale; ++column) {
            const int px = x + column;
            if (px >= 0 && px < g_display.width) g_display.buffer[py * g_display.width + px] = color;
        }
    }
}

void draw_text_line(int x, int y, const char* text, uint16_t foreground, uint16_t background, int scale) {
    if (!g_display.panel || !g_display.buffer || !text || scale <= 0 || y >= g_display.height) return;
    const int line_height = std::min(panel_font::kGlyphHeight * scale, g_display.height - y);
    if (line_height <= 0 || line_height > kTextRows) return;
    std::fill_n(g_display.buffer, g_display.width * line_height, background);

    int cursor_x = x;
    for (const char* cursor = text; *cursor && cursor_x < g_display.width; ) {
        uint32_t codepoint{};
        cursor = panel_font::next_utf8(cursor, &codepoint);
        const panel_font::Glyph& glyph = panel_font::glyph(codepoint);
        for (int column = 0; column < panel_font::kGlyphWidth; ++column) {
            for (int row = 0; row < panel_font::kGlyphHeight; ++row) {
                if ((glyph.columns[column] & (1U << row)) != 0U)
                    pixel_block(cursor_x + column * scale, row * scale, scale, foreground);
            }
        }
        cursor_x += panel_font::kGlyphAdvance * scale;
    }
    draw_bitmap_and_wait(0, y, g_display.width, y + line_height, g_display.buffer);
}

void draw_wrapped_ascii(int x, int y, const char* text, uint16_t foreground, uint16_t background, int scale) {
    if (!text || scale <= 0) return;
    const int per_line = std::max(1, (g_display.width - x) / (panel_font::kGlyphAdvance * scale));
    const int line_height = panel_font::kGlyphHeight * scale;
    const char* source = text;
    while (*source && y < g_display.height) {
        char line[48]{};
        int copied{};
        while (source[copied] && copied < per_line && copied + 1 < static_cast<int>(sizeof(line))) {
            line[copied] = source[copied];
            ++copied;
        }
        draw_text_line(x, y, line, foreground, background, scale);
        source += copied;
        y += line_height;
    }
}

const char* state_label(sirokko::State state) {
    switch (state) {
        case sirokko::State::RECOVERY_PURGE: return "ОХЛАЖДЕНИЕ";
        case sirokko::State::IDLE: return "ОЖИДАНИЕ";
        case sirokko::State::PREPURGE: return "ПРОДУВКА";
        case sirokko::State::PREHEAT: return "ПРЕДНАГРЕВ";
        case sirokko::State::IGNITION: return "РОЗЖИГ";
        case sirokko::State::VERIFY: return "ПРОВЕРКА";
        case sirokko::State::RUN: return "РАБОТА";
        case sirokko::State::STOP_PURGE: return "ОСТАНОВ";
        case sirokko::State::FAULT_PURGE: return "АВАРИЙНАЯ ПРОДУВКА";
        case sirokko::State::FAULT: return "АВАРИЯ";
        case sirokko::State::MAINTENANCE: return "ОБСЛУЖИВАНИЕ";
    }
    return "";
}

void format_temperature(char* buffer, size_t capacity, int32_t milli_c) {
    const int32_t whole = milli_c / 1000;
    const int32_t tenth = milli_c >= 0 ? (milli_c % 1000) / 100 : ((-milli_c) % 1000) / 100;
    std::snprintf(buffer, capacity, "%" PRId32 ".%" PRId32 " C", whole, tenth);
}

void format_edit_value(const panel_ui::Screen& screen, char* buffer, size_t capacity) {
    switch (screen.edit_field) {
        case panel_ui::EditField::MODE:
            std::snprintf(buffer, capacity, "%s", screen.edit_value == 0 ? "ТЕМПЕРАТУРА" : "РУЧНОЙ");
            break;
        case panel_ui::EditField::MANUAL_POWER:
            std::snprintf(buffer, capacity, "%" PRId32 " %%", screen.edit_value);
            break;
        case panel_ui::EditField::TEMPERATURE:
            format_temperature(buffer, capacity, screen.edit_value);
            break;
        case panel_ui::EditField::SHUTDOWN_TIMER:
        case panel_ui::EditField::START_DURATION:
        case panel_ui::EditField::DELAYED_START:
            std::snprintf(buffer, capacity, "%" PRId32 " МИН", screen.edit_value);
            break;
        case panel_ui::EditField::NONE:
            std::snprintf(buffer, capacity, "-");
            break;
    }
}

void draw_header(const panel_ui::View& view) {
    draw_text_line(4, 0, "СИРОККО", kAccent, kHeader, 2);
    draw_text_line(4, 15, state_label(view.control.state), kDimText, kBackground, 2);
}

void draw_home(const panel_ui::View& view) {
    draw_header(view);
    char value[64]{};
    if (view.settings.mode == sirokko::Mode::MANUAL) {
        draw_text_line(4, 48, "РУЧНАЯ МОЩНОСТЬ", kText, kBackground, 2);
        std::snprintf(value, sizeof(value), "%u %%", static_cast<unsigned>(view.settings.manual_percent));
    } else {
        draw_text_line(4, 48, "УСТАВКА", kText, kBackground, 2);
        format_temperature(value, sizeof(value), view.settings.room_setpoint_milli_c);
    }
    draw_text_line(4, 72, value, kAccent, kBackground, 3);
    draw_text_line(4, 112, "ПОВОРОТ: ИЗМЕНИТЬ", kDimText, kBackground, 2);
    draw_text_line(4, 130, "КЛИК: МЕНЮ", kDimText, kBackground, 2);
    draw_text_line(4, 148, "KEY0: ПУСК / СТОП", kWarning, kBackground, 2);

    if (view.control.calibration_active) {
        draw_text_line(4, 184, "КАЛИБРОВКА", kWarning, kBackground, 2);
        draw_text_line(4, 202, view.control.calibration_phase, kText, kBackground, 2);
    } else if (view.control.delayed_armed) {
        std::snprintf(value, sizeof(value), "ПУСК: %llu МИН",
                      static_cast<unsigned long long>((view.control.delayed_remaining_ms + 59999U) / 60000U));
        draw_text_line(4, 184, value, kText, kBackground, 2);
    } else if (view.control.duration_active) {
        std::snprintf(value, sizeof(value), "СТОП: %llu МИН",
                      static_cast<unsigned long long>((view.control.duration_remaining_ms + 59999U) / 60000U));
        draw_text_line(4, 184, value, kText, kBackground, 2);
    }
}

void draw_menu(const panel_ui::Screen& screen, const panel_ui::View& view) {
    draw_header(view);
    draw_text_line(4, 32, "МЕНЮ", kText, kBackground, 2);
    int y = 50;
    for (int index = 0; index < static_cast<int>(panel_ui::MenuItem::COUNT); ++index) {
        const auto item = static_cast<panel_ui::MenuItem>(index);
        const bool selected = item == screen.menu_item;
        const char* label = item == panel_ui::MenuItem::DELAYED_START && view.control.delayed_armed
            ? "ОТМЕНА ПУСКА" : panel_ui::menu_label(item);
        draw_text_line(4, y, label, selected ? kText : kDimText,
                       selected ? kSelected : kBackground, 2);
        y += 16;
    }
    draw_text_line(4, 240, "КЛИК: ОТКРЫТЬ", kDimText, kBackground, 2);
    draw_text_line(4, 258, "ДВОЙНОЙ: НАЗАД", kDimText, kBackground, 2);
}

void draw_edit(const panel_ui::Screen& screen, const panel_ui::View& view) {
    draw_header(view);
    draw_text_line(4, 40, "РЕДАКТИРОВАНИЕ", kText, kBackground, 2);
    draw_text_line(4, 62, panel_ui::edit_label(screen.edit_field), kDimText, kBackground, 2);
    char value[32]{};
    format_edit_value(screen, value, sizeof(value));
    draw_text_line(4, 84, value, kAccent, kBackground, 3);
    draw_text_line(4, 132, "КЛИК: СОХРАНИТЬ", kDimText, kBackground, 2);
    draw_text_line(4, 150, "ДВОЙНОЙ: ОТМЕНА", kDimText, kBackground, 2);
}

void draw_diagnostics(const panel_ui::View& view) {
    draw_header(view);
    draw_text_line(4, 35, "ДИАГНОСТИКА", kText, kBackground, 2);
    char line[48]{};
    std::snprintf(line, sizeof(line), "ОШИБКИ: %08" PRIX32, view.control.faults);
    draw_text_line(4, 62, line, kDimText, kBackground, 2);
    std::snprintf(line, sizeof(line), "КОМАНДА: %.0f %%", view.control.command_percent);
    draw_text_line(4, 78, line, kDimText, kBackground, 2);
    std::snprintf(line, sizeof(line), "КОД: %u..%u %s", static_cast<unsigned>(view.control.minimum_speed_code),
                  static_cast<unsigned>(view.control.maximum_speed_code),
                  view.control.range_calibrated ? "ОК" : "НЕТ");
    draw_text_line(4, 94, line, kDimText, kBackground, 2);
    std::snprintf(line, sizeof(line), "ТОЧКИ: %u", static_cast<unsigned>(view.control.calibration_points));
    draw_text_line(4, 110, line, kDimText, kBackground, 2);
    std::snprintf(line, sizeof(line), "КАЛ: %s", view.control.calibration_phase);
    draw_text_line(4, 126, line, kDimText, kBackground, 2);
    std::snprintf(line, sizeof(line), "ПРИЧИНА: %s", view.control.calibration_reason);
    draw_text_line(4, 142, line, kDimText, kBackground, 2);
    std::snprintf(line, sizeof(line), "ЦЕЛЬ: %u %llu С", static_cast<unsigned>(view.control.calibration_target_code),
                  static_cast<unsigned long long>(view.control.calibration_elapsed_ms / 1000U));
    draw_text_line(4, 158, line, kDimText, kBackground, 2);
    draw_text_line(4, 174, view.control.room_excess ? "КОМНАТА: ВЫШЕ" : "КОМНАТА: НОРМА",
                   view.control.room_excess ? kWarning : kDimText, kBackground, 2);
    draw_text_line(4, 202, "ДВОЙНОЙ: НАЗАД", kDimText, kBackground, 2);
}

void draw_wifi(const panel_ui::View& view) {
    draw_header(view);
    draw_text_line(4, 35, "WI-FI", kText, kBackground, 2);
    const panel_ui::LocalCredentials& credentials = g_model.credentials();
    if (!credentials.available) {
        draw_text_line(4, 65, "НЕТ ДАННЫХ", kError, kBackground, 2);
    } else {
        draw_text_line(4, 62, "СЕТЬ:", kDimText, kBackground, 2);
        draw_wrapped_ascii(4, 80, credentials.ssid.data(), kText, kBackground, 2);
        draw_text_line(4, 116, "ПАРОЛЬ:", kDimText, kBackground, 2);
        draw_wrapped_ascii(4, 134, credentials.password.data(), kText, kBackground, 2);
        draw_text_line(4, 220, "ТОЛЬКО НА ПАНЕЛИ", kWarning, kBackground, 2);
    }
    draw_text_line(4, 250, "ДВОЙНОЙ: НАЗАД", kDimText, kBackground, 2);
}

void draw_ota(const panel_ui::View& view) {
    draw_header(view);
    draw_text_line(4, 35, "OTA В БРАУЗЕРЕ", kText, kBackground, 2);
    draw_text_line(4, 65, "АДРЕС: 192.168.4.1", kDimText, kBackground, 2);
    draw_text_line(4, 83, "ВЫБЕРИТЕ OTA", kDimText, kBackground, 2);
    draw_text_line(4, 101, "ТОЛЬКО ОСТАНОВ", kWarning, kBackground, 2);
    draw_text_line(4, 132, "ДВОЙНОЙ: НАЗАД", kDimText, kBackground, 2);
}

void render() {
    if (!g_display.panel) return;
    const panel_ui::Screen screen = g_model.screen();
    const panel_ui::View& view = g_model.view();
    draw_solid(0, 0, g_display.width, g_display.height, kBackground);
    switch (screen.page) {
        case panel_ui::Page::HOME: draw_home(view); break;
        case panel_ui::Page::MENU: draw_menu(screen, view); break;
        case panel_ui::Page::EDIT: draw_edit(screen, view); break;
        case panel_ui::Page::DIAGNOSTICS: draw_diagnostics(view); break;
        case panel_ui::Page::WIFI: draw_wifi(view); break;
        case panel_ui::Page::OTA: draw_ota(view); break;
    }
    if (screen.notice != panel_ui::Notice::NONE) {
        const uint16_t color = screen.notice == panel_ui::Notice::QUEUED ? kAccent : kError;
        draw_text_line(4, std::max(0, g_display.height - 16), panel_ui::notice_label(screen.notice), color,
                       kBackground, 2);
    }
}

bool update_view() {
    RuntimeView runtime{};
    if (!runtime_view(&runtime)) {
        g_model.set_view_unavailable();
        return false;
    }
    g_model.update({runtime.control, runtime.settings, runtime.now_ms, runtime.storage_ok});
    return true;
}

bool submit_action(sirokko::Action action, uint64_t params_hash, uint64_t delay_ms,
                   const sirokko::Settings* settings) {
    sirokko::Grant grant{};
    return runtime_issue(action, params_hash, &grant) && runtime_submit(grant, action, delay_ms, settings);
}

bool execute_command(const panel_ui::Command& command) {
    switch (command.kind) {
        case panel_ui::CommandKind::SETTINGS:
            return submit_action(sirokko::Action::SETTINGS, settings_hash(command.settings), 0, &command.settings);
        case panel_ui::CommandKind::TIMER:
            return submit_action(sirokko::Action::TIMER, command_hash(command.kind, command.delay_ms),
                                 command.delay_ms, nullptr);
        case panel_ui::CommandKind::ARM_DELAYED:
            return submit_action(sirokko::Action::ARM, command_hash(command.kind, command.delay_ms),
                                 command.delay_ms, nullptr);
        case panel_ui::CommandKind::CANCEL_ARM:
            return submit_action(sirokko::Action::CANCEL_ARM, command_hash(command.kind, 0), 0, nullptr);
        case panel_ui::CommandKind::CALIBRATE:
            return submit_action(sirokko::Action::CALIBRATE, command_hash(command.kind, 0), 0, nullptr);
        case panel_ui::CommandKind::REQUEST_NETWORK_CREDENTIALS: {
            char ssid[33]{};
            char password[65]{};
            const bool success = runtime_network_credentials(ssid, sizeof(ssid), password, sizeof(password));
            if (success) g_model.set_credentials(ssid, password);
            std::memset(ssid, 0, sizeof(ssid));
            std::memset(password, 0, sizeof(password));
            return success;
        }
        case panel_ui::CommandKind::NONE:
            return false;
    }
    return false;
}

void panel_task(void*) {
    const auto sample_pressed = [](gpio_num_t pin) { return gpio_get_level(pin) == 0; };
    g_encoder.reset(gpio_get_level(board::kEncoderA) != 0, gpio_get_level(board::kEncoderB) != 0,
                    sample_pressed(board::kEncoderPush), now_ms());
    uint64_t last_refresh{};

    while (true) {
        const uint64_t now = now_ms();
        const panel_ui::InputEvent event = g_encoder.poll(gpio_get_level(board::kEncoderA) != 0,
                                                           gpio_get_level(board::kEncoderB) != 0,
                                                           sample_pressed(board::kEncoderPush), now);
        if (event.turns) g_model.rotate(event.turns);
        if (event.click) g_model.click(now);
        g_model.tick(now);

        panel_ui::Command command{};
        if (g_model.take_command(&command)) {
            const bool accepted = execute_command(command);
            g_model.command_result(accepted);
            if (!accepted && command.kind == panel_ui::CommandKind::REQUEST_NETWORK_CREDENTIALS)
                g_model.credentials_unavailable();
        }

        if (now - last_refresh >= kRefreshMs) {
            update_view();
            render();
            last_refresh = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

bool panel_begin() {
    if (g_started) return true;
    if (!inputs_begin() || !display_begin()) {
        ESP_LOGE(kTag, "local panel initialization failed");
        display_release();
        return false;
    }

    RuntimeView runtime{};
    if (runtime_view(&runtime)) {
        g_model.reset({runtime.control, runtime.settings, runtime.now_ms, runtime.storage_ok});
    } else {
        g_model.reset({});
        g_model.set_view_unavailable();
    }
    render();
    if (xTaskCreate(panel_task, "panel", 6144, nullptr, 3, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "local panel task creation failed");
        display_release();
        return false;
    }
    g_started = true;
    return true;
}
