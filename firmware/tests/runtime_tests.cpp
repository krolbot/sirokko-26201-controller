#define main baseline_main
#include "sim_tests.cpp"
#undef main
#include "../main/app_main.cpp"

bool LocalDisplay::begin() { healthy_.store(true); return true; }
bool LocalDisplay::render(const sirokko::UiFrame&) { return true; }
bool SensorSuite::begin() { return true; }
void SensorSuite::poll(uint32_t) {}
sirokko::SensorSample SensorSuite::room() const { return {}; }
sirokko::SensorSample SensorSuite::exhaust() const { return {}; }
sirokko::SensorSample SensorSuite::hot_air() const { return {}; }
uint8_t SensorSuite::exhaust_status() const { return 0; }
uint8_t SensorSuite::hot_air_status() const { return 0; }
bool service_network_begin() { return true; }
bool service_network_import(const char*) { return false; }
bool service_network_configured() { return false; }
bool service_network_connected() { return false; }
namespace {
bool sensor_freeze{};
void prepare(bool accepted=true) {
    sensor_freeze=false;
    fake_idf::observer={}; fake_idf::reset(); rt_fake::tick={}; rt_fake::delay={}; rt_fake::time_us=0; rt_fake::next_step_ms=0;
    rt_fake::valid_marks=rt_fake::rollback_marks=rt_fake::wdt_resets=0; rt_fake::image_state=0;
    delete g_sensor_queue; delete g_setting_queue; delete g_snapshot_mutex; delete g_service_mutex;
    g_sensor_queue=xQueueCreate(1,sizeof(SensorFrame)); g_setting_queue=xQueueCreate(1,sizeof(OperatorSettings));
    g_snapshot_mutex=xSemaphoreCreateMutex(); g_service_mutex=xSemaphoreCreateMutex();
    g_config=test_config(); g_config.commissioning_accepted=accepted;
    g_store=CommissioningStore{}; check(g_store.begin(),"store boot");
    g_outputs=OutputDriver{}; g_outputs.initialize(g_config); g_display.begin();
    g_storage_ok.store(true); g_settings_ok.store(true); g_config_changed.store(false); g_remote_stop.store(false); g_ota_pending.store(false);
    g_control_progress.store(0); g_flash_outputs_safe.store(false); g_current_faults.store(0);
    g_display_progress.store(0); g_passes.store(0); g_settings_revision.store(0); g_saved_revision.store(0);
    g_startup_faults=accepted?FAULT_NONE:FAULT_CONFIG; g_inputs_ok=g_config_integrity=g_records_ok=true;
    g_initial_settings={kOperatorSchema,20000,1000,0}; g_initial_counters={}; g_frame={}; g_history_snapshot={};
    g_initial_history.begin(g_store.boot_count(),g_initial_counters);
    g_lease_state.store(LeaseState::IDLE); g_lease_result.store(ServiceResult::BUSY);
    fake_idf::levels[5]=1; fake_idf::levels[6]=fake_idf::levels[7]=fake_idf::levels[39]=fake_idf::levels[40]=1;
}
void run(const std::function<void()>& inspect,bool display_progress=true,bool refresh=true) {
    auto feed=[&] {
        SensorFrame s{{18000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},0,0};
        if(g_frame.status.state==State::IGNITION_TRIAL || g_frame.status.state==State::FLAME_CONFIRM || g_frame.status.state==State::RUN_HIGH || g_frame.status.state==State::RUN_LOW) s.exhaust.milli_c=110000;
        xQueueOverwrite(g_sensor_queue,&s);
    };
    feed();
    const uint32_t deadline=ms()+12000;
    rt_fake::tick=[&] { if(ms()>deadline) throw std::runtime_error("control test timeout"); if(display_progress) g_display_progress.store(ms()); inspect(); if(refresh && !sensor_freeze) feed(); };
    try { control_task(nullptr); } catch(const rt_fake::Done&) {}
    rt_fake::tick={}; fake_idf::observer={};
}
void gpio_off() { for(int p:{14,15,21,41}) check(fake_idf::levels[p]==0,"flash granted before GPIO inhibit"); }
void stop_matrix() {
    for(State target:{State::WAIT_COOLDOWN,State::PREPURGE,State::PREHEAT,State::SPINUP,State::IGNITION_TRIAL,State::FLAME_CONFIRM,State::RUN_HIGH}) {
        prepare(); bool stop_sent{};
        run([&] {
            auto state=g_frame.status.state;
            if(stop_sent) { check(off(g_frame.status.outputs),"STOP did not remove fuel/glow in same control pass"); check(state==State::POST_PURGE || state==State::WAIT_COOLDOWN,"STOP state"); throw rt_fake::Done{}; }
            if(state==target && (target!=State::WAIT_COOLDOWN || ms()>=100)) { runtime_remote_stop(); stop_sent=true; }
            else if(state==State::WAIT_COOLDOWN && ms()>=100) fake_idf::levels[7]=0;
            else if(state!=State::WAIT_COOLDOWN) fake_idf::levels[7]=1;
        });
        check(stop_sent && rt_fake::wdt_resets>0,"target reached");
    }
}
void start_in_menu_stop_priority() {
    prepare(); bool menu_seen{},started{},stop_sent{};
    run([&] {
        if(stop_sent) { check(off(g_frame.status.outputs) && g_frame.page==UiPage::HOME,"STOP priority in menu"); throw rt_fake::Done{}; }
        if(ms()>=100 && !menu_seen) fake_idf::levels[39]=fake_idf::levels[40]=0;
        if(g_frame.page==UiPage::SETTINGS) { menu_seen=true; fake_idf::levels[39]=fake_idf::levels[40]=1; fake_idf::levels[7]=0; }
        if(g_frame.status.state==State::PREPURGE) { started=true; runtime_remote_stop(); stop_sent=true; }
    });
    check(menu_seen && started,"START was swallowed by settings");
}
void physical_OFF_ack_and_held_START() {
    prepare(); unsigned phase{}; uint32_t since{};
    run([&] {
        if(phase==0 && ms()>=100 && g_frame.status.state==State::WAIT_COOLDOWN) {
            g_lease_purpose.store(FlashPurpose::OTA); g_lease_state.store(LeaseState::REQUESTED);
            fake_idf::observer=[](const std::string&) { check(g_lease_state.load()==LeaseState::REQUESTED,"ack published before GPIO apply finished"); };
            phase=1;
        } else if(phase==1) {
            check(g_lease_state.load()==LeaseState::GRANTED && runtime_flash_safe(),"lease rejected or unsafe at acknowledgment"); gpio_off(); fake_idf::observer={};
            g_store.save_latched_faults(FAULT_IGNITION_NOT_PROVEN); g_current_faults.store(FAULT_SAFETY_OPEN);
            check(runtime_checkpoint(),"pre-update NVS checkpoint"); uint32_t persisted{}; check(g_store.load_latched_faults(&persisted) && persisted==(FAULT_IGNITION_NOT_PROVEN|FAULT_SAFETY_OPEN),"update erased an unflushed fault");
            fake_idf::levels[7]=0; since=ms(); phase=2;
        } else if(phase==2 && ms()-since>=100) {
            check(g_frame.status.state==State::WAIT_COOLDOWN && off(g_frame.status.outputs),"START crossed OTA lease");
            g_lease_state.store(LeaseState::RELEASE); since=ms(); phase=3;
        } else if(phase==3 && ms()-since>=100) {
            check(g_frame.status.state==State::WAIT_COOLDOWN,"held START became delayed ignition");
            fake_idf::levels[7]=1; since=ms(); phase=4;
        } else if(phase==4 && ms()-since>=80) { fake_idf::levels[7]=0; phase=5; }
        else if(phase==5 && g_frame.status.state==State::PREPURGE) throw rt_fake::Done{};
    });
    check(phase==5,"fresh START after maintenance");
}
void local_repair_but_no_OTA_on_invalid_profile() {
    prepare(false); bool requested{};
    run([&] {
        if(!requested && ms()>=150) { check(!g_outputs.healthy() && g_outputs.shutdown_applied(),"safe uncommissioned adapter"); g_lease_purpose.store(FlashPurpose::LOCAL_SETTINGS); g_lease_state.store(LeaseState::REQUESTED); requested=true; }
        else if(requested) { if(g_lease_state.load()!=LeaseState::GRANTED) { print_status(); std::cerr<<"lease="<<static_cast<int>(g_lease_state.load())<<" result="<<static_cast<int>(g_lease_result.load())<<" fan="<<g_frame.status.outputs.fan_request<<" speed="<<g_frame.status.outputs.speed_permille<<'\n'; } check(g_lease_state.load()==LeaseState::GRANTED,"first profile cannot be commissioned over USB"); gpio_off(); throw rt_fake::Done{}; }
    });
}
void failed_GPIO_never_grants_OTA() {
    prepare(); bool requested{};
    run([&] {
        if(!requested && ms()>=100) { g_lease_purpose.store(FlashPurpose::OTA); g_lease_state.store(LeaseState::REQUESTED); fake_idf::failing_call="gpio_level:14:0"; fake_idf::persistent_failure=true; requested=true; }
        else if(requested) { check(g_lease_state.load()==LeaseState::REJECTED && g_lease_result.load()==ServiceResult::OUTPUT,"failed OFF grant"); throw rt_fake::Done{}; }
    });
}
void request_timeout_never_returns_stale_OK() {
    prepare(); g_lease_result.store(ServiceResult::OK);
    rt_fake::delay=[](uint32_t) { g_lease_result.store(ServiceResult::OK); }; // control computes result but never publishes OFF acknowledgment.
    check(runtime_acquire_flash(FlashPurpose::OTA)==ServiceResult::BUSY && !*g_service_mutex && g_lease_state.load()==LeaseState::IDLE,"timeout reused stale OK");
    rt_fake::delay={};
}
void UI_or_sensor_stall_fails_closed() {
    for(bool stalled_ui:{false,true}) {
        prepare(); bool started{};
        run([&] {
            if(g_frame.status.state==State::WAIT_COOLDOWN && ms()>=100) fake_idf::levels[7]=0;
            if(g_frame.status.state==State::PREPURGE) started=true;
            if(g_frame.status.state==State::RUN_HIGH && !stalled_ui) sensor_freeze=true;
            if(stalled_ui && !started) g_display_progress.store(ms());
            if(ms()>6000) { check(started && g_frame.status.state==State::LOCKOUT && off(g_frame.status.outputs),"stalled peripheral blocked fuel OFF"); check(g_frame.status.faults & (stalled_ui?FAULT_LOCAL_UI:FAULT_ROOM_SENSOR),"stall fault reason"); throw rt_fake::Done{}; }
        },!stalled_ui,true);
    }
}
void storage_integrity_and_write_failure() {
    prepare(); g_records_ok=false; fake_idf::nvs["counters_v1"]={0xff,0x33}; const auto original=fake_idf::nvs;
    rt_fake::delay=[](uint32_t) { if(ms()>6000) throw rt_fake::Done{}; };
    try { storage_task(nullptr); } catch(const rt_fake::Done&) {}
    check(fake_idf::nvs==original,"storage automatically overwrote unknown records");
    prepare(); g_frame.config=g_config; g_frame.settings=g_initial_settings; g_frame.last_event.boot_id=1;
    fake_idf::failing_call="commit";
    rt_fake::delay=[](uint32_t wait) {
        if(wait==5 && g_lease_state.load()==LeaseState::REQUESTED) g_lease_state.store(LeaseState::GRANTED);
        if(!g_storage_ok.load()) throw rt_fake::Done{};
        if(ms()>7000) throw std::runtime_error("no attempted safe NVS snapshot");
    };
    try { storage_task(nullptr); } catch(const rt_fake::Done&) {}
    check(!g_storage_ok.load(),"failed commit reported healthy"); rt_fake::delay={};
    run([] { if(ms()>7100) { check(off(g_frame.status.outputs) && (g_frame.status.faults & FAULT_CONFIG),"NVS failure allowed firing"); throw rt_fake::Done{}; } });
}
void health(bool healthy,bool stalled=false) {
    prepare(); g_ota_pending.store(true); rt_fake::image_state=ESP_OTA_IMG_PENDING_VERIFY;
    rt_fake::delay=[&](uint32_t wait) {
        if(wait==5 && g_lease_state.load()==LeaseState::REQUESTED) { g_lease_state.store(LeaseState::GRANTED); return; }
        if(!stalled || ms()<4500) {
            g_passes.fetch_add(25); g_frame.now_ms=ms();
            g_frame.room=g_frame.exhaust=g_frame.hot_air={20000,ms(),SensorQuality::OK};
        }
        g_frame.config=g_config;
        if(!healthy) g_settings_ok.store(false);
        if(stalled && ms()>=5500) throw rt_fake::Done{};
        if(ms()>65000) throw std::runtime_error("health task did not decide");
    };
    try { health_task(nullptr); } catch(const rt_fake::Done&) {} catch(const rt_fake::Restart&) {}
    rt_fake::delay={};
    if(stalled) check(!rt_fake::valid_marks && g_ota_pending.load(),"stalled control approved image");
    else if(healthy) check(rt_fake::valid_marks==1 && !g_ota_pending.load() && !rt_fake::rollback_marks,"healthy image not marked");
    else check(rt_fake::rollback_marks==1 && !rt_fake::valid_marks && g_ota_pending.load(),"unhealthy image did not roll back");
}
}
int runtime_suite_main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        // DRAFT-06 removes the NC GPIO8 input. Preserve immediate remote STOP coverage;
        // physical long-STOP stage and GPIO8 pad-failure matrices live in four_button_tests.
        {"real_control_task_REMOTE_STOP_each_ignition_stage",stop_matrix},{"real_menu_keeps_long_START_and_remote_STOP_independent",start_in_menu_stop_priority},
        {"GPIO_OFF_ack_before_flash_and_START_race",physical_OFF_ack_and_held_START},{"uncommissioned_profile_local_USB_repair",local_repair_but_no_OTA_on_invalid_profile},
        {"GPIO_failure_rejects_flash_ack",failed_GPIO_never_grants_OTA},{"lease_timeout_never_reuses_previous_OK",request_timeout_never_returns_stale_OK},
        {"stalled_UI_and_sensor_keep_control_alive",UI_or_sensor_stall_fails_closed},{"real_storage_preserves_bad_records_and_latches_write_error",storage_integrity_and_write_failure},
        {"real_health_task_marks_only_valid_image",[]{health(true);}},{"real_health_task_rolls_back_bad_image",[]{health(false);}},
        {"stalled_control_cannot_approve_pending_image",[]{health(true,true);}}
    };
    size_t passed{};
    for(const auto& [name,test]:tests) try { test(); ++passed; std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { fake_idf::observer={}; rt_fake::tick={}; rt_fake::delay={}; std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    fake_idf::observer={}; rt_fake::tick={}; rt_fake::delay={};
    std::cout<<"RESULT "<<passed<<'/'<<tests.size()<<" passed (real app tasks; injected scheduler/peripherals/otadata)\n";
    return passed==tests.size()?0:1;
}
#ifndef SIROKKO_FOUR_BUTTON_HOST_TEST
int main() { return runtime_suite_main(); }
#endif
