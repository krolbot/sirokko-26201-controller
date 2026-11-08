#pragma once
// Deterministic test-only ESP-IDF transport. Real application passes are compiled
// unchanged into the runtime tests. This is NOT an NVS electrical/power-cut model.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cassert>

using esp_err_t=int;
constexpr esp_err_t ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=0x102, ESP_ERR_INVALID_STATE=0x103;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND=0x1102, ESP_ERR_NVS_TYPE_MISMATCH=0x1103;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH=0x110c, ESP_ERR_NVS_NO_FREE_PAGES=0x110d;
constexpr esp_err_t ESP_ERR_NVS_NEW_VERSION_FOUND=0x1110;
using TickType_t=uint32_t;
using BaseType_t=int;
constexpr BaseType_t pdTRUE=1,pdFALSE=0,pdPASS=1;
constexpr TickType_t portMAX_DELAY=0xffffffffU;
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
#define ESP_ERROR_CHECK(expr) do { assert((expr)==ESP_OK); } while(false)

namespace fake_runtime_idf {
struct Queue { size_t capacity{},item_size{};std::deque<std::vector<uint8_t>> items; };
struct Semaphore { bool available{true}; };
struct NvsHandle { std::string space; std::map<std::string,std::vector<uint8_t>> pending;std::vector<std::string> erased; };
inline uint64_t clock_ms{};
inline std::vector<std::unique_ptr<Queue>> queues;
inline std::vector<std::unique_ptr<Semaphore>> semaphores;
inline std::map<std::string,std::vector<uint8_t>> flash;
inline std::map<uint32_t,NvsHandle> handles;
inline uint32_t next_handle{1};
inline esp_err_t nvs_init_error{ESP_OK},nvs_open_error{ESP_OK},nvs_set_error{ESP_OK},nvs_commit_error{ESP_OK},nvs_get_error{ESP_OK};
inline std::function<void(const std::string&)> before_set,after_set,before_commit,after_commit,before_get;
inline unsigned commits{},restart_count{},boot_select_count{},watchdog_resets{};
inline TickType_t last_queue_wait{},maximum_queue_wait{};
inline bool durable_at_set{};
inline esp_err_t boot_select_error{ESP_OK};
inline bool output_failure{},web_ok{true},panel_ok{true};
inline bool pending_verify{};
inline bool app_valid{},rollback_requested{};
inline uint32_t pwm{};
inline std::map<int,int> gpio;
inline const void* selected_partition{};
inline std::function<void()> before_boot_select,after_boot_select;
template<class... T> void log(T&&...) {}
inline void reset() {
    queues.clear();semaphores.clear();flash.clear();handles.clear();next_handle=1;
    clock_ms=0;commits=restart_count=boot_select_count=watchdog_resets=0;last_queue_wait=maximum_queue_wait=0;durable_at_set=false;boot_select_error=ESP_OK;
    nvs_init_error=nvs_open_error=nvs_set_error=nvs_commit_error=nvs_get_error=ESP_OK;
    before_set={};after_set={};before_commit={};after_commit={};before_get={};
    before_boot_select={};after_boot_select={};gpio.clear();pwm=0;selected_partition=nullptr;
    output_failure=false;web_ok=panel_ok=true;pending_verify=app_valid=rollback_requested=false;
}
}
#define ESP_LOGI(...) fake_runtime_idf::log(__VA_ARGS__)
#define ESP_LOGE(...) fake_runtime_idf::log(__VA_ARGS__)
#define ESP_LOGW(...) fake_runtime_idf::log(__VA_ARGS__)

using QueueHandle_t=fake_runtime_idf::Queue*;
using SemaphoreHandle_t=fake_runtime_idf::Semaphore*;
inline QueueHandle_t xQueueCreate(size_t count,size_t bytes) {
    auto q=std::make_unique<fake_runtime_idf::Queue>();q->capacity=count;q->item_size=bytes;
    auto* result=q.get();fake_runtime_idf::queues.push_back(std::move(q));return result;
}
inline BaseType_t xQueueSend(QueueHandle_t q,const void* item,TickType_t wait) {
    fake_runtime_idf::last_queue_wait=wait;fake_runtime_idf::maximum_queue_wait=std::max(wait,fake_runtime_idf::maximum_queue_wait);
    if(!q || q->items.size()>=q->capacity) return pdFALSE;
    const auto* bytes=static_cast<const uint8_t*>(item);q->items.emplace_back(bytes,bytes+q->item_size);return pdTRUE;
}
inline BaseType_t xQueueOverwrite(QueueHandle_t q,const void* item) {
    if(!q) return pdFALSE;
    q->items.clear();return xQueueSend(q,item,0);
}
inline BaseType_t xQueueReceive(QueueHandle_t q,void* item,TickType_t wait) {
    fake_runtime_idf::last_queue_wait=wait;fake_runtime_idf::maximum_queue_wait=std::max(wait,fake_runtime_idf::maximum_queue_wait);
    if(!q || q->items.empty()) return pdFALSE;
    std::memcpy(item,q->items.front().data(),q->item_size);q->items.pop_front();return pdTRUE;
}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q) {return q?static_cast<unsigned>(q->items.size()):0;}
inline BaseType_t xQueueReset(QueueHandle_t q) {if(!q)return pdFALSE;q->items.clear();return pdTRUE;}
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    auto p=std::make_unique<fake_runtime_idf::Semaphore>();auto* result=p.get();
    fake_runtime_idf::semaphores.push_back(std::move(p));return result;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t) {if(!s || !s->available)return pdFALSE;s->available=false;return pdTRUE;}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {if(!s)return pdFALSE;s->available=true;return pdTRUE;}
