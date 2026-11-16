#include "status_led.hpp"
#include "board.hpp"
#include "runtime_api.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

namespace {
constexpr char kTag[] = "status_led";
constexpr uint32_t kBoot = 0x080008;
constexpr uint32_t kError = 0x100000;

uint32_t state_color(const RuntimeView &view, uint64_t now) {
    using sirokko::State;
    if (view.control.faults || !view.storage_ok || view.control.state == State::FAULT ||
        view.control.state == State::FAULT_PURGE)
        return now % 1000 < 500 ? kError : 0;
    if (!view.startup_ready)
        return kBoot;
    switch (view.control.state) {
    case State::UNCONFIGURED:
    case State::TRIAL_READY:
        return 0x101000; // First-start setup required.
    case State::IDLE:
        return view.control.start_block == sirokko::Block::NONE ? 0x000010 : 0x101000;
    case State::PREHEAT:
    case State::IGNITION:
        return 0x101000;
    case State::RUN:
        return 0x001000;
    case State::RECOVERY_PURGE:
    case State::STOP_PURGE:
        return 0x000a10;
    case State::MAINTENANCE:
        return kBoot;
    case State::FAULT_PURGE:
    case State::FAULT:
        return kError;
    }
    return kError;
}

void led_task(void *) {
    led_strip_config_t config{};
    config.strip_gpio_num = board::kStatusLed;
    config.max_leds = 1;
    config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
    config.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt{};
    rmt.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt.resolution_hz = 10000000;
    led_strip_handle_t strip{};
    esp_err_t error = led_strip_new_rmt_device(&config, &rmt, &strip);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "LED unavailable: %s", esp_err_to_name(error));
        vTaskDelete(nullptr);
        return;
    }
    static RuntimeView view{};
    uint32_t previous = UINT32_MAX;
    while (true) {
        runtime_view(&view);
        const auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        // Indicate a fault if controller updates stop.
        const bool stale = !view.now_ms || now - view.now_ms > 1000;
        uint32_t color = kBoot;
        if (!stale)
            color = state_color(view, now);
        else if (view.now_ms || now > 10000)
            color = now % 1000 < 500 ? kError : 0;
        if (color != previous) {
            error = led_strip_set_pixel(strip, 0, (color >> 16) & 255, (color >> 8) & 255, color & 255);
            if (error == ESP_OK)
                error = led_strip_refresh(strip);
            if (error != ESP_OK) {
                ESP_LOGW(kTag, "LED update failed: %s", esp_err_to_name(error));
                led_strip_del(strip);
                vTaskDelete(nullptr);
                return;
            }
            previous = color;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
} // namespace

void status_led_begin() {
    if (xTaskCreate(led_task, "status_led", 4096, nullptr, 1, nullptr) != pdPASS)
        ESP_LOGW(kTag, "LED task unavailable");
}
