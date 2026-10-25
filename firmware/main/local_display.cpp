#include "local_display.hpp"
#include <algorithm>
#ifdef SIROKKO_DISPLAY_HOST_TEST
#include "display_test_api.hpp"
#else
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr int64_t kTransferTimeoutUs = 50'000;
bool on_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* context) {
    static_cast<std::atomic<bool>*>(context)->store(false, std::memory_order_release);
    return false;
}
}
bool LocalDisplay::begin() {
    pixels_ = static_cast<uint16_t*>(spi_bus_dma_memory_alloc(kSpiHost, sirokko::board::kTftBandBytes, 0));
    if (!pixels_) return false;
    esp_lcd_panel_io_spi_config_t cfg{};
    cfg.cs_gpio_num = sirokko::board::kTftCs;
    cfg.dc_gpio_num = sirokko::board::kTftDc;
    cfg.spi_mode = sirokko::board::kTftSpiMode;
    cfg.pclk_hz = sirokko::board::kTftSpiClockHz;
    cfg.cs_ena_pretrans = sirokko::board::kTftCsPreCycles;
    cfg.cs_ena_posttrans = sirokko::board::kTftCsPostCycles;
    cfg.trans_queue_depth = 1;
    cfg.on_color_trans_done = on_done;
    cfg.user_ctx = &transfer_in_progress_;
    cfg.lcd_cmd_bits = 8;
    cfg.lcd_param_bits = 8;
    esp_lcd_panel_io_handle_t io{};
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &cfg, &io) != ESP_OK) return false;
    io_ = io;
    esp_lcd_panel_dev_config_t panel_cfg{};
    panel_cfg.reset_gpio_num = sirokko::board::kTftReset;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_cfg.bits_per_pixel = 16;
    esp_lcd_panel_handle_t panel{};
    if (esp_lcd_new_panel_st7789(io, &panel_cfg, &panel) != ESP_OK) return false;
    panel_ = panel;
    if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) return false;
    std::fill_n(pixels_, sirokko::board::kTftWidth * sirokko::board::kTftBandRows, 0);
    healthy_.store(true);
    for (uint8_t band = 0; band < sirokko::board::kTftBandCount; ++band) {
        if (!submit_band(band)) return false;
        while (transfer_in_progress_.load(std::memory_order_acquire)) {
            if (esp_timer_get_time() - transfer_started_us_ > kTransferTimeoutUs) {
                // Do not call panel/io deletion: IDF waits for stalled in-flight SPI transactions.
                healthy_.store(false);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if (esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) { healthy_.store(false); return false; }
    return true;
}
bool LocalDisplay::submit_band(uint8_t band) {
    if (!healthy() || !panel_ || !pixels_ || band >= sirokko::board::kTftBandCount) return false;
    transfer_in_progress_.store(true, std::memory_order_release);
    transfer_started_us_ = esp_timer_get_time();
    const int y = band * sirokko::board::kTftBandRows;
    if (esp_lcd_panel_draw_bitmap(static_cast<esp_lcd_panel_handle_t>(panel_), 0, y,
        sirokko::board::kTftWidth, y + sirokko::board::kTftBandRows, pixels_) != ESP_OK) {
        transfer_in_progress_.store(false, std::memory_order_release);
        healthy_.store(false);
        return false;
    }
    return true;
}
bool LocalDisplay::render(const sirokko::UiFrame& frame) {
    if (!healthy()) return false;
    if (transfer_in_progress_.load(std::memory_order_acquire)) {
        if (esp_timer_get_time() - transfer_started_us_ > kTransferTimeoutUs) { healthy_.store(false); return false; }
        return true;
    }
    // Freeze a coherent frame for all physical DMA strips.
    if (next_band_ == 0) frame_ = frame;
    sirokko::render_ui_band(pixels_, next_band_, frame_);
    if (!submit_band(next_band_)) return false;
    next_band_ = static_cast<uint8_t>((next_band_ + 1) % sirokko::board::kTftBandCount);
    return true;
}
