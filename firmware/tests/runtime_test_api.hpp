#pragma once
// Deterministic scheduler boundary for app_main.cpp. Actual control/CLI/health tasks
// are compiled unmodified; hardware, scheduling and otadata calls are injected.
#include "idf_stubs.hpp"
#include <deque>
#include <functional>
#include <cstring>
#include <stdexcept>
namespace rt_fake {
inline uint64_t time_us{};
inline std::function<void()> tick;
inline std::function<void(uint32_t)> delay;
inline unsigned valid_marks{}, rollback_marks{}, wdt_resets{};
inline int image_state{};
struct Done {};
struct Restart {};
struct Queue { size_t size{},capacity{}; std::deque<std::vector<unsigned char>> data; };
}
using TickType_t=uint32_t;
using QueueHandle_t=rt_fake::Queue*;
using SemaphoreHandle_t=bool*;
constexpr int pdTRUE=1,pdPASS=1;
#define pdMS_TO_TICKS(ms) (ms)
inline TickType_t xTaskGetTickCount() { return rt_fake::time_us/1000; }
inline void vTaskDelay(uint32_t ms) { rt_fake::time_us+=ms*1000ULL; if(rt_fake::delay) rt_fake::delay(ms); }
inline void vTaskDelayUntil(TickType_t* t,uint32_t ms) { *t+=ms; rt_fake::time_us=static_cast<uint64_t>(*t)*1000; if(rt_fake::tick) rt_fake::tick(); }
inline void vTaskDelete(void*) { throw rt_fake::Done{}; }
inline int xTaskCreate(void(*)(void*),const char*,unsigned,void*,int,void*) { return pdPASS; }
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,int,void*,int) { return pdPASS; }
inline QueueHandle_t xQueueCreate(unsigned capacity,size_t size) { return new rt_fake::Queue{size,capacity,{}}; }
inline int xQueueSend(QueueHandle_t q,const void* p,uint32_t) {
    if(q->data.size()>=q->capacity) return 0;
    const auto* b=static_cast<const unsigned char*>(p); q->data.emplace_back(b,b+q->size); return pdTRUE;
}
inline int xQueueOverwrite(QueueHandle_t q,const void* p) { q->data.clear(); return xQueueSend(q,p,0); }
inline int xQueueReceive(QueueHandle_t q,void* p,uint32_t) {
    if(q->data.empty()) return 0;
    std::memcpy(p,q->data.front().data(),q->size); q->data.pop_front(); return pdTRUE;
}
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new bool(false); }
inline int xSemaphoreTake(SemaphoreHandle_t h,uint32_t) { if(!h || *h) return 0; *h=true; return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t h) { *h=false; }
inline int64_t esp_timer_get_time() { return rt_fake::time_us; }
inline int esp_task_wdt_add(void*) { return ESP_OK; }
inline int esp_task_wdt_reset() { ++rt_fake::wdt_resets; return ESP_OK; }
inline void esp_restart() { throw rt_fake::Restart{}; }
constexpr int ESP_RST_BROWNOUT=1;
inline int esp_reset_reason() { return 0; }
struct esp_app_desc_t { const char* version="0.1.7-DRAFT"; };
inline const esp_app_desc_t* esp_app_get_description() { static esp_app_desc_t d; return &d; }
struct esp_partition_t {};
using esp_ota_img_states_t=int;
constexpr int ESP_OTA_IMG_PENDING_VERIFY=1;
inline const esp_partition_t* esp_ota_get_running_partition() { static esp_partition_t p; return &p; }
inline int esp_ota_get_state_partition(const esp_partition_t*,int* state) { *state=rt_fake::image_state; return ESP_OK; }
inline int esp_ota_mark_app_valid_cancel_rollback() { ++rt_fake::valid_marks; return ESP_OK; }
inline int esp_ota_mark_app_invalid_rollback_and_reboot() { ++rt_fake::rollback_marks; throw rt_fake::Restart{}; }
inline int gpio_get_level(gpio_num_t pin) { return fake_idf::levels[pin]; }
