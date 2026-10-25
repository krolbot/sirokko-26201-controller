#pragma once
#include "idf_stubs.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <cstring>
using spi_host_device_t=int;
using esp_lcd_spi_bus_handle_t=int;
using esp_lcd_panel_io_handle_t=void*;
using esp_lcd_panel_handle_t=void*;
struct esp_lcd_panel_io_event_data_t {};
constexpr int SPI2_HOST=2, LCD_RGB_ELEMENT_ORDER_BGR=1;
struct esp_lcd_panel_io_spi_config_t {
    int cs_gpio_num{},dc_gpio_num{},spi_mode{}; unsigned pclk_hz{}; size_t trans_queue_depth{};
    bool (*on_color_trans_done)(void*,esp_lcd_panel_io_event_data_t*,void*){};
    void* user_ctx{}; int lcd_cmd_bits{},lcd_param_bits{}; uint8_t cs_ena_pretrans{},cs_ena_posttrans{};
};
struct esp_lcd_panel_dev_config_t { int reset_gpio_num{},rgb_ele_order{},bits_per_pixel{}; };
namespace spi_fake {
inline int64_t now{},bus_free{},pending_end{};
inline esp_lcd_panel_io_spi_config_t config{};
inline const uint16_t* pending{};
inline size_t allocated{};
inline int pending_y{};
inline bool stalled{},draw_error{};
inline std::vector<void*> allocations;
struct Strip { int y; std::vector<uint16_t> pixels; };
struct Transaction { int cs,mode; unsigned clock; int64_t start,end; };
inline std::vector<Strip> completed;
inline std::vector<Transaction> transactions;
inline std::vector<uint16_t> original;
inline void reset() {
    for (void* p:allocations) std::free(p);
    allocations.clear(); completed.clear(); transactions.clear(); original.clear();
    now=bus_free=pending_end=0; pending=nullptr; allocated=0; stalled=draw_error=false; config={};
}
inline void advance(int64_t target) {
    for (void* allocation:allocations) {
        const auto* words=static_cast<const uint16_t*>(allocation);
        if (words[0]!=0xface || words[allocated/sizeof(uint16_t)+1]!=0xbeef) throw std::runtime_error("render/DMA buffer overrun");
    }
    now=target;
    if (pending && !stalled && now>=pending_end) {
        if (!std::equal(original.begin(),original.end(),pending)) throw std::runtime_error("DMA buffer overwritten while busy");
        completed.push_back({pending_y,original}); pending=nullptr;
        config.on_color_trans_done(nullptr,nullptr,config.user_ctx);
    }
}
inline int64_t reserve(int cs,int mode,unsigned clock,unsigned bits,unsigned overhead_us=0) {
    const auto start=std::max(now,bus_free);
    const auto duration=(static_cast<int64_t>(bits)*1'000'000+clock-1)/clock+overhead_us;
    bus_free=start+duration; transactions.push_back({cs,mode,clock,start,bus_free}); return bus_free;
}
}
inline int64_t esp_timer_get_time() { return spi_fake::now; }
inline uint32_t pdMS_TO_TICKS(uint32_t n) { return n; } // deterministic model milliseconds, not a measured RTOS tick.
inline void vTaskDelay(uint32_t n) { spi_fake::advance(spi_fake::now+static_cast<int64_t>(n)*1000); }
inline void* spi_bus_dma_memory_alloc(int,size_t n,int) {
    spi_fake::allocated=n;
    auto* p=static_cast<uint16_t*>(std::malloc(n+2*sizeof(uint16_t)));
    if (!p) return nullptr;
    p[0]=0xface; p[n/sizeof(uint16_t)+1]=0xbeef;
    spi_fake::allocations.push_back(p); return p+1;
}
inline int esp_lcd_new_panel_io_spi(int,const esp_lcd_panel_io_spi_config_t* c,void** out) {
    spi_fake::config=*c; *out=reinterpret_cast<void*>(1); return ESP_OK;
}
inline int esp_lcd_new_panel_st7789(void*,const esp_lcd_panel_dev_config_t*,void** out) { *out=reinterpret_cast<void*>(2); return ESP_OK; }
inline int esp_lcd_panel_reset(void*) { return ESP_OK; }
inline int esp_lcd_panel_init(void*) { return ESP_OK; }
inline int esp_lcd_panel_disp_on_off(void*,bool) { return ESP_OK; }
inline int esp_lcd_panel_draw_bitmap(void*,int x,int y,int width,int end,const void* data) {
    if (spi_fake::draw_error) return ESP_FAIL;
    if (x || width!=sirokko::kUiWidth || end-y!=sirokko::kUiBandRows || spi_fake::pending) throw std::runtime_error("incompatible geometry/overlapping DMA");
    spi_fake::pending=static_cast<const uint16_t*>(data); spi_fake::pending_y=y;
    spi_fake::original.assign(spi_fake::pending,spi_fake::pending+sirokko::kUiWidth*sirokko::kUiBandRows);
    // Serial-bus model: full pixel payload plus 11 command/window bytes and a conservative 10us allowance.
    // This models arbitration/load, NOT measured IDF scheduling, edge rise/fall or CS-high timing.
    spi_fake::pending_end=spi_fake::reserve(spi_fake::config.cs_gpio_num,spi_fake::config.spi_mode,
        spi_fake::config.pclk_hz,(width*(end-y)*2+11)*8,10);
    return ESP_OK;
}
