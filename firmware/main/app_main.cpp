#include "runtime_api.hpp"
#include "start_stop_button.hpp"
#include "record_codec.hpp"
#include "panel.hpp"
#include "ota_activation.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include "board.hpp"
#include "bootloader_random.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensors.hpp"

namespace {
constexpr char kTag[]="sirokko";
constexpr uint32_t kSettingsMagic=0x53495232;
constexpr uint32_t kProcessMagic=0x50524332;
constexpr uint32_t kRangeMagic=0x524E4732;
struct RuntimeCommand {
    sirokko::Grant grant;
    sirokko::Action action;
    uint64_t delay_ms;
    sirokko::Settings settings;
    sirokko::ProcessProfile process;
};
struct OtaActivation {uint32_t lease{};const void* partition{};};
QueueHandle_t g_ota_activation_queue{};
sirokko::OtaActivationGate g_ota_activation;
std::atomic<uint32_t> g_maintenance_epoch{};
enum class StoreKind : uint8_t { PROCESS, RANGE };
struct StoreResult {
    StoreKind kind{};
    bool ok{};
    sirokko::ProcessProfile process;
    sirokko::OperatingRange range;
};
SensorSuite g_sensor_suite;
QueueHandle_t g_sensor_queue{},g_command_queue{},g_settings_queue{},g_process_queue{},g_range_queue{},g_store_results{};
SemaphoreHandle_t g_view_mutex{},g_gate_mutex{};
RuntimeView g_view{};
sirokko::Controller* g_controller{};
sirokko::CommandGate g_gate;
sirokko::FailClosedOutputDriver g_output_driver;
std::atomic<bool> g_stop{},g_operator_problem{},g_exit_maintenance{},g_output_driver_fault{};
std::atomic<bool> g_storage_ok{true},g_process_pending{},g_range_pending{},g_boot_ready{},g_web_ready{},g_credentials_ready{};
std::atomic<uint32_t> g_control_passes{};
char g_ap_ssid[33]{},g_ap_password[21]{};

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()/1000); }

template<class T> bool load_record(const char* key,uint32_t magic,T* output) {
    nvs_handle_t nvs{};
    if(nvs_open("sirokko",NVS_READONLY,&nvs)!=ESP_OK) return false;
    record_codec::Record<T> record{};
    size_t size=sizeof(record);
    const esp_err_t error=nvs_get_blob(nvs,key,&record,&size);
    nvs_close(nvs);
    if(error!=ESP_OK || !record_codec::valid(record,magic,size)) return false;
    *output=record.value;
    return true;
}
template<class T> bool save_record(const char* key,uint32_t magic,const T& value) {
    const auto record=record_codec::encode(magic,value);
    nvs_handle_t nvs{};
    if(nvs_open("sirokko",NVS_READWRITE,&nvs)!=ESP_OK) return false;
    bool ok=nvs_set_blob(nvs,key,&record,sizeof(record))==ESP_OK && nvs_commit(nvs)==ESP_OK;
    record_codec::Record<T> readback{};
    size_t size=sizeof(readback);
    ok=ok && nvs_get_blob(nvs,key,&readback,&size)==ESP_OK &&
        record_codec::valid(readback,magic,size) && std::memcmp(&record,&readback,sizeof(record))==0;
    nvs_close(nvs);
    return ok;
}
sirokko::Settings load_settings() {
    sirokko::Settings result{};
    if(!load_record("settings_v2",kSettingsMagic,&result) || !sirokko::valid_settings(result)) result={};
    result.room_setpoint_milli_c=15000; // explicit power-on contract, never an automatic START
    return result;
}
bool load_or_create_password() {
    nvs_handle_t nvs{};
    if(nvs_open("sirokko",NVS_READWRITE,&nvs)!=ESP_OK) return false;
    size_t size=sizeof(g_ap_password);
    const esp_err_t error=nvs_get_str(nvs,"ap_password",g_ap_password,&size);
    if(error==ESP_OK && size==sizeof(g_ap_password) && std::strlen(g_ap_password)==20) {
        nvs_close(nvs);return true;
    }
    if(error!=ESP_ERR_NVS_NOT_FOUND) { nvs_close(nvs);return false; } // do not silently rotate a damaged record
    constexpr char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    constexpr unsigned count=sizeof(alphabet)-1;
    bootloader_random_enable(); // no ADC/RF users yet; true entropy before AP initialization
    for(size_t i=0;i<20;) {
        uint8_t v{};esp_fill_random(&v,1);
        if(v<256U-(256U%count)) g_ap_password[i++]=alphabet[v%count];
    }
    bootloader_random_disable();
    g_ap_password[20]=0;
    const bool ok=nvs_set_str(nvs,"ap_password",g_ap_password)==ESP_OK && nvs_commit(nvs)==ESP_OK;
    char check[21]{};size=sizeof(check);
    const bool verified=ok && nvs_get_str(nvs,"ap_password",check,&size)==ESP_OK &&
        size==sizeof(check) && std::memcmp(check,g_ap_password,sizeof(check))==0;
    std::memset(check,0,sizeof(check));
    nvs_close(nvs);return verified;
}