inline TickType_t xTaskGetTickCount() {return static_cast<TickType_t>(fake_runtime_idf::clock_ms);}
inline void vTaskDelay(TickType_t ms) {fake_runtime_idf::clock_ms+=ms;}
inline void vTaskDelayUntil(TickType_t* last,TickType_t ms) {*last+=ms;fake_runtime_idf::clock_ms=*last;}
inline void vTaskDelete(void*) {}
inline BaseType_t xTaskCreate(void(*)(void*),const char*,uint32_t,void*,unsigned,void*) {return pdPASS;}
inline BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,unsigned,void*,int) {return pdPASS;}
inline int64_t esp_timer_get_time() {return static_cast<int64_t>(fake_runtime_idf::clock_ms*1000);}
inline esp_err_t esp_task_wdt_add(void*) {return ESP_OK;}
inline esp_err_t esp_task_wdt_reset() {++fake_runtime_idf::watchdog_resets;return ESP_OK;}

using nvs_handle_t=uint32_t;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
inline esp_err_t nvs_flash_init() {return fake_runtime_idf::nvs_init_error;}
inline esp_err_t nvs_open(const char* space,int,nvs_handle_t* out) {
    if(fake_runtime_idf::nvs_open_error!=ESP_OK)return fake_runtime_idf::nvs_open_error;
    *out=fake_runtime_idf::next_handle++;fake_runtime_idf::handles[*out].space=space;return ESP_OK;
}
inline std::string nvs_full_key(nvs_handle_t h,const char* key) {return fake_runtime_idf::handles.at(h).space+"/"+key;}
inline esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t size) {
    const auto full=nvs_full_key(h,key);
    if(fake_runtime_idf::before_set)fake_runtime_idf::before_set(full);
    if(fake_runtime_idf::nvs_set_error!=ESP_OK)return fake_runtime_idf::nvs_set_error;
    const auto* p=static_cast<const uint8_t*>(data);fake_runtime_idf::handles.at(h).pending[full]={p,p+size};
    if(fake_runtime_idf::durable_at_set)fake_runtime_idf::flash[full]=fake_runtime_idf::handles.at(h).pending[full];
    if(fake_runtime_idf::after_set)fake_runtime_idf::after_set(full);
    return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t h) {
    auto& handle=fake_runtime_idf::handles.at(h);
    const auto key=handle.pending.empty()?std::string{}:handle.pending.begin()->first;
    if(fake_runtime_idf::before_commit)fake_runtime_idf::before_commit(key);
    if(fake_runtime_idf::nvs_commit_error!=ESP_OK)return fake_runtime_idf::nvs_commit_error;
    for(const auto& name:handle.erased)fake_runtime_idf::flash.erase(name);
    handle.erased.clear();
    for(const auto& [name,value]:handle.pending)fake_runtime_idf::flash[name]=value;
    handle.pending.clear();++fake_runtime_idf::commits;
    if(fake_runtime_idf::after_commit)fake_runtime_idf::after_commit(key);
    return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* size) {
    const auto full=nvs_full_key(h,key);
    if(fake_runtime_idf::before_get)fake_runtime_idf::before_get(full);
    if(fake_runtime_idf::nvs_get_error!=ESP_OK)return fake_runtime_idf::nvs_get_error;
    auto found=fake_runtime_idf::flash.find(full);
    if(found==fake_runtime_idf::flash.end())return ESP_ERR_NVS_NOT_FOUND;
    const size_t needed=found->second.size();
    if(!out){*size=needed;return ESP_OK;}
    if(*size<needed){*size=needed;return ESP_ERR_NVS_INVALID_LENGTH;}
    std::memcpy(out,found->second.data(),needed);*size=needed;return ESP_OK;
}
inline esp_err_t nvs_set_str(nvs_handle_t h,const char* key,const char* data) {return nvs_set_blob(h,key,data,std::strlen(data)+1);}
inline esp_err_t nvs_get_str(nvs_handle_t h,const char* key,char* data,size_t* size) {return nvs_get_blob(h,key,data,size);}
inline esp_err_t nvs_erase_key(nvs_handle_t h,const char* key) {
    const auto full=nvs_full_key(h,key);
    if(!fake_runtime_idf::flash.count(full))return ESP_ERR_NVS_NOT_FOUND;
    fake_runtime_idf::handles.at(h).erased.push_back(full);return ESP_OK;
}
inline void nvs_close(nvs_handle_t h) {fake_runtime_idf::handles.erase(h);}

