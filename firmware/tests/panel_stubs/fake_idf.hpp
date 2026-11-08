#pragma once
// Test-only transport/RTOS fixture. Not an ESP32 emulator or hardware timing measurement.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
using esp_err_t=int;
constexpr esp_err_t ESP_OK=0,ESP_FAIL=-1;
using BaseType_t=int;
using TickType_t=uint32_t;
constexpr BaseType_t pdFALSE=0,pdTRUE=1,pdPASS=1;
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
using gpio_num_t=int;
constexpr int GPIO_NUM_NC=-1;
constexpr int GPIO_NUM_1=1,GPIO_NUM_2=2,GPIO_NUM_4=4,GPIO_NUM_5=5,GPIO_NUM_6=6,GPIO_NUM_7=7,GPIO_NUM_8=8,GPIO_NUM_9=9,GPIO_NUM_10=10,GPIO_NUM_11=11,GPIO_NUM_12=12,GPIO_NUM_13=13,GPIO_NUM_14=14,GPIO_NUM_15=15,GPIO_NUM_16=16,GPIO_NUM_17=17,GPIO_NUM_38=38,GPIO_NUM_40=40,GPIO_NUM_41=41,GPIO_NUM_42=42,GPIO_NUM_47=47;
constexpr int GPIO_MODE_INPUT=1,GPIO_PULLUP_ENABLE=1,GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0;
struct gpio_config_t {uint64_t pin_bit_mask{};int mode{},pull_up_en{},pull_down_en{},intr_type{};};
constexpr int SPI3_HOST=2,SPI_DMA_CH_AUTO=0;
struct spi_bus_config_t {int sclk_io_num{},mosi_io_num{},miso_io_num{},quadwp_io_num{},quadhd_io_num{},max_transfer_sz{};};
using esp_lcd_spi_bus_handle_t=void*;
using esp_lcd_panel_io_handle_t=void*;
using esp_lcd_panel_handle_t=void*;
struct esp_lcd_panel_io_event_data_t {};
using ColorDone=bool(*)(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*);
struct esp_lcd_panel_io_spi_config_t {
    int cs_gpio_num{},dc_gpio_num{},spi_mode{},pclk_hz{},trans_queue_depth{},lcd_cmd_bits{},lcd_param_bits{};
    ColorDone on_color_trans_done{};void* user_ctx{};
};
constexpr int LCD_RGB_ELEMENT_ORDER_BGR=1,LCD_RGB_DATA_ENDIAN_BIG=0;
struct esp_lcd_panel_dev_config_t {int reset_gpio_num{},rgb_ele_order{},bits_per_pixel{},data_endian{};};
struct FakeSemaphore {bool signalled{};};
using SemaphoreHandle_t=FakeSemaphore*;
constexpr int MALLOC_CAP_DMA=1,MALLOC_CAP_INTERNAL=2,MALLOC_CAP_8BIT=4;
namespace fake_idf {
struct Transfer {int x{},y{},right{},bottom{};const void* original{};std::vector<uint8_t> bytes;};
inline uint64_t clock_us{};
inline ColorDone callback{};
inline void* callback_context{};
inline bool pending{},fail_draw{},swap{},mirror_x{},mirror_y{};
inline int allocations{},frees{},draws{};
inline uint32_t spi_hz{};
inline int endian=-1,task_priority{},task_stack{};
inline std::vector<Transfer> transfers;
inline std::array<uint16_t,320*240> framebuffer{};
inline void (*task)(void*){};
template<typename...Args>inline void log(const char*,const char*,Args...){}
inline void complete() {
    assert(pending&&!transfers.empty());const auto& t=transfers.back();
    assert(std::memcmp(t.original,t.bytes.data(),t.bytes.size())==0); // DMA memory must stay immutable.
    size_t n=0;
    for(int y=t.y;y<t.bottom;++y)for(int x=t.x;x<t.right;++x,n+=2)
        framebuffer[static_cast<size_t>(y*320+x)]=static_cast<uint16_t>((t.bytes[n]<<8)|t.bytes[n+1]);
    pending=false;assert(callback);callback(nullptr,nullptr,callback_context);
}
}
#define ESP_LOGE(...) fake_idf::log(__VA_ARGS__)
inline esp_err_t gpio_config(const gpio_config_t*){return ESP_OK;}
inline int gpio_get_level(gpio_num_t){return 1;}
inline esp_err_t spi_bus_initialize(int,const spi_bus_config_t* b,int){assert(b->max_transfer_sz>=320*16*2);return ESP_OK;}
inline esp_err_t spi_bus_free(int){return ESP_OK;}
inline SemaphoreHandle_t xSemaphoreCreateBinary(){return new FakeSemaphore{};}
inline void vSemaphoreDelete(SemaphoreHandle_t s){delete s;}
inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t* wake){s->signalled=true;*wake=pdTRUE;return pdTRUE;}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t wait){assert(wait<=1);if(!s->signalled)return pdFALSE;s->signalled=false;return pdTRUE;}
inline void* heap_caps_malloc(size_t size,int){++fake_idf::allocations;return std::malloc(size);}
inline void heap_caps_free(void* p){++fake_idf::frees;std::free(p);}
inline int64_t esp_timer_get_time(){return static_cast<int64_t>(fake_idf::clock_us);}
inline void vTaskDelay(TickType_t ticks){assert(ticks>0);fake_idf::clock_us+=ticks*1000;}
inline void fake_yield(){}
#define taskYIELD() fake_yield()
inline BaseType_t xTaskCreate(void(*fn)(void*),const char*,uint32_t stack,void*,int priority,void*) {
    fake_idf::task=fn;fake_idf::task_stack=static_cast<int>(stack);fake_idf::task_priority=priority;return pdPASS;
}
inline esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t,const esp_lcd_panel_io_spi_config_t* c,esp_lcd_panel_io_handle_t* out) {
    assert(c->trans_queue_depth==1);fake_idf::callback=c->on_color_trans_done;fake_idf::callback_context=c->user_ctx;
    fake_idf::spi_hz=static_cast<uint32_t>(c->pclk_hz);*out=reinterpret_cast<void*>(1);return ESP_OK;
}
inline esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t,const esp_lcd_panel_dev_config_t* c,esp_lcd_panel_handle_t* out) {
    assert(c->bits_per_pixel==16);fake_idf::endian=c->data_endian;*out=reinterpret_cast<void*>(2);return ESP_OK;
}
inline esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t){return ESP_OK;}
inline esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t){return ESP_OK;}
inline esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t,bool){return ESP_OK;}
inline esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t,bool swap){fake_idf::swap=swap;return ESP_OK;}
inline esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t,bool x,bool y){fake_idf::mirror_x=x;fake_idf::mirror_y=y;return ESP_OK;}
inline esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t,int,int){return ESP_OK;}
inline esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t,bool){return ESP_OK;}
inline esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t){assert(!fake_idf::pending);return ESP_OK;}
inline esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t){assert(!fake_idf::pending);return ESP_OK;}
inline esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int x,int y,int right,int bottom,const void* data) {
    assert(!fake_idf::pending);assert(x>=0&&right<=320&&y>=0&&bottom<=240);
    assert(right>x&&bottom>y&&bottom-y<=16);
    if(fake_idf::fail_draw)return ESP_FAIL;
    const auto* bytes=static_cast<const uint8_t*>(data);const size_t count=static_cast<size_t>((right-x)*(bottom-y)*2);
    fake_idf::transfers.push_back({x,y,right,bottom,data,std::vector<uint8_t>(bytes,bytes+count)});
    ++fake_idf::draws;fake_idf::pending=true;return ESP_OK;
}