bool output_begin() {
    gpio_set_level(board::kGlow,0);gpio_set_level(board::kFuel,0);gpio_set_level(board::kFanRequest,0);
    gpio_set_level(board::kExhaustCs,1);gpio_set_level(board::kAirCs,1);
    gpio_config_t outputs{};
    outputs.pin_bit_mask=(1ULL<<board::kGlow)|(1ULL<<board::kFuel)|(1ULL<<board::kFanRequest)|
        (1ULL<<board::kExhaustCs)|(1ULL<<board::kAirCs);
    outputs.mode=GPIO_MODE_OUTPUT;
    if(gpio_config(&outputs)!=ESP_OK) return false;
    gpio_set_level(board::kGlow,0);gpio_set_level(board::kFuel,0);gpio_set_level(board::kFanRequest,0);
    gpio_set_level(board::kExhaustCs,1);gpio_set_level(board::kAirCs,1);
    ledc_timer_config_t timer{};
    timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.duty_resolution=LEDC_TIMER_12_BIT;
    timer.timer_num=LEDC_TIMER_0;timer.freq_hz=board::kPwmFrequencyHz;timer.clk_cfg=LEDC_AUTO_CLK;
    if(ledc_timer_config(&timer)!=ESP_OK) return false;
    ledc_channel_config_t channel{};
    channel.gpio_num=board::kSpeedPwm;channel.speed_mode=LEDC_LOW_SPEED_MODE;
    channel.channel=LEDC_CHANNEL_0;channel.timer_sel=LEDC_TIMER_0;
    return ledc_channel_config(&channel)==ESP_OK;
}
bool digital_write(void*,sirokko::OutputLine line,bool enabled) {
    gpio_num_t pin=board::kFanRequest;
    if(line==sirokko::OutputLine::GLOW) pin=board::kGlow;
    else if(line==sirokko::OutputLine::FUEL) pin=board::kFuel;
    return gpio_set_level(pin,enabled?1:0)==ESP_OK;
}
bool pwm_write(void*,uint16_t code) {
    return ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,code)==ESP_OK &&
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0)==ESP_OK;
}
bool output_apply(const sirokko::Outputs& requested) {
    return g_output_driver.apply(requested,nullptr,digital_write,pwm_write);
}
bool button_runtime_snapshot(void*,start_stop_button::Snapshot* snapshot) {
    RuntimeView view{};
    if(!snapshot || !runtime_view(&view)) return false;
    *snapshot={view.control.state,view.control.start_block,view.control.state_version,view.control.delayed_armed,view.control.calibration_active};
    return true;
}
bool button_runtime_start(void*,uint64_t source_version) {
    sirokko::Grant grant{};
    if(!runtime_issue(sirokko::Action::START,0x4B45593053544152ULL,&grant) || grant.state_version!=source_version) return false;
    return runtime_submit(grant,sirokko::Action::START,0,nullptr);
}
bool button_runtime_stop(void*,bool problem) { runtime_stop(problem);return true; }
void start_stop_button_task(void*) {
    const start_stop_button::Executor executor{nullptr,button_runtime_snapshot,button_runtime_start,button_runtime_stop};
    start_stop_button::Button button(executor);
    button.reset(gpio_get_level(board::kStartStopButton)==0,now_ms());
    while(true) {
        button.poll(gpio_get_level(board::kStartStopButton)==0,now_ms());
        vTaskDelay(pdMS_TO_TICKS(start_stop_button::kTiming.polling_ms));
    }
}
bool start_stop_button_begin() {
    gpio_config_t input{};input.pin_bit_mask=1ULL<<board::kStartStopButton;
    input.mode=GPIO_MODE_INPUT;input.pull_up_en=GPIO_PULLUP_ENABLE;
    if(gpio_config(&input)!=ESP_OK) return false;
    return xTaskCreate(start_stop_button_task,"key0_button",3072,nullptr,4,nullptr)==pdPASS;
}
void sensor_task(void*) {
    while(true) {
        g_sensor_suite.poll(now_ms());
        const auto readings=g_sensor_suite.readings();
        xQueueOverwrite(g_sensor_queue,&readings);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
void storage_task(void*) {
    sirokko::Settings settings{};bool settings_pending=false;uint64_t edited_ms=0;
    while(true) {
        if(xQueueReceive(g_settings_queue,&settings,0)==pdTRUE) { settings_pending=true;edited_ms=now_ms(); }
        RuntimeView view{};
        const bool quiet=runtime_view(&view) && !view.control.outputs.fuel && !view.control.outputs.glow &&
            (view.control.state==sirokko::State::IDLE || view.control.state==sirokko::State::FAULT ||
             view.control.state==sirokko::State::RECOVERY_PURGE);
        if(quiet) {
            sirokko::ProcessProfile process{};
            sirokko::OperatingRange range{};
            if(xQueueReceive(g_process_queue,&process,0)==pdTRUE) {
                StoreResult result{};result.kind=StoreKind::PROCESS;result.process=process;
                result.ok=save_record("process_v2",kProcessMagic,process);
                g_storage_ok.store(result.ok);
                xQueueSend(g_store_results,&result,portMAX_DELAY);
            } else if(view.control.state!=sirokko::State::RECOVERY_PURGE && xQueueReceive(g_range_queue,&range,0)==pdTRUE) {
                StoreResult result{};result.kind=StoreKind::RANGE;result.range=range;
                result.ok=save_record("range_v2",kRangeMagic,range);
                g_storage_ok.store(result.ok);
                xQueueSend(g_store_results,&result,portMAX_DELAY);
            } else if(settings_pending && now_ms()-edited_ms>=1500) {
                const bool ok=save_record("settings_v2",kSettingsMagic,settings);
                g_storage_ok.store(ok);settings_pending=false;
                if(!ok) ESP_LOGE(kTag,"settings persistence/readback failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void control_task(void*) {
    sirokko::Sensors sensors{};
    TickType_t wake=xTaskGetTickCount();
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    while(true) {
        xQueueReceive(g_sensor_queue,&sensors,0);
        sirokko::TickInput input{};input.now_ms=now_ms();input.sensors=sensors;
        StoreResult stored{};
        if(xQueueReceive(g_store_results,&stored,0)==pdTRUE) {
            if(stored.kind==StoreKind::PROCESS) {
                if(stored.ok && !g_controller->update_process_profile(stored.process,input.now_ms)) g_storage_ok.store(false);
                g_process_pending.store(false);
            } else {
                if(!g_controller->finish_calibration_storage(stored.ok)) g_storage_ok.store(false);
                g_range_pending.store(false);
            }
        }
        input.external_faults=(g_output_driver_fault.load()?static_cast<uint32_t>(sirokko::FAULT_CONTROL_WATCHDOG):0U)|
            (g_storage_ok.load()?0U:static_cast<uint32_t>(sirokko::FAULT_STORAGE));
        input.stop=g_stop.exchange(false);input.operator_combustion_problem=g_operator_problem.exchange(false);
        RuntimeCommand command{};
        bool have=xQueueReceive(g_command_queue,&command,0)==pdTRUE;
        bool locked=false;
        const auto before=g_controller->snapshot(input.now_ms);
        if(have && xSemaphoreTake(g_gate_mutex,pdMS_TO_TICKS(2))==pdTRUE) {
            locked=true;have=g_gate.consume(command.grant,input.now_ms,before.state_version);
        } else if(have) have=false;
        if(!have && command.action==sirokko::Action::PROCESS_CONFIG && command.grant.valid) g_process_pending.store(false);
        if(have && command.action==sirokko::Action::SETTINGS) {
            if(g_controller->update_settings(command.settings)) {
                const auto accepted=g_controller->settings();xQueueOverwrite(g_settings_queue,&accepted);
            }
        }
        if(have && command.action==sirokko::Action::PROCESS_CONFIG) {
            if(xQueueSend(g_process_queue,&command.process,0)!=pdTRUE) { g_process_pending.store(false);g_storage_ok.store(false); }
        }
        input.stop=input.stop || g_stop.exchange(false);
        input.operator_combustion_problem=input.operator_combustion_problem || g_operator_problem.exchange(false);
        input.exit_maintenance=g_exit_maintenance.exchange(false);
        if(have) {
            input.start=command.action==sirokko::Action::START;
            input.start_calibration=command.action==sirokko::Action::CALIBRATE;
            input.arm_delayed=command.action==sirokko::Action::ARM;
            input.cancel_delayed=command.action==sirokko::Action::CANCEL_ARM;
            input.delay_ms=command.delay_ms;
            input.set_shutdown_timer=command.action==sirokko::Action::TIMER;
            input.shutdown_after_ms=command.delay_ms;
            input.reset_fault=command.action==sirokko::Action::RESET_FAULT;
            input.enter_maintenance=command.action==sirokko::Action::OTA;
        }
        const auto snapshot=g_controller->tick(input);
        if(locked) { xSemaphoreGive(g_gate_mutex); }
        if(!output_apply(snapshot.outputs)) {
            gpio_set_level(board::kFuel,0);gpio_set_level(board::kGlow,0);
            g_output_driver_fault.store(true);
            ESP_LOGE(kTag,"output update failed; fuel/glow forced LOW");
        }
        // This decision uses current controller/sensor/output state, never RuntimeView.
        const bool ota_safe=sirokko::ota_control_safe(snapshot,sensors,g_controller->process_profile(),input.now_ms) &&
            !g_output_driver_fault.load() && g_storage_ok.load() && !g_process_pending.load() &&
            !g_range_pending.load() && !input.stop && !input.operator_combustion_problem &&
            !g_stop.load() && !g_exit_maintenance.load();
        g_ota_activation.observe(ota_safe);
        g_maintenance_epoch.store(g_ota_activation.epoch());
        OtaActivation activate{};
        if(xQueueReceive(g_ota_activation_queue,&activate,0)==pdTRUE)
            g_ota_activation.queue(activate.lease,activate.partition,input.now_ms);
        const auto* ota_partition=static_cast<const esp_partition_t*>(g_ota_activation.take_due(input.now_ms));
        if(ota_partition && !g_stop.load() && !g_exit_maintenance.load()) {
            // Final flash metadata write is serialized by the sole state-machine owner.
            // No network wait, mutex wait or delay occurs in this cold, outputs-off action.
            esp_task_wdt_reset();
            if(ota_partition==esp_ota_get_next_update_partition(nullptr) &&
               esp_ota_set_boot_partition(ota_partition)==ESP_OK) esp_restart();
            else { g_storage_ok.store(false);g_exit_maintenance.store(true); }
        }
        if(xSemaphoreTake(g_view_mutex,pdMS_TO_TICKS(2))==pdTRUE) {
            g_view.control=snapshot;g_view.sensors=sensors;g_view.settings=g_controller->settings();g_view.now_ms=input.now_ms;
            g_view.process=g_controller->process_profile();g_view.storage_ok=g_storage_ok.load();
            g_view.events=g_controller->events();g_view.event_count=g_controller->event_count();
            xSemaphoreGive(g_view_mutex);
        }
        sirokko::OperatingRange candidate{};
        if(!g_range_pending.load() && g_controller->pending_calibration_range(&candidate)) {
            g_range_pending.store(true);
            if(xQueueSend(g_range_queue,&candidate,0)!=pdTRUE) {
                g_range_pending.store(false);g_storage_ok.store(false);
            }
        }
        g_control_passes.fetch_add(1);esp_task_wdt_reset();
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(sirokko::kControlPeriodMs));
    }
}
bool start_access_point() {
    if(esp_netif_init()!=ESP_OK) return false;
    const esp_err_t loop=esp_event_loop_create_default();
    if(loop!=ESP_OK && loop!=ESP_ERR_INVALID_STATE) return false;
    if(!esp_netif_create_default_wifi_ap()) return false;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if(esp_wifi_init(&init)!=ESP_OK) return false;
    uint8_t mac[6]{};
    if(esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP)!=ESP_OK) return false;
    std::snprintf(g_ap_ssid,sizeof(g_ap_ssid),"Sirokko-%02X%02X%02X",mac[3],mac[4],mac[5]);
    wifi_config_t config{};
    std::memcpy(config.ap.ssid,g_ap_ssid,std::strlen(g_ap_ssid));config.ap.ssid_len=std::strlen(g_ap_ssid);
    std::memcpy(config.ap.password,g_ap_password,std::strlen(g_ap_password));
    config.ap.channel=6;config.ap.max_connection=4;config.ap.authmode=WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.capable=true;config.ap.pmf_cfg.required=true;
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK || esp_wifi_set_mode(WIFI_MODE_AP)!=ESP_OK ||
       esp_wifi_set_config(WIFI_IF_AP,&config)!=ESP_OK || esp_wifi_start()!=ESP_OK) return false;
    g_credentials_ready.store(true);
    ESP_LOGI(kTag,"AP %s at http://192.168.4.1; credential available only on local panel",g_ap_ssid);
    return true;
}
void ota_health_task(void*) {
    const esp_partition_t* running=esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    const bool pending=running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY;
    if(!pending) { g_boot_ready.store(true);vTaskDelete(nullptr);return; }
    const uint32_t initial=g_control_passes.load();
    vTaskDelay(pdMS_TO_TICKS(5000));
    RuntimeView view{};
    const bool healthy=static_cast<uint32_t>(g_control_passes.load()-initial)>=200 && g_web_ready.load() &&
        !g_output_driver_fault.load() && runtime_view(&view) && !view.control.outputs.fuel && !view.control.outputs.glow;
    if(healthy && esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) g_boot_ready.store(true);
    else {
        runtime_stop();
        gpio_set_level(board::kFuel,0);gpio_set_level(board::kGlow,0);
        ESP_LOGE(kTag,"OTA health check failed; requesting previous application");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    vTaskDelete(nullptr);
}
} // namespace

bool runtime_view(RuntimeView* view) {
    if(!view || !g_view_mutex || xSemaphoreTake(g_view_mutex,pdMS_TO_TICKS(20))!=pdTRUE) return false;
    *view=g_view;xSemaphoreGive(g_view_mutex);return true;
}
bool runtime_issue(sirokko::Action action,uint64_t params_hash,sirokko::Grant* grant) {
    RuntimeView view{};
    if(!grant || !runtime_view(&view) || xSemaphoreTake(g_gate_mutex,pdMS_TO_TICKS(20))!=pdTRUE) return false;
    uint64_t token=(static_cast<uint64_t>(esp_random())<<32)|esp_random();
    if(!token) token=1;
    *grant=g_gate.issue(action,params_hash,now_ms(),view.control.state_version,token);
    xSemaphoreGive(g_gate_mutex);return grant->valid;
}
bool runtime_submit(const sirokko::Grant& grant,sirokko::Action action,uint64_t delay_ms,const sirokko::Settings* settings) {
    RuntimeView view{};
    if(!grant.valid || grant.action!=action || !runtime_view(&view) || action==sirokko::Action::PROCESS_CONFIG) return false;
    const bool starting=action==sirokko::Action::START || action==sirokko::Action::ARM || action==sirokko::Action::CALIBRATE;
    if(starting && (!g_boot_ready.load() || g_process_pending.load() || g_range_pending.load() || view.control.start_block!=sirokko::Block::NONE)) return false;
    if(action==sirokko::Action::CALIBRATE && !view.process.calibration_floor_speed_code) return false;
    if(action==sirokko::Action::ARM && (delay_ms==0 || delay_ms>sirokko::kMaximumTimerMs)) return false;
    if(action==sirokko::Action::OTA && (!g_boot_ready.load() || g_process_pending.load() || g_range_pending.load() ||
        view.control.state!=sirokko::State::IDLE || view.control.outputs.fan_request || view.control.outputs.fuel || view.control.outputs.glow)) return false;
    if(action==sirokko::Action::SETTINGS && (!settings || !sirokko::valid_settings(*settings) ||
        view.control.state==sirokko::State::MAINTENANCE || view.control.calibration_active)) return false;
    if(action==sirokko::Action::TIMER && (delay_ms>sirokko::kMaximumTimerMs ||
        !(view.control.state==sirokko::State::PREPURGE || view.control.state==sirokko::State::PREHEAT ||
          view.control.state==sirokko::State::IGNITION || view.control.state==sirokko::State::VERIFY || view.control.state==sirokko::State::RUN))) return false;
    RuntimeCommand command{};command.grant=grant;command.action=action;command.delay_ms=delay_ms;
    command.settings=settings?*settings:view.settings;
    return xQueueSend(g_command_queue,&command,0)==pdTRUE;
}
bool runtime_process_valid(const sirokko::ProcessProfile& p) { return p.qualified && sirokko::valid_process_profile(p); }
bool runtime_configure_process(const sirokko::Grant& grant,const sirokko::ProcessProfile& profile) {
    RuntimeView view{};
    if(!grant.valid || grant.action!=sirokko::Action::PROCESS_CONFIG || !runtime_process_valid(profile) ||
        !runtime_view(&view) || view.control.calibration_active || view.control.delayed_armed || g_range_pending.load() ||
        (view.control.state!=sirokko::State::IDLE && view.control.state!=sirokko::State::FAULT && view.control.state!=sirokko::State::RECOVERY_PURGE)) return false;
    bool expected=false;
    if(!g_process_pending.compare_exchange_strong(expected,true)) return false;
    RuntimeCommand command{};command.grant=grant;command.action=sirokko::Action::PROCESS_CONFIG;command.process=profile;
    if(xQueueSend(g_command_queue,&command,0)==pdTRUE) return true;
    g_process_pending.store(false);return false;
}
void runtime_stop(bool operator_combustion_problem) {
    if(operator_combustion_problem) g_operator_problem.store(true);
    g_stop.store(true);
    g_maintenance_epoch.store(0);
    if(g_gate_mutex && xSemaphoreTake(g_gate_mutex,pdMS_TO_TICKS(20))==pdTRUE) { g_gate.invalidate();xSemaphoreGive(g_gate_mutex); }
}
uint32_t runtime_maintenance_lease() {
    if(g_stop.load() || g_exit_maintenance.load() || g_output_driver_fault.load() || !g_storage_ok.load()) return 0;
    return g_maintenance_epoch.load();
}
bool runtime_maintenance_safe() {return runtime_maintenance_lease()!=0;}
bool runtime_activate_ota(uint32_t lease,const void* verified_partition) {
    if(!lease || lease!=runtime_maintenance_lease() || !verified_partition) return false;
    const OtaActivation request{lease,verified_partition};
    return xQueueSend(g_ota_activation_queue,&request,0)==pdTRUE;
}
void runtime_exit_maintenance() { g_exit_maintenance.store(true);g_maintenance_epoch.store(0); }
bool runtime_network_credentials(char* ssid,size_t ssid_capacity,char* password,size_t password_capacity) {
    if(!g_credentials_ready.load() || !ssid || !password || ssid_capacity<sizeof(g_ap_ssid) || password_capacity<sizeof(g_ap_password)) return false;
    std::memcpy(ssid,g_ap_ssid,sizeof(g_ap_ssid));std::memcpy(password,g_ap_password,sizeof(g_ap_password));return true;
}
extern "C" void app_main() {
    if(!output_begin()) { ESP_LOGE(kTag,"cannot establish safe GPIO state");return; }
    const esp_err_t nvs_error=nvs_flash_init();
    if(nvs_error!=ESP_OK) {
        g_storage_ok.store(false);
        ESP_LOGE(kTag,"NVS unavailable; preserving existing records, fuel disabled");
    }
    g_view_mutex=xSemaphoreCreateMutex();g_gate_mutex=xSemaphoreCreateMutex();
    g_ota_activation_queue=xQueueCreate(1,sizeof(OtaActivation));
    g_sensor_queue=xQueueCreate(1,sizeof(sirokko::Sensors));g_command_queue=xQueueCreate(8,sizeof(RuntimeCommand));
    g_settings_queue=xQueueCreate(1,sizeof(sirokko::Settings));g_process_queue=xQueueCreate(1,sizeof(sirokko::ProcessProfile));
    g_range_queue=xQueueCreate(1,sizeof(sirokko::OperatingRange));g_store_results=xQueueCreate(2,sizeof(StoreResult));
    if(!g_ota_activation_queue || !g_view_mutex || !g_gate_mutex || !g_sensor_queue || !g_command_queue || !g_settings_queue ||
       !g_process_queue || !g_range_queue || !g_store_results) return;
    sirokko::ProcessProfile profile{};
    if(!load_record("process_v2",kProcessMagic,&profile) || !runtime_process_valid(profile)) profile={};
    const auto settings=load_settings();
    const uint64_t boot_id=(static_cast<uint64_t>(esp_random())<<32)|esp_random();
    g_controller=new(std::nothrow) sirokko::Controller(profile,settings,boot_id);
    if(!g_controller) return;
    sirokko::OperatingRange range{};
    if(load_record("range_v2",kRangeMagic,&range)) g_controller->install_range(range);
    const bool sensors_ok=g_sensor_suite.begin();
    ESP_LOGI(kTag,"sensors initialization=%s",sensors_ok?"OK":"FAULT");
    if(xTaskCreatePinnedToCore(sensor_task,"sensors",6144,nullptr,8,nullptr,0)!=pdPASS ||
       xTaskCreatePinnedToCore(control_task,"control",24576,nullptr,20,nullptr,1)!=pdPASS ||
       xTaskCreate(storage_task,"storage",4096,nullptr,2,nullptr)!=pdPASS) {
        gpio_set_level(board::kFuel,0);gpio_set_level(board::kGlow,0);return;
    }
    if(!start_stop_button_begin()) ESP_LOGE(kTag,"KEY0 unavailable; control remains independent");
    // Network/panel setup follows the independent safety loop. A credential failure cannot stop cooling.
    if(nvs_error==ESP_OK && load_or_create_password() && start_access_point()) g_web_ready.store(web_start(g_ap_password));
    if(!g_web_ready.load()) ESP_LOGE(kTag,"AP/browser unavailable; local control remains independent");
    if(!panel_begin()) ESP_LOGE(kTag,"panel unavailable; control and browser remain independent");
    if(xTaskCreate(ota_health_task,"ota_health",4096,nullptr,3,nullptr)!=pdPASS) ESP_LOGE(kTag,"boot self-check unavailable; START stays blocked");
    ESP_LOGI(kTag,"firmware %s; process configured=%d; physical fire qualification is not inferred",
        esp_app_get_description()->version,profile.qualified);
}
