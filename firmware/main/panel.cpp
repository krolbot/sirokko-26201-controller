#include "panel.hpp"
#include <atomic>
#include "panel_ui.hpp"
#include "panel_render.hpp"
#include "panel_damage.hpp"
#include "runtime_api.hpp"
#include "command_policy.hpp"

#include <algorithm>
#include <cstdint>
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
constexpr int kNativeWidth = CONFIG_SIROKKO_PANEL_WIDTH;
constexpr int kNativeHeight = CONFIG_SIROKKO_PANEL_HEIGHT;
constexpr int kRotation = CONFIG_SIROKKO_PANEL_ROTATION;
constexpr int kXGap = CONFIG_SIROKKO_PANEL_X_GAP;
constexpr int kYGap = CONFIG_SIROKKO_PANEL_Y_GAP;
#ifdef CONFIG_SIROKKO_PANEL_INVERT
constexpr bool kInvertColors = true;
#else
constexpr bool kInvertColors = false;
#endif
constexpr char kTag[] = "panel";
constexpr uint32_t kPanelSpiHz = 20U * 1000U * 1000U;
constexpr uint32_t kViewPeriodMs = 20;
constexpr uint32_t kFramePeriodMs = 40; // scheduling target, not a measured physical FPS guarantee
constexpr uint32_t kTransferTimeoutMs = 250;

struct Display {
    esp_lcd_panel_io_handle_t io{};
    esp_lcd_panel_handle_t panel{};
    SemaphoreHandle_t color_done{};
    uint16_t *buffer{};
    uint16_t *scratch{};
    bool spi_initialized{}, failed{}, in_flight{}, frame_active{};
    int next_y{};
    uint64_t transfer_started{}, frame_started{}, next_frame{};
    panel_render::DamageTracker damage{};
    panel_render::DamageTracker::Stripe pending{};
};
Display g_display;
panel_ui::Model g_model;
panel_ui::EncoderInput g_encoder;
panel_render::Renderer g_renderer;
bool g_started{};
std::atomic<bool> g_healthy{};

uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}
bool ok(esp_err_t result) {
    return result == ESP_OK;
}
bool color_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *context) {
    BaseType_t wake = pdFALSE;
    if (context)
        xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context), &wake);
    return wake == pdTRUE;
}
uint64_t mix(uint64_t hash, uint64_t value) {
    return (hash ^ value) * 1099511628211ULL;
}
uint64_t settings_hash(const sirokko::Settings &settings) {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, static_cast<uint32_t>(settings.room_setpoint_milli_c));
    h = mix(h, settings.duration_ms);
    h = mix(h, static_cast<uint64_t>(settings.mode));
    return mix(h, settings.manual_percent);
}
uint64_t command_hash(panel_ui::CommandKind kind, uint64_t delay_ms) {
    return mix(mix(1469598103934665603ULL, static_cast<uint64_t>(kind)), delay_ms);
}
void display_release() {
    // Initialization-only cleanup: a live DMA transfer must never lose its buffer/semaphore.
    if (g_display.in_flight)
        return;
    if (g_display.panel)
        esp_lcd_panel_del(g_display.panel);
    if (g_display.io)
        esp_lcd_panel_io_del(g_display.io);
    if (g_display.color_done)
        vSemaphoreDelete(g_display.color_done);
    if (g_display.buffer)
        heap_caps_free(g_display.buffer);
    if (g_display.scratch)
        heap_caps_free(g_display.scratch);
    if (g_display.spi_initialized)
        spi_bus_free(SPI3_HOST);
    g_display = {};
}
bool rotation_flags(bool *swap_xy, bool *mirror_x, bool *mirror_y) {
    if (!swap_xy || !mirror_x || !mirror_y)
        return false;
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
    bool swap_xy{}, mirror_x{}, mirror_y{};
    if (!rotation_flags(&swap_xy, &mirror_x, &mirror_y))
        return false;
    const int width = swap_xy ? kNativeHeight : kNativeWidth;
    const int height = swap_xy ? kNativeWidth : kNativeHeight;
    if (width != panel_render::kWidth || height != panel_render::kHeight) {
        ESP_LOGE(kTag, "approved UI requires a logical 320x240 panel");
        return false;
    }
    spi_bus_config_t bus{};
    bus.sclk_io_num = board::kTftSck;
    bus.mosi_io_num = board::kTftMosi;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz =
        panel_render::kWidth * panel_render::kStripeRows * static_cast<int>(sizeof(uint16_t)) + 8;
    if (!ok(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO)))
        return false;
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
    if (!ok(esp_lcd_new_panel_io_spi(
            reinterpret_cast<esp_lcd_spi_bus_handle_t>(static_cast<intptr_t>(SPI3_HOST)), &io,
            &g_display.io))) {
        display_release();
        return false;
    }
    esp_lcd_panel_dev_config_t panel{};
    panel.reset_gpio_num = board::kTftReset;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel.bits_per_pixel = 16;
    panel.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
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
    const size_t bytes = panel_render::kWidth * panel_render::kStripeRows * sizeof(uint16_t);
    g_display.buffer = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    g_display.scratch =
        static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!g_display.buffer || !g_display.scratch) {
        display_release();
        return false;
    }
    g_display.damage.invalidate();
    g_renderer.reset();
    return true;
}
bool inputs_begin() {
    gpio_config_t input{};
    input.pin_bit_mask =
        (1ULL << board::kEncoderA) | (1ULL << board::kEncoderB) | (1ULL << board::kEncoderPush);
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    return ok(gpio_config(&input));
}
panel_ui::View panel_view(const RuntimeView &runtime) {
    panel_ui::View view{};
    view.control = runtime.control;
    view.control.start_block = command_policy::block(runtime, sirokko::Action::START);
    view.settings = runtime.settings;
    view.now_ms = runtime.now_ms;
    view.storage_ok = runtime.storage_ok;
    view.sensors = runtime.sensors;
    view.envelope = runtime.envelope;
    return view;
}
bool update_view() {
    RuntimeView runtime{};
    if (!runtime_view(&runtime)) {
        g_model.set_view_unavailable();
        return false;
    }
    g_model.update(panel_view(runtime));
    return true;
}
bool submit_action(sirokko::Action action, uint64_t params_hash, uint64_t delay_ms,
                   const sirokko::Settings *settings, uint16_t speed_code = 0) {
    sirokko::Grant grant{};
    RuntimeView latest{};
    const auto &expected = g_model.view().control;
    if (!runtime_view(&latest) || latest.control.state_version != expected.state_version ||
        latest.control.boot_id != expected.boot_id)
        return false;
    return runtime_issue(action, params_hash, &grant) && grant.state_version == expected.state_version &&
           runtime_submit(grant, action, delay_ms, settings, speed_code);
}
bool execute_command(const panel_ui::Command &command) {
    switch (command.kind) {
    case panel_ui::CommandKind::SETTINGS:
        return submit_action(sirokko::Action::SETTINGS, settings_hash(command.settings), 0,
                             &command.settings);
    case panel_ui::CommandKind::TIMER:
        return submit_action(sirokko::Action::TIMER, command_hash(command.kind, command.delay_ms),
                             command.delay_ms, nullptr);
    case panel_ui::CommandKind::TRIAL_CONFIRM:
        return submit_action(sirokko::Action::TRIAL_CONFIRM, command_hash(command.kind, 0), 0, nullptr);
    case panel_ui::CommandKind::TRIAL_PWM:
        return submit_action(sirokko::Action::TRIAL_PWM, command_hash(command.kind, command.speed_code), 0,
                             nullptr, command.speed_code);
    case panel_ui::CommandKind::RESET_FAULT:
        return submit_action(sirokko::Action::RESET_FAULT, command_hash(command.kind, 0), 0, nullptr);
    case panel_ui::CommandKind::REQUEST_NETWORK_CREDENTIALS: {
        char ssid[33]{}, password[65]{};
        bool success = runtime_network_credentials(ssid, sizeof(ssid), password, sizeof(password));
        if (success) {
            g_model.set_credentials(ssid, password);
            success = g_model.credentials().available;
        }
        if (!success)
            g_model.credentials_unavailable();
        std::memset(ssid, 0, sizeof(ssid));
        std::memset(password, 0, sizeof(password));
        return success;
    }
    case panel_ui::CommandKind::NONE:
        return false;
    }
    return false;
}
void display_fail(const char *message) {
    if (!g_display.failed)
        ESP_LOGE(kTag, "%s", message);
    g_display.failed = true;
    g_healthy = false;
    // Buffer intentionally retained: a missing/late DMA callback is not permission to free or reuse it.
}
bool complete_transfer(TickType_t wait) {
    if (g_display.in_flight && xSemaphoreTake(g_display.color_done, wait) == pdTRUE) {
        g_display.in_flight = false;
        g_display.damage.commit(g_display.pending);
        return true;
    }
    return false;
}
void render_step(uint64_t now) {
    if (!g_display.panel || g_display.failed)
        return;
    if (g_display.in_flight && !complete_transfer(0)) {
        if (now >= g_display.transfer_started && now - g_display.transfer_started > kTransferTimeoutMs)
            display_fail("LCD transfer timeout; panel input disabled, independent KEY0/control retained");
        return;
    }
    const auto screen = g_model.screen();
    const auto &view = g_model.view();
    const auto requested = panel_render::select_scene(screen, view, now);
    const bool preempt = panel_render::urgent(requested) &&
                         (requested != g_renderer.frame().scene ||
                          view.control.faults != g_renderer.frame().view.control.faults ||
                          view.control.state_version != g_renderer.frame().view.control.state_version);
    if (preempt) {
        g_display.frame_active = false;
        g_display.next_frame = now;
        g_display.damage.invalidate();
    }
    if (!g_display.frame_active) {
        if (now < g_display.next_frame)
            return;
        g_renderer.update(screen, view, g_model.credentials(), now);
        g_display.next_y = 0;
        g_display.frame_started = now;
        g_display.frame_active = true;
    }
    // At most one bounded stripe per scheduler iteration; never sleep through a scene transition.
    const int y = g_display.next_y;
    if (!g_renderer.draw_band(y, panel_render::kStripeRows, g_display.buffer, g_display.scratch)) {
        display_fail("invalid panel render band");
        return;
    }
    g_display.pending = g_display.damage.inspect(g_display.buffer, y, panel_render::kStripeRows);
    const auto &damage = g_display.pending;
    if (damage.width) {
        for (int row = 0; row < panel_render::kStripeRows; ++row)
            std::memmove(g_display.buffer + row * damage.width,
                         g_display.buffer + row * panel_render::kWidth + damage.x,
                         static_cast<size_t>(damage.width) * sizeof(uint16_t));
        // ST7789 16-bit RAM data is big endian on the SPI wire; Canvas buffers remain host-native.
        for (int i = 0; i < damage.width * panel_render::kStripeRows; ++i) {
            const uint16_t p = g_display.buffer[i];
            g_display.buffer[i] = static_cast<uint16_t>((p >> 8) | (p << 8));
        }
        g_display.in_flight = true;
        g_display.transfer_started = now;
        if (!ok(esp_lcd_panel_draw_bitmap(g_display.panel, damage.x, y, damage.x + damage.width,
                                          y + panel_render::kStripeRows, g_display.buffer))) {
            // A failed enqueue has no outstanding DMA transaction.
            g_display.in_flight = false;
            display_fail("LCD draw enqueue failed");
            return;
        }
    } else
        g_display.damage.commit(damage);
    g_display.next_y += panel_render::kStripeRows;
    if (g_display.next_y >= panel_render::kHeight) {
        g_display.frame_active = false;
        g_display.next_frame = g_display.frame_started + kFramePeriodMs;
    }
}
void panel_task(void *) {
    g_encoder.reset(gpio_get_level(board::kEncoderA) != 0, gpio_get_level(board::kEncoderB) != 0,
                    gpio_get_level(board::kEncoderPush) == 0, now_ms());
    uint64_t last_view{};
    while (true) {
        const uint64_t now = now_ms();
        // Refresh before processing a pending click, so a new STOP/fault invalidates the old context.
        if (now < last_view || now - last_view >= kViewPeriodMs) {
            update_view();
            last_view = now;
        }
        const auto input =
            g_encoder.poll(gpio_get_level(board::kEncoderA) != 0, gpio_get_level(board::kEncoderB) != 0,
                           gpio_get_level(board::kEncoderPush) == 0, now);
        if (!g_display.failed) {
            if (input.turns)
                g_model.rotate(input.turns);
            if (input.click)
                g_model.click(now);
            g_model.tick(now);
            panel_ui::Command command{};
            if (g_model.take_command(&command)) {
                const bool accepted = execute_command(command);
                g_model.command_result(accepted);
            }
            render_step(now);
        }
        // Wake directly on DMA completion, with at most one RTOS tick of waiting.
        // Skipped stripes yield; a finished frame sleeps so idle/watchdog tasks get CPU.
        if (g_display.in_flight && !g_display.failed)
            complete_transfer(1);
        else if (g_display.frame_active && !g_display.failed)
            taskYIELD();
        else
            vTaskDelay(1);
    }
}
} // namespace

bool panel_begin() {
    if (g_started)
        return true;
    if (!inputs_begin() || !display_begin()) {
        ESP_LOGE(kTag, "local panel initialization failed");
        display_release();
        return false;
    }
    RuntimeView runtime{};
    if (runtime_view(&runtime))
        g_model.reset(panel_view(runtime));
    else {
        g_model.reset({});
        g_model.set_view_unavailable();
    }
    g_healthy = true;
    if (xTaskCreate(panel_task, "panel", 8192, nullptr, 3, nullptr) != pdPASS) {
        g_healthy = false;
        ESP_LOGE(kTag, "local panel task creation failed");
        display_release();
        return false;
    }
    g_started = true;
    return true;
}

bool panel_ready() {
    return g_healthy.load();
}