using gpio_num_t=int;
constexpr int GPIO_MODE_INPUT=1,GPIO_MODE_OUTPUT=2,GPIO_PULLUP_ENABLE=1,GPIO_PULLUP_DISABLE=0,GPIO_PULLDOWN_DISABLE=0;
struct gpio_config_t {uint64_t pin_bit_mask{};int mode{},pull_up_en{},pull_down_en{},intr_type{};};
inline esp_err_t gpio_config(const gpio_config_t*) {return ESP_OK;}
inline esp_err_t gpio_set_level(gpio_num_t pin,int level) {if(fake_runtime_idf::output_failure)return ESP_FAIL;fake_runtime_idf::gpio[pin]=level;return ESP_OK;}
inline int gpio_get_level(gpio_num_t pin) {return fake_runtime_idf::gpio[pin];}
constexpr int LEDC_LOW_SPEED_MODE=0,LEDC_TIMER_12_BIT=12,LEDC_TIMER_0=0,LEDC_AUTO_CLK=0,LEDC_CHANNEL_0=0;
struct ledc_timer_config_t {int speed_mode{},duty_resolution{},timer_num{},freq_hz{},clk_cfg{};};
struct ledc_channel_config_t {int gpio_num{},speed_mode{},channel{},timer_sel{};uint32_t duty{},hpoint{};};
inline esp_err_t ledc_timer_config(const ledc_timer_config_t*) {return ESP_OK;}
inline esp_err_t ledc_channel_config(const ledc_channel_config_t*) {return ESP_OK;}
inline esp_err_t ledc_set_duty(int,int,uint32_t duty) {if(fake_runtime_idf::output_failure)return ESP_FAIL;fake_runtime_idf::pwm=duty;return ESP_OK;}
inline esp_err_t ledc_update_duty(int,int) {return fake_runtime_idf::output_failure?ESP_FAIL:ESP_OK;}

inline void bootloader_random_enable() {}
inline void bootloader_random_disable() {}
inline uint32_t esp_random() {static uint32_t n=1234;return ++n;}
inline void esp_fill_random(void* out,size_t size) {std::memset(out,42,size);}
constexpr int ESP_MAC_WIFI_SOFTAP=0,WIFI_AUTH_WPA2_PSK=3,WIFI_STORAGE_RAM=1,WIFI_MODE_AP=2,WIFI_IF_AP=1;
struct wifi_init_config_t {int unused{};};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t {struct {uint8_t ssid[32]{},password[64]{};uint8_t ssid_len{},channel{},max_connection{};int authmode{};struct{bool capable{},required{};}pmf_cfg;}ap;};
inline esp_err_t esp_netif_init() {return ESP_OK;}
inline esp_err_t esp_event_loop_create_default() {return ESP_OK;}
inline void* esp_netif_create_default_wifi_ap() {static int x;return &x;}
inline esp_err_t esp_wifi_init(const wifi_init_config_t*) {return ESP_OK;}
inline esp_err_t esp_read_mac(uint8_t* out,int) {std::memset(out,42,6);return ESP_OK;}
inline esp_err_t esp_wifi_set_storage(int) {return ESP_OK;}
inline esp_err_t esp_wifi_set_mode(int) {return ESP_OK;}
inline esp_err_t esp_wifi_set_config(int,const wifi_config_t*) {return ESP_OK;}
inline esp_err_t esp_wifi_start() {return ESP_OK;}
struct esp_app_desc_t {const char* version{"runtime-host"};const char* project_name{"sirokko_26201_controller"};};
inline const esp_app_desc_t* esp_app_get_description() {static esp_app_desc_t d;return &d;}
struct esp_partition_t {uint32_t address{};};
enum esp_ota_img_states_t {ESP_OTA_IMG_NEW,ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID};
inline const esp_partition_t* esp_ota_get_running_partition() {static esp_partition_t p{0x20000};return &p;}
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {static esp_partition_t p{0x420000};return &p;}
inline esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* out) {*out=fake_runtime_idf::pending_verify?ESP_OTA_IMG_PENDING_VERIFY:ESP_OTA_IMG_VALID;return ESP_OK;}
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p) {
    if(fake_runtime_idf::before_boot_select)fake_runtime_idf::before_boot_select();
    ++fake_runtime_idf::boot_select_count;
    if(fake_runtime_idf::boot_select_error!=ESP_OK)return fake_runtime_idf::boot_select_error;
    fake_runtime_idf::selected_partition=p;
    if(fake_runtime_idf::after_boot_select)fake_runtime_idf::after_boot_select();
    return ESP_OK;
}
inline void esp_restart() {++fake_runtime_idf::restart_count;}
inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() {fake_runtime_idf::app_valid=true;return ESP_OK;}
inline esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot() {fake_runtime_idf::rollback_requested=true;esp_restart();return ESP_OK;}
