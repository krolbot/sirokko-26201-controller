#include "runtime_api.hpp"
#include "wifi_password.hpp"
#include "captive_dns.hpp"
#include "lwip/inet.h"
#include "start_stop_button.hpp"
#include "ota_activation.hpp"
#include "persistence.hpp"
#include "panel.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include "board.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
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
    constexpr uint32_t kSettingsMagic = 0x53495232;
    constexpr uint32_t kEnvelopeMagic = 0x454E5631;
    constexpr uint32_t kModelMagic = 0x4D4F4434;
    constexpr uint32_t kStageMagic = 0x53544734;
    constexpr char kSettingsKey[] = "settings_v2";
    constexpr char kEnvelopeKey[] = "envelope_v1";
    constexpr char kModelKey[] = "model_active4";
    constexpr char kStageKey[] = "model_stage4";
    enum class StoreKind:uint8_t {
        SETTINGS, MODEL_STAGE, MODEL_ACTIVE, MODEL_REVISION_STAGE, MODEL_REVISION_ACTIVE
    }
    ;
    struct RuntimeCommand {
        sirokko::Grant grant{
        }
        ;
        sirokko::Action action{
        }
        ;
        uint64_t delay_ms{
        }
        ;
        sirokko::Settings settings{
        }
        ;
        sirokko::LearnedModelRevision revision{
        }
        ;
    }
    ;
    struct StoreRequest {
        uint64_t id{
        }
        ,boot_id{
        }
        ;
        StoreKind kind{
        }
        ;
        sirokko::Settings settings{
        }
        ;
        sirokko::LearnedOperatingModel candidate{
        }
        ;
    }
    ;
    struct StoreResult {
        uint64_t id{
        }
        ;
        StoreKind kind{
        }
        ;
        bool ok{
        }
        ;
    }
    ;
    struct OtaActivation {
        uint32_t lease{
        }
        ;
        const void* partition{
        }
        ;
    }
    ;
    enum StorageFault : uint32_t {
        STORAGE_INIT = 1,
        STORAGE_SETTINGS = 2,
        STORAGE_ENVELOPE = 4,
        STORAGE_MODEL = 8,
        STORAGE_STAGE = 16,
        STORAGE_INTERNAL = 32,
    }
    ;
    std::atomic<uint32_t> g_storage_faults{
    }
    ;
    std::atomic<bool> g_storage_busy{
    }
    ,g_model_pending{
    }
    ,g_stop{
    }
    ,g_operator_problem{
    }
    ,g_exit_maintenance{
    }
    ,g_output_driver_fault{
    }
    ,g_boot_ready{
    }
    ,g_web_ready{
    }
    ,g_credentials_ready{
    }
    ;
    std::atomic<uint32_t> g_control_passes{
    }
    ,g_maintenance_epoch{
    }
    ;
    sirokko::ModelCommitGate g_model_commit;
    sirokko::OtaActivationGate g_ota_activation;
    sirokko::CommandGate g_gate;
    sirokko::FailClosedOutputDriver g_output_driver;
    sirokko::Controller* g_controller{
    }
    ;
    SensorSuite g_sensor_suite;
    QueueHandle_t g_sensor_queue{
    }
    ,g_command_queue{
    }
    ,g_storage_jobs{
    }
    ,g_store_results{
    }
    ,g_ota_activation_queue{
    }
    ;
    SemaphoreHandle_t g_view_mutex{
    }
    ,g_gate_mutex{
    }
    ;
    RuntimeView g_view{
    }
    ;
    char g_ap_ssid[33]{
    }
    ,g_ap_password[sirokko::wifi::kPasswordCapacity]{
    }
    ;
    uint64_t now_ms(){
        return static_cast<uint64_t>(esp_timer_get_time()/1000);
    }
    bool storage_healthy(){
        return g_storage_faults.load()==0;
    }
    uint32_t fault_for(const char* key) {
        if (!std::strcmp(key, kSettingsKey)) return STORAGE_SETTINGS;
        if (!std::strcmp(key, kEnvelopeKey)) return STORAGE_ENVELOPE;
        if (!std::strcmp(key, kModelKey)) return STORAGE_MODEL;
        if (!std::strcmp(key, kStageKey)) return STORAGE_STAGE;
        return STORAGE_INTERNAL;
    }
    void storage_status(uint32_t fault,bool ok){
        if(ok)g_storage_faults.fetch_and(~fault);
        else g_storage_faults.fetch_or(fault);
    }
    enum class RecordRead:uint8_t {
        MISSING,VALID,INVALID
    }
    ;
    template<class T> RecordRead read_record(const char* key,uint32_t magic,record_codec::Record<T>* out){
        nvs_handle_t nvs{
        }
        ;
        if(nvs_open("sirokko",NVS_READONLY,&nvs)!=ESP_OK){
            storage_status(fault_for(key),false);
            return RecordRead::INVALID;
        }
        record_codec::Record<T> record{
        }
        ;
        size_t size=sizeof(record);
        const auto e=nvs_get_blob(nvs,key,&record,&size);
        nvs_close(nvs);
        if(e==ESP_ERR_NVS_NOT_FOUND)return RecordRead::MISSING;
        if(e!=ESP_OK||!record_codec::valid(record,magic,size)){
            storage_status(fault_for(key),false);
            return RecordRead::INVALID;
        } *out=record;
        return RecordRead::VALID;
    }
    template<class T> bool load_record(const char* key,uint32_t magic,T* out){
        record_codec::Record<T> r{
        }
        ;
        if(read_record(key,magic,&r)!=RecordRead::VALID)return false;
        *out=r.value;
        return true;
    }
    template<class T> bool save_encoded_record(const char* key,uint32_t magic,const record_codec::Record<T>& record){
        nvs_handle_t nvs{
        }
        ;
        if(nvs_open("sirokko",NVS_READWRITE,&nvs)!=ESP_OK){
            storage_status(fault_for(key),false);
            return false;
        }
        bool ok=record_codec::valid(record,magic,sizeof(record))&&nvs_set_blob(nvs,key,&record,sizeof(record))==ESP_OK&&nvs_commit(nvs)==ESP_OK;
        record_codec::Record<T> check{
        }
        ;
        size_t size=sizeof(check);
        ok=ok&&nvs_get_blob(nvs,key,&check,&size)==ESP_OK&&record_codec::valid(check,magic,size)&&std::memcmp(&record,&check,sizeof(record))==0;
        nvs_close(nvs);
        storage_status(fault_for(key),ok);
        return ok;
    }
    template<class T> bool save_record(const char* key,uint32_t magic,const T& value){
        return save_encoded_record(key,magic,record_codec::encode(magic,value));
    }
    sirokko::Settings load_settings() {
        sirokko::Settings result{
        }
        ;
        if (load_record(kSettingsKey, kSettingsMagic, &result) && !sirokko::valid_settings(result)) {
            storage_status(STORAGE_SETTINGS, false);
            result = {
            }
            ;
        }
        return result;
    }
    // Factory preparation is the only production source for an initial envelope.
    // A record must pass record-codec integrity/version checks *and* the complete
    // envelope validator; a bare provisioned flag never authorizes combustion.
    sirokko::SafetyEnvelope load_factory_envelope() {
        // Do not use ModelRecord's payload type for the envelope; the local codec
        // record gives read_record its exact type without adding a runtime alias.
        record_codec::Record<sirokko::SafetyEnvelope> envelope_record{
        }
        ;
        const RecordRead state = read_record(kEnvelopeKey, kEnvelopeMagic, &envelope_record);
        const sirokko::SafetyEnvelope& envelope = envelope_record.value;
        if (state == RecordRead::MISSING) return {
        }
        ;
        if (state != RecordRead::VALID || !envelope.provisioned ||
        !sirokko::valid_safety_envelope(envelope)) {
            storage_status(STORAGE_ENVELOPE, false);
            return {
            }
            ;
        }
        return envelope;
    }
    void load_model(){
        sirokko::ModelRecord active{
        }
        ;
        record_codec::Record<sirokko::ModelStageV4> stage{
        }
        ;
        const auto active_state=read_record(kModelKey,kModelMagic,&active), stage_state=read_record(kStageKey,kStageMagic,&stage);
        if(active_state==RecordRead::INVALID||stage_state==RecordRead::INVALID)return;
        if(stage_state==RecordRead::VALID&&(!sirokko::valid_model_stage(stage.value,kModelMagic)||!sirokko::model_boot_consistent(stage.value,active_state==RecordRead::VALID,active))){
            storage_status(STORAGE_STAGE,false);
            return;
        }
        if(active_state==RecordRead::VALID&&active.value.valid&&!g_controller->install_learned_model(active.value))storage_status(STORAGE_MODEL,false);
    }
    bool write_model_stage(const StoreRequest& request){
        sirokko::ModelStageV4 stage{
        }
        ;
        stage.boot_id=request.boot_id;
        stage.transaction_id=request.id;
        const auto old=read_record(kModelKey,kModelMagic,&stage.previous);
        if(old==RecordRead::INVALID)return false;
        stage.previous_present=old==RecordRead::VALID;
        stage.candidate=record_codec::encode(kModelMagic,request.candidate);
        return save_record(kStageKey,kStageMagic,stage);
    }
    bool activate_model_stage(const StoreRequest& request){
        record_codec::Record<sirokko::ModelStageV4> stage{
        }
        ;
        return read_record(kStageKey,kStageMagic,&stage)==RecordRead::VALID&&sirokko::valid_model_stage(stage.value,kModelMagic)&&stage.value.boot_id==request.boot_id&&stage.value.transaction_id==request.id&&g_model_commit.claimed()&&save_encoded_record(kModelKey,kModelMagic,stage.value.candidate);
    }
    bool output_begin(){
        gpio_set_level(board::kGlow,0);
        gpio_set_level(board::kFuel,0);
        gpio_set_level(board::kFanRequest,0);
        gpio_config_t cfg{
        }
        ;
        cfg.pin_bit_mask=(1ULL<<board::kGlow)|(1ULL<<board::kFuel)|(1ULL<<board::kFanRequest);
        cfg.mode=GPIO_MODE_OUTPUT;
        if(gpio_config(&cfg)!=ESP_OK)return false;
        ledc_timer_config_t timer{
        }
        ;
        timer.speed_mode=LEDC_LOW_SPEED_MODE;
        timer.duty_resolution=LEDC_TIMER_12_BIT;
        timer.timer_num=LEDC_TIMER_0;
        timer.freq_hz=board::kPwmFrequencyHz;
        timer.clk_cfg=LEDC_AUTO_CLK;
        if(ledc_timer_config(&timer)!=ESP_OK)return false;
        ledc_channel_config_t channel{
        }
        ;
        channel.gpio_num=board::kSpeedPwm;
        channel.speed_mode=LEDC_LOW_SPEED_MODE;
        channel.channel=LEDC_CHANNEL_0;
        channel.timer_sel=LEDC_TIMER_0;
        return ledc_channel_config(&channel)==ESP_OK;
    }
    bool digital_write(void*,sirokko::OutputLine line,bool enabled){
        const gpio_num_t pin=line==sirokko::OutputLine::GLOW?board::kGlow:line==sirokko::OutputLine::FUEL?board::kFuel:board::kFanRequest;
        return gpio_set_level(pin,enabled?1:0)==ESP_OK;
    }
    bool pwm_write(void*,uint16_t code){
        return ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,code)==ESP_OK&&ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0)==ESP_OK;
    }
    bool output_apply(const sirokko::Outputs& out){
        return g_output_driver.apply(out,nullptr,digital_write,pwm_write);
    }
    bool button_snapshot(void*,start_stop_button::Snapshot* out){
        RuntimeView v{
        }
        ;
        if(!out||!runtime_view(&v))return false;
        *out={
            v.control.state,v.control.start_block,v.control.state_version,v.control.delayed_armed,v.control.commissioning.active
        }
        ;
        return true;
    }
    bool button_start(void*,uint64_t version){
        sirokko::Grant g{
        }
        ;
        return runtime_issue(sirokko::Action::START,0x4b45593053544152ULL,&g)&&g.state_version==version&&runtime_submit(g,sirokko::Action::START,0,nullptr);
    }
    bool button_stop(void*,bool problem){
        runtime_stop(problem);
        return true;
    }
    void button_task(void*){
        start_stop_button::Button button({
            nullptr,button_snapshot,button_start,button_stop
        }
        );
        button.reset(gpio_get_level(board::kStartStopButton)==0,now_ms());
        while(true){
            button.poll(gpio_get_level(board::kStartStopButton)==0,now_ms());
            vTaskDelay(pdMS_TO_TICKS(start_stop_button::kTiming.polling_ms));
        }
    }
    bool start_stop_button_begin(){
        gpio_config_t cfg{
        }
        ;
        cfg.pin_bit_mask=1ULL<<board::kStartStopButton;
        cfg.mode=GPIO_MODE_INPUT;
        cfg.pull_up_en=GPIO_PULLUP_ENABLE;
        return gpio_config(&cfg)==ESP_OK&&xTaskCreate(button_task,"key0_button",3072,nullptr,4,nullptr)==pdPASS;
    }
    void sensor_task(void*){
        while(true){
            g_sensor_suite.poll(now_ms());
            const auto s=g_sensor_suite.readings();
            xQueueOverwrite(g_sensor_queue,&s);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    struct ControlWork {
        sirokko::Sensors sensors{
        }
        ;
        bool settings_pending{
        }
        ;
        uint64_t edited_ms{
        }
        ,next_id{
        }
        ;
        StoreRequest inflight{
        }
        ;
        StoreResult result{
        }
        ;
        bool result_ready{
        }
        ;
    }
    ;
    struct StorageWork {
        StoreResult result{
        }
        ;
        bool waiting{
        }
        ;
    }
    ;
    bool enqueue(ControlWork& w,const StoreRequest& r){
        w.inflight=r;
        w.result_ready=false;
        g_storage_busy=true;
        if(xQueueSend(g_storage_jobs,&r,0)==pdTRUE)return true;
        g_storage_busy=false;
        storage_status(STORAGE_INTERNAL,false);
        return false;
    }
    void storage_pass(StorageWork& w){
        if(w.waiting){
            if(xQueueSend(g_store_results,&w.result,0)==pdTRUE)w.waiting=false;
            return;
        }
        StoreRequest r{
        }
        ;
        if(xQueueReceive(g_storage_jobs,&r,0)!=pdTRUE)return;
        w.result={
            r.id,r.kind,false
        }
        ;
        if(r.kind==StoreKind::SETTINGS)w.result.ok=save_record(kSettingsKey,kSettingsMagic,r.settings);
        else if(r.kind==StoreKind::MODEL_STAGE||r.kind==StoreKind::MODEL_REVISION_STAGE)w.result.ok=write_model_stage(r);
        else w.result.ok=activate_model_stage(r);
        w.waiting=xQueueSend(g_store_results,&w.result,0)!=pdTRUE;
    }
    void storage_task(void*){
        StorageWork w{
        }
        ;
        while(true){
            storage_pass(w);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    void finish_storage(ControlWork& w,const sirokko::TickInput& input){
        if(!g_storage_busy)return;
        if(!w.result_ready){
            if(xQueueReceive(g_store_results,&w.result,0)!=pdTRUE)return;
            if(w.result.id!=w.inflight.id||w.result.kind!=w.inflight.kind){
                storage_status(STORAGE_INTERNAL,false);
                return;
            }
            w.result_ready=true;
        }
        if(w.inflight.kind==StoreKind::MODEL_STAGE){
            const auto snap=g_controller->snapshot(input.now_ms);
            const bool safe=w.result.ok&&!input.stop&&!input.operator_combustion_problem&&!snap.faults&&snap.commissioning.active&&snap.commissioning.phase==sirokko::CommissioningPhase::PERSIST_STAGING&&g_model_commit.claim();
            if(safe&&g_controller->seal_model_storage(w.inflight.id)){
                auto active=w.inflight;
                active.kind=StoreKind::MODEL_ACTIVE;
                if(enqueue(w,active))return;
            }
            g_model_commit.revoke_precommit();
            g_controller->complete_model_storage(w.inflight.id,false);
            g_model_pending=false;
            g_model_commit.reset();
        }
        else if(w.inflight.kind==StoreKind::MODEL_REVISION_STAGE){
            const bool safe=w.result.ok&&!input.stop&&!input.operator_combustion_problem&&!g_controller->snapshot(input.now_ms).faults&&g_model_commit.claim();
            if(safe){
                auto active=w.inflight;
                active.kind=StoreKind::MODEL_REVISION_ACTIVE;
                if(enqueue(w,active))return;
            }
            g_model_commit.revoke_precommit();
            g_model_pending=false;
            g_model_commit.reset();
        }
        else if(w.inflight.kind==StoreKind::MODEL_ACTIVE){
            // A sealed record is durable evidence, not permission to change RAM while
            // the controller is still executing a STOP/fault purge. Retain the ACK and
            // finish after the safety owner reaches its terminal state.
            const auto snap=g_controller->snapshot(input.now_ms);
            if(w.result.ok&&(snap.state==sirokko::State::STOP_PURGE||snap.state==sirokko::State::FAULT_PURGE))return;
            if(!g_controller->complete_model_storage(w.inflight.id,w.result.ok)&&w.result.ok)storage_status(STORAGE_INTERNAL,false);
            g_model_pending=false;
            g_model_commit.reset();
        }
        else if(w.inflight.kind==StoreKind::MODEL_REVISION_ACTIVE){
            const auto snap=g_controller->snapshot(input.now_ms);
            if(w.result.ok&&(snap.state==sirokko::State::STOP_PURGE||snap.state==sirokko::State::FAULT_PURGE))return;
            if(w.result.ok&&!g_controller->install_learned_model(w.inflight.candidate))storage_status(STORAGE_INTERNAL,false);
            g_model_pending=false;
            g_model_commit.reset();
        }
        w.result_ready=false;
        g_storage_busy=false;
    }
    void grant_storage(ControlWork& w,const sirokko::Snapshot& snap,const sirokko::TickInput& input){
        if(g_storage_busy)return;
        StoreRequest r{
        }
        ;
        r.boot_id=snap.boot_id;
        r.id=++w.next_id;
        if(!r.id){
            storage_status(STORAGE_INTERNAL,false);
            return;
        }
        if(!snap.commissioning.active)g_model_commit.reset();
        if(!g_model_pending&&!input.stop&&!input.operator_combustion_problem&&g_controller->pending_learned_model(&r.candidate)&&g_model_commit.open()&&g_controller->begin_model_storage(r.id)){
            r.kind=StoreKind::MODEL_STAGE;
            g_model_pending=true;
            if(!enqueue(w,r)){
                g_controller->abort_model_storage();
                g_model_pending=false;
                g_model_commit.reset();
            }
            return;
        }
        if(w.settings_pending&&input.now_ms-w.edited_ms>=1500){
            r.kind=StoreKind::SETTINGS;
            r.settings=g_controller->settings();
            if(enqueue(w,r))w.settings_pending=false;
        }
    }
    void control_pass(ControlWork& w){
        xQueueReceive(g_sensor_queue,&w.sensors,0);
        sirokko::TickInput input{
        }
        ;
        input.now_ms=now_ms();
        input.sensors=w.sensors;
        input.external_faults=(g_output_driver_fault?static_cast<uint32_t>(sirokko::FAULT_CONTROL_WATCHDOG):0)|(storage_healthy()?0:static_cast<uint32_t>(sirokko::FAULT_STORAGE));
        input.stop=g_stop.exchange(false);
        input.operator_combustion_problem=g_operator_problem.exchange(false);
        input.exit_maintenance=g_exit_maintenance.exchange(false);
        RuntimeCommand c{
        }
        ;
        bool have=xQueueReceive(g_command_queue,&c,0)==pdTRUE;
        bool locked=false;
        const auto before=g_controller->snapshot(input.now_ms);
        if(have&&xSemaphoreTake(g_gate_mutex,pdMS_TO_TICKS(2))==pdTRUE){
            locked=true;
            have=g_gate.consume(c.grant,input.now_ms,before.state_version);
        }
        else if(have)have=false;
        if(have&&(c.action==sirokko::Action::START||c.action==sirokko::Action::ARM||c.action==sirokko::Action::CALIBRATE||c.action==sirokko::Action::OTA||c.action==sirokko::Action::MODEL_REVISION)&&(g_storage_busy||g_model_pending||!storage_healthy()))have=false;
        if(have&&c.action==sirokko::Action::SETTINGS&&g_controller->update_settings(c.settings)){
            w.settings_pending=true;
            w.edited_ms=input.now_ms;
        }
        if(have){
            input.start=c.action==sirokko::Action::START;
            input.start_commissioning=c.action==sirokko::Action::CALIBRATE;
            input.arm_delayed=c.action==sirokko::Action::ARM;
            input.cancel_delayed=c.action==sirokko::Action::CANCEL_ARM;
            input.delay_ms=c.delay_ms;
            input.set_shutdown_timer=c.action==sirokko::Action::TIMER;
            input.shutdown_after_ms=c.delay_ms;
            input.reset_fault=c.action==sirokko::Action::RESET_FAULT;
            input.enter_maintenance=c.action==sirokko::Action::OTA;
            if(c.action==sirokko::Action::MODEL_REVISION){
                sirokko::Controller candidate=*g_controller;
                if(candidate.revise_learned_model(c.revision)){
                    StoreRequest r{
                    }
                    ;
                    r.id=++w.next_id;
                    r.boot_id=before.boot_id;
                    r.kind=StoreKind::MODEL_REVISION_STAGE;
                    r.candidate=candidate.learned_model();
                    if(!g_storage_busy&&!g_model_pending&&g_model_commit.open()){
                        g_model_pending=true;
                        if(!enqueue(w,r)){
                            g_model_pending=false;
                            g_model_commit.reset();
                        }
                    }
                }
            }
        }
        // Capture a STOP/fault arriving after command admission but before the only
        // state-owner tick. It must outrank the newly consumed command and storage ACK.
        input.stop=input.stop||g_stop.exchange(false);
        input.operator_combustion_problem=input.operator_combustion_problem||g_operator_problem.exchange(false);
        auto snap=g_controller->tick(input);
        if(locked)xSemaphoreGive(g_gate_mutex);
        if(input.stop||input.operator_combustion_problem||snap.faults)g_model_commit.revoke_precommit();
        finish_storage(w,input);
        snap=g_controller->snapshot(input.now_ms);
        if(!output_apply(snap.outputs)){
            gpio_set_level(board::kFuel,0);
            gpio_set_level(board::kGlow,0);
            g_output_driver_fault=true;
        }
        grant_storage(w,snap,input);
        snap=g_controller->snapshot(input.now_ms);
        const bool ota_safe=sirokko::ota_control_safe(snap,w.sensors,g_controller->safety_envelope(),input.now_ms)&&storage_healthy()&&!g_storage_busy&&!g_model_pending&&!g_output_driver_fault&&!input.stop&&!g_stop&&!g_exit_maintenance;
        g_ota_activation.observe(ota_safe);
        if(!ota_safe&&!g_stop&&!g_exit_maintenance)g_ota_activation.acknowledge_revoke();
        g_maintenance_epoch=g_ota_activation.epoch();
        OtaActivation a{
        }
        ;
        if(xQueueReceive(g_ota_activation_queue,&a,0)==pdTRUE)g_ota_activation.queue(a.lease,a.partition,input.now_ms);
        const auto* p=static_cast<const esp_partition_t*>(g_ota_activation.claim_due(input.now_ms));
        if(p){
            const bool selected=p==esp_ota_get_next_update_partition(nullptr)&&esp_ota_set_boot_partition(p)==ESP_OK;
            g_ota_activation.finish_commit(selected);
            if(selected)esp_restart();
            else {
                storage_status(STORAGE_INTERNAL,false);
                g_exit_maintenance=true;
            }
        }
        if(xSemaphoreTake(g_view_mutex,pdMS_TO_TICKS(2))==pdTRUE){
            g_view.control=snap;
            if((g_storage_busy||g_model_pending)&&g_view.control.start_block==sirokko::Block::NONE)g_view.control.start_block=sirokko::Block::BUSY;
            g_view.sensors=w.sensors;
            g_view.settings=g_controller->settings();
            g_view.envelope=g_controller->safety_envelope();
            g_view.now_ms=input.now_ms;
            g_view.storage_ok=storage_healthy();
            g_view.events=g_controller->events();
            g_view.event_count=g_controller->event_count();
            xSemaphoreGive(g_view_mutex);
        }
        ++g_control_passes;
        esp_task_wdt_reset();
    }
    void control_task(void*){
        ControlWork w{
        }
        ;
        TickType_t wake=xTaskGetTickCount();
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
        while(true){
            control_pass(w);
            vTaskDelayUntil(&wake,pdMS_TO_TICKS(sirokko::kControlPeriodMs));
        }
    }
    bool load_wifi_password(){
        nvs_handle_t nvs{};
        if(nvs_open("sirokko",NVS_READWRITE,&nvs)!=ESP_OK)return false;
        size_t size=sizeof(g_ap_password);
        const auto result=nvs_get_str(nvs,sirokko::wifi::kPasswordKey,g_ap_password,&size);
        nvs_close(nvs);
        if(result==ESP_ERR_NVS_NOT_FOUND){
            g_ap_password[0]=0;
            return true;
        }
        return result==ESP_OK&&sirokko::wifi::valid_password(g_ap_password);
    }
    bool start_access_point(){
        if(esp_netif_init()!=ESP_OK)return false;
        const auto loop=esp_event_loop_create_default();
        if(loop!=ESP_OK&&loop!=ESP_ERR_INVALID_STATE)return false;
        esp_netif_t* ap=esp_netif_create_default_wifi_ap();
        if(!ap)return false;
        if(esp_netif_dhcps_stop(ap)!=ESP_OK)return false;
        esp_netif_ip_info_t ip{};
        ip.ip.addr=ip.gw.addr=inet_addr("192.168.26.1");
        ip.netmask.addr=inet_addr("255.255.255.0");
        if(esp_netif_set_ip_info(ap,&ip)!=ESP_OK)return false;
        esp_netif_dns_info_t dns{};
        dns.ip.type=ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr=ip.ip.addr;
        uint8_t offer_dns=2; // DHCP OFFER_DNS
        if(esp_netif_set_dns_info(ap,ESP_NETIF_DNS_MAIN,&dns)!=ESP_OK||
           esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer_dns,sizeof(offer_dns))!=ESP_OK||
           esp_netif_dhcps_start(ap)!=ESP_OK)return false;
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
        if(esp_wifi_init(&init)!=ESP_OK)return false;
        uint8_t mac[6]{
        }
        ;
        if(esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP)!=ESP_OK)return false;
        std::snprintf(g_ap_ssid,sizeof(g_ap_ssid),"Sirokko-%02X%02X%02X",mac[3],mac[4],mac[5]);
        wifi_config_t cfg{
        }
        ;
        std::memcpy(cfg.ap.ssid,g_ap_ssid,std::strlen(g_ap_ssid));
        cfg.ap.ssid_len=std::strlen(g_ap_ssid);
        std::memcpy(cfg.ap.password,g_ap_password,std::strlen(g_ap_password));
        cfg.ap.channel=6;
        cfg.ap.max_connection=4;
        cfg.ap.authmode=g_ap_password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
        cfg.ap.pmf_cfg.capable=true;
        cfg.ap.pmf_cfg.required=g_ap_password[0]!=0;
        if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(WIFI_MODE_AP)!=ESP_OK||esp_wifi_set_config(WIFI_IF_AP,&cfg)!=ESP_OK||esp_wifi_start()!=ESP_OK)return false;
        ESP_LOGI(kTag,"Wi-Fi AP %s (%s), http://192.168.26.1",g_ap_ssid,g_ap_password[0]?"password protected":"open");
        if(!captive_dns_start())return false;
        g_credentials_ready=true;
        return true;
    }
}
// namespace
bool runtime_view(RuntimeView* out){
    if(!out||!g_view_mutex||xSemaphoreTake(g_view_mutex,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    *out=g_view;
    xSemaphoreGive(g_view_mutex);
    return true;
}
bool runtime_issue(sirokko::Action action,uint64_t hash,sirokko::Grant* out){
    RuntimeView view{
    }
    ;
    if(!out||!runtime_view(&view)||xSemaphoreTake(g_gate_mutex,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    uint64_t token=(static_cast<uint64_t>(esp_random())<<32)|esp_random();
    if(!token)token=1;
    *out=g_gate.issue(action,hash,now_ms(),view.control.state_version,token);
    xSemaphoreGive(g_gate_mutex);
    return out->valid;
}
bool runtime_submit(const sirokko::Grant& grant,sirokko::Action action,uint64_t delay,const sirokko::Settings* settings){
    RuntimeView view{
    }
    ;
    if(!grant.valid||grant.action!=action||!runtime_view(&view))return false;
    const bool starting=action==sirokko::Action::START||action==sirokko::Action::ARM||action==sirokko::Action::CALIBRATE;
    if(starting&&(!g_boot_ready||!storage_healthy()||g_storage_busy||g_model_pending||view.control.start_block!=sirokko::Block::NONE))return false;
    if(action==sirokko::Action::CALIBRATE&&view.control.commissioning.block!=sirokko::Block::NONE)return false;
    if(action==sirokko::Action::SETTINGS&&(!settings||!sirokko::valid_settings(*settings)||view.control.commissioning.active))return false;
    if(action==sirokko::Action::OTA&&(!g_boot_ready||view.control.state!=sirokko::State::IDLE||g_storage_busy||g_model_pending))return false;
    RuntimeCommand c{
    }
    ;
    c.grant=grant;
    c.action=action;
    c.delay_ms=delay;
    c.settings=settings?*settings:view.settings;
    return xQueueSend(g_command_queue,&c,0)==pdTRUE;
}
bool runtime_revise_model(const sirokko::Grant& grant,const sirokko::LearnedModelRevision& revision){
    RuntimeView view{
    }
    ;
    if(!grant.valid||grant.action!=sirokko::Action::MODEL_REVISION||!runtime_view(&view)||g_storage_busy||g_model_pending||view.control.state!=sirokko::State::IDLE)return false;
    RuntimeCommand c{
    }
    ;
    c.grant=grant;
    c.action=sirokko::Action::MODEL_REVISION;
    c.revision=revision;
    return xQueueSend(g_command_queue,&c,0)==pdTRUE;
}
void runtime_stop(bool problem){
    if(problem)g_operator_problem=true;
    g_stop=true;
    g_model_commit.revoke_precommit();
    g_ota_activation.revoke_precommit();
    g_maintenance_epoch=0;
    if(g_gate_mutex&&xSemaphoreTake(g_gate_mutex,pdMS_TO_TICKS(20))==pdTRUE){
        g_gate.invalidate();
        xSemaphoreGive(g_gate_mutex);
    }
}
uint32_t runtime_maintenance_lease(){
    if(g_stop||g_exit_maintenance||g_output_driver_fault||!storage_healthy()||g_storage_busy||g_model_pending||g_ota_activation.commit_started())return 0;
    return g_maintenance_epoch;
}
bool runtime_maintenance_safe(){
    return runtime_maintenance_lease()!=0;
}
bool runtime_activate_ota(uint32_t lease,const void* partition){
    if(!lease||lease!=runtime_maintenance_lease()||!partition)return false;
    const OtaActivation a{
        lease,partition
    }
    ;
    return xQueueSend(g_ota_activation_queue,&a,0)==pdTRUE;
}
void runtime_exit_maintenance(){
    g_exit_maintenance=true;
    g_ota_activation.revoke_precommit();
    g_maintenance_epoch=0;
}
bool runtime_save_wifi_password(const char* password){
    if(!sirokko::wifi::valid_password(password))return false;
    nvs_handle_t nvs{};
    if(nvs_open("sirokko",NVS_READWRITE,&nvs)!=ESP_OK)return false;
    const bool ok=nvs_set_str(nvs,sirokko::wifi::kPasswordKey,password)==ESP_OK&&nvs_commit(nvs)==ESP_OK;
    nvs_close(nvs);
    // Apply on the next normal restart, without disrupting control or the connection.
    return ok;
}
bool runtime_network_credentials(char* ssid,size_t ssid_size,char* password,size_t password_size){
    if(!g_credentials_ready||!g_web_ready||!ssid||!password||ssid_size<sizeof(g_ap_ssid)||password_size<sizeof(g_ap_password))return false;
    std::memcpy(ssid,g_ap_ssid,sizeof(g_ap_ssid));
    std::memcpy(password,g_ap_password,sizeof(g_ap_password));
    return true;
}
extern "C" void app_main(){
    if(!output_begin())return;
    if(nvs_flash_init()!=ESP_OK)storage_status(STORAGE_INIT,false);
    g_view_mutex=xSemaphoreCreateMutex();
    g_gate_mutex=xSemaphoreCreateMutex();
    g_sensor_queue=xQueueCreate(1,sizeof(sirokko::Sensors));
    g_command_queue=xQueueCreate(8,sizeof(RuntimeCommand));
    g_storage_jobs=xQueueCreate(1,sizeof(StoreRequest));
    g_store_results=xQueueCreate(1,sizeof(StoreResult));
    g_ota_activation_queue=xQueueCreate(1,sizeof(OtaActivation));
    if(!g_view_mutex||!g_gate_mutex||!g_sensor_queue||!g_command_queue||!g_storage_jobs||!g_store_results||!g_ota_activation_queue)return;
    const uint64_t boot=(static_cast<uint64_t>(esp_random())<<32)|esp_random();
    const auto envelope=load_factory_envelope();
    g_controller=new(std::nothrow)sirokko::Controller(envelope,load_settings(),boot);
    if(!g_controller)return;
    load_model();
    g_sensor_suite.begin();
    if(xTaskCreatePinnedToCore(sensor_task,"sensors",6144,nullptr,8,nullptr,0)!=pdPASS||xTaskCreatePinnedToCore(control_task,"control",24576,nullptr,20,nullptr,1)!=pdPASS||xTaskCreate(storage_task,"storage",4096,nullptr,2,nullptr)!=pdPASS)return;
    start_stop_button_begin();
    if(load_wifi_password()&&start_access_point())g_web_ready=web_start(g_ap_password);
    ESP_LOGI(kTag,"Web interface: %s",g_web_ready?"ready":"unavailable");
    g_boot_ready=true;
}
