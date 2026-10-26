// Production app_main -> Machine -> OutputDriver, with injected GPIO/RTOS only.
#define SIROKKO_FOUR_BUTTON_HOST_TEST
#include "runtime_tests.cpp"
#include <array>
namespace {
unsigned timer_passes{};
void timer_run(const std::function<void()>& inspect, State target=State::RUN_HIGH) {
    timer_passes=0;
    g_display_progress.store(ms());
    auto feed=[&] {
        if(sensor_freeze) return;
        const auto state=g_frame.status.state;
        SensorFrame f{{target==State::RUN_LOW && (state==State::RUN_HIGH || state==State::RUN_LOW)?20000:18000,ms(),SensorQuality::OK},
                      {20000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},0,0};
        if ((state==State::IGNITION_TRIAL && target!=State::IGNITION_TRIAL) || state==State::FLAME_CONFIRM || state==State::RUN_HIGH || state==State::RUN_LOW)
            f.exhaust.milli_c=110000;
        xQueueOverwrite(g_sensor_queue,&f);
    };
    feed();
    rt_fake::tick=[&] {
        if (++timer_passes>4000) { print_status(); throw std::runtime_error("timer app pass limit"); }
        inspect(); g_display_progress.store(ms()); feed();
    };
    try { control_task(nullptr); } catch(const rt_fake::Done&) {}
    rt_fake::tick={}; fake_idf::observer={};
}
// Set next observed core time relative to its last pass, preserving 64-bit uptime over uint32 wrap.
void jump(uint32_t delta) {
    rt_fake::time_us=(rt_fake::time_us/1000-static_cast<uint32_t>(ms()-g_frame.now_ms)+delta)*1000;
}
void safe_wait() {
    check(off(g_frame.status.outputs) && fake_idf::levels[14]==0 && fake_idf::levels[15]==0 && fake_idf::levels[21]==0,"timer waiting enabled dangerous GPIO");
}
void duration_boundaries() {
    for(uint16_t minutes:{0,1,60,1440}) for(bool wrap:{false,true}) for(bool late:{false,true}) {
        prepare(); g_initial_settings.duration_minutes=minutes;
        if(wrap) rt_fake::time_us=static_cast<uint64_t>(UINT32_MAX-10000u)*1000;
        unsigned phase{}; uint32_t origin{};
        timer_run([&] {
            const auto& s=g_frame.status;
            if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied() && s.start_block==StartBlock::NONE) { fake_idf::levels[7]=0; phase=1; }
            else if(phase==1 && s.state==State::PREPURGE) { origin=g_frame.now_ms; fake_idf::levels[7]=1; phase=2; }
            else if(phase==2 && s.state==State::RUN_HIGH) {
                if(!minutes) { check(!s.duration_active,"OFF started a timer"); jump(86400000u); phase=5; }
                else { check(s.duration_active && s.duration_remaining_ms==static_cast<uint32_t>(minutes)*60000u-(g_frame.now_ms-origin),"duration did not include ignition"); jump(s.duration_remaining_ms-1); phase=3; }
            } else if(phase==3) {
                check(s.state==State::RUN_HIGH && s.duration_remaining_ms==1,"before deadline stopped early"); jump(late?5000:1); phase=4;
            } else if(phase==4) {
                check(s.state==State::POST_PURGE && s.timer_expired && !s.duration_active && s.duration_remaining_ms==0 && s.outputs.fan_request,"expiry did not use normal postpurge"); safe_wait();
                check(g_frame.last_event.reason==StopReason::TIMER,"timer logged as thermostat");
                throw rt_fake::Done{};
            } else if(phase==5) { check(s.state==State::RUN_HIGH && !s.duration_active,"OFF stopped RUN"); throw rt_fake::Done{}; }
        });
    }
}
void duration_all_active_states() {
    for(State target:{State::PREPURGE,State::PREHEAT,State::SPINUP,State::IGNITION_TRIAL,State::FLAME_CONFIRM,State::RUN_HIGH,State::RUN_LOW}) {
        prepare(); g_initial_settings.duration_minutes=1;
        if(target==State::PREPURGE) g_config.prepurge_ms=120000;
        if(target==State::PREHEAT) g_config.preheat_ms=120000;
        if(target==State::SPINUP) g_config.spinup_ms=120000;
        if(target==State::IGNITION_TRIAL) g_config.ignition_window_ms=120000;
        if(target==State::FLAME_CONFIRM) g_config.flame_confirm_ms=120000;
        g_config.glow_max_ms=600000;
        unsigned phase{};
        timer_run([&] {
            const auto& s=g_frame.status;
            if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied()) { fake_idf::levels[7]=0; phase=1; }
            else if(phase==1 && s.state==target) { jump(s.duration_remaining_ms); phase=2; }
            else if(phase==2) { check(s.state==State::POST_PURGE && s.faults==0 && s.timer_expired && s.outputs.fan_request,"stage expiry faulted or skipped purge"); safe_wait(); throw rt_fake::Done{}; }
        },target);
    }
}
void delayed_two_timers() {
    for(uint16_t delay:{1,60,1440}) for(bool wrap:{false,true}) for(bool late:{false,true}) {
        prepare(); g_initial_settings.delay_minutes=delay; g_initial_settings.duration_minutes=1;
        if(wrap) rt_fake::time_us=static_cast<uint64_t>(UINT32_MAX-10000u)*1000;
        unsigned phase{}; uint32_t armed{},started{};
        timer_run([&] {
            const auto& s=g_frame.status;
            if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied()) { fake_idf::levels[7]=0; phase=1; }
            else if(phase==1 && s.delay_armed) { armed=g_frame.now_ms; check(!s.duration_active,"run duration started during delay"); phase=2; }
            else if(phase==2) { // Continue holding the original START: never cancel/repeat/ignite.
                safe_wait(); check(s.delay_armed && s.state==State::WAIT_COOLDOWN,"arming hold became STOP");
                if(static_cast<uint32_t>(g_frame.now_ms-armed)>=2000) { fake_idf::levels[7]=1; jump(s.delay_remaining_ms-1); phase=3; }
            } else if(phase==3) {
                check(s.delay_armed && s.delay_remaining_ms==1 && !s.duration_active,"delay pre-deadline"); safe_wait(); jump(late?5000:1); phase=4;
            } else if(phase==4) {
                check(s.state==State::PREPURGE && !s.delay_armed && s.duration_active && s.duration_remaining_ms==60000,"delay did not start ordinary independent session");
                started=g_frame.now_ms; phase=5;
            } else if(phase==5 && s.state==State::RUN_HIGH) { check(g_frame.counters.starts==1,"delay repeated start"); jump(s.duration_remaining_ms); phase=6; }
            else if(phase==6) { check(s.state==State::POST_PURGE && s.timer_expired && g_frame.now_ms-started==60000,"second timer expiry"); safe_wait(); runtime_remote_stop(); phase=7; }
            else if(phase==7) {
                // Cool the plant model and finish postpurge, then sustain a new heat demand without START.
                SensorFrame cold{{18000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},0,0};
                xQueueOverwrite(g_sensor_queue,&cold);
                if(s.state==State::WAIT_COOLDOWN) { jump(86400000u); phase=8; }
            } else if(phase==8) { safe_wait(); check(s.state==State::WAIT_COOLDOWN && !s.delay_armed && g_frame.counters.starts==1,"hidden re-ignition after duration"); throw rt_fake::Done{}; }
        });
    }
}
void delayed_cancel_and_fresh_conditions() {
    // Every change arrives after arming, exactly on the due pass. Cancel/deny is consumed once.
    for(int blocker=0;blocker<10;++blocker) {
        prepare(); g_initial_settings.delay_minutes=1; unsigned phase{};
        timer_run([&] {
            const auto& s=g_frame.status;
            if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied()) { fake_idf::levels[7]=0; phase=1; }
            else if(phase==1 && s.delay_armed) { fake_idf::levels[7]=1; phase=2; }
            else if(phase==2) {
                jump(s.delay_remaining_ms);
                if(blocker==0) runtime_remote_stop();
                if(blocker==1) fake_idf::levels[6]=0;
                if(blocker==2) fake_idf::levels[5]=0;
                if(blocker==3 || blocker==4) { g_lease_purpose.store(blocker==3?FlashPurpose::OTA:FlashPurpose::LOCAL_SETTINGS); g_lease_state.store(LeaseState::REQUESTED); }
                if(blocker==5) g_config_changed.store(true);
                if(blocker==6) g_settings_ok.store(false);
                if(blocker==7) fake_idf::forced_pad[8]=1;
                if(blocker==8 || blocker==9) {
                    sensor_freeze=true;
                    SensorFrame f{{blocker==8?30000:18000,ms(),SensorQuality::OK},{blocker==9?55000:20000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},0,0};
                    xQueueOverwrite(g_sensor_queue,&f);
                }
                phase=4;
            } else if(phase==4) {
                check(!s.delay_armed && s.state!=State::PREPURGE && g_frame.counters.starts==0,"cancellation lost to deadline"); safe_wait();
                check(s.delay_cancel_reason!=StartBlock::NONE || s.faults!=FAULT_NONE,"missing cancellation/fault reason");
                fake_idf::levels[6]=fake_idf::levels[5]=1; g_lease_state.store(LeaseState::RELEASE); sensor_freeze=false; jump(120000); phase=5;
            } else if(phase==5) { check(!s.delay_armed && s.state!=State::PREPURGE && g_frame.counters.starts==0,"request stayed pending after inhibit cleared"); safe_wait(); throw rt_fake::Done{}; }
        });
    }
}
void delayed_long_cancel_rearm() {
    prepare(); g_initial_settings.delay_minutes=1; unsigned phase{}; uint32_t since{};
    timer_run([&] {
        const auto& s=g_frame.status;
        if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied()) { fake_idf::levels[7]=0; phase=1; }
        else if(phase==1 && s.delay_armed) { fake_idf::levels[7]=1; since=ms(); phase=2; }
        else if(phase==2 && ms()-since>=100) { fake_idf::levels[7]=0; phase=3; }
        else if(phase==3 && !s.delay_armed) { check(s.state==State::WAIT_COOLDOWN && !s.outputs.fan_request,"cold cancel spuriously started purge"); safe_wait(); since=ms(); phase=4; }
        else if(phase==4 && ms()-since>=2000) { check(!s.delay_armed && s.state==State::WAIT_COOLDOWN,"held cancellation rearmed or immediately started"); fake_idf::levels[7]=1; since=ms(); phase=5; }
        else if(phase==5 && ms()-since>=100) { fake_idf::levels[7]=0; phase=6; }
        else if(phase==6 && s.delay_armed) { check(s.delay_remaining_ms==60000 && g_frame.counters.starts==0,"fresh explicit rearm"); throw rt_fake::Done{}; }
    });
}
// Real physical short/UP/DOWN pulses, no direct mutation of LocalMenu state.
struct Panel {
    std::vector<int> pins; size_t index{}; unsigned phase{}; uint32_t since{};
    explicit Panel(std::vector<int> p):pins(std::move(p)){}
    bool tick() {
        if(index==pins.size()) return true;
        if(phase==0) { fake_idf::levels[pins[index]]=0; since=ms(); phase=1; }
        else if(ms()-since>=100) {
            if(phase==1) { fake_idf::levels[pins[index]]=1; phase=2; since=ms(); }
            else { ++index; phase=0; }
        }
        return false;
    }
};
void real_menu_apply_cancel() {
    for(int choice:{0,1,2}) { // Apply / DOWN cancel / short exit on final page.
        prepare(); Panel panel({7,39,39,7,39,7,39,7,choice==0?39:choice==1?40:7});
        timer_run([&] {
            if(timer_passes<=8 || g_frame.status.state!=State::WAIT_COOLDOWN || !g_outputs.arm_applied()) return;
            if(panel.tick()) {
                check(g_frame.settings.duration_minutes==(choice==0?61:0),"real menu staged/apply/cancel contract");
                check(!g_frame.status.duration_active && !g_frame.status.delay_armed && g_frame.counters.starts==0,"editing started a timer or heater"); throw rt_fake::Done{};
            }
        });
    }
}
void editing_stop_fault_and_restart_now() {
    for(int action:{0,1,2,3,4}) { // remote STOP, FAULT, local long STOP, restart NOW, pending STOP during Apply.
        prepare(); g_initial_settings.duration_minutes=1;
        Panel panel(action>=3?std::vector<int>{7,39,39,7,7,7,39}:std::vector<int>{7,39,39,7});
        unsigned phase{}; uint32_t since{}; uint32_t minimum_remaining=60000;
        timer_run([&] {
            const auto& s=g_frame.status;
            if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied()) { fake_idf::levels[7]=0; phase=1; }
            else if(phase==1 && s.state==State::RUN_HIGH) { fake_idf::levels[7]=1; since=ms(); phase=2; }
            else if(phase==2 && ms()-since>=100) phase=3;
            else if(phase==3) {
                if(action==4 && g_frame.page==UiPage::TIMER_CONFIRM) fake_idf::levels[7]=0;
                minimum_remaining=std::min(minimum_remaining,s.duration_remaining_ms);
                if(panel.tick()) {
                    if(action==3) { check(s.duration_active && s.duration_remaining_ms>minimum_remaining+500,"explicit Apply did not restart NOW"); throw rt_fake::Done{}; }
                    if(action!=4) check(g_frame.page==UiPage::TIMER_HOURS,"did not exercise live timer editor");
                    if(action==0) runtime_remote_stop();
                    if(action==1) fake_idf::levels[5]=0;
                    if(action==2) fake_idf::levels[7]=0;
                    phase=4;
                }
            } else if(phase==4 && (s.state==State::POST_PURGE || s.state==State::LOCKOUT)) {
                safe_wait(); check(!s.duration_active && g_frame.page==UiPage::HOME,"STOP/FAULT lost to editor");
                check(g_frame.last_event.reason==(action==1?StopReason::FAULT:(action==2 || action==4)?StopReason::LOCAL_STOP:StopReason::REMOTE_STOP),"STOP/FAULT reason lost"); throw rt_fake::Done{};
            }
        });
    }
}
void core_apply_OFF_and_priority() {
    Sim s; s.in.duration_minutes=1; s.reach_run_high();
    const auto initial=s.machine.status(s.in.now_ms).duration_remaining_ms;
    s.in.duration_minutes=60; s.tick(); check(s.machine.status(s.in.now_ms).duration_remaining_ms==initial,"unconfirmed setting moved deadline");
    s.in.apply_duration=true; s.tick(); check(s.machine.status(s.in.now_ms).duration_remaining_ms==3600000,"restart NOW conversion");
    s.in.duration_minutes=0; s.tick(); check(!s.machine.status(s.in.now_ms).duration_active && s.state()==State::RUN_HIGH && s.out.fuel_command,"OFF changed fuel");
    s.in.duration_minutes=1; s.tick(); check(s.machine.status(s.in.now_ms).duration_remaining_ms==60000,"enable timer in active session");
    s.in.now_ms+=60000; s.in.duration_minutes=1440; s.tick(); check(s.state()==State::POST_PURGE && s.machine.status(s.in.now_ms).timer_expired,"Apply hid already expired deadline");
    for(bool fault:{false,true}) {
        Sim p; p.in.duration_minutes=1; p.reach_run_high(); p.in.now_ms+=60000;
        p.in.stop_active=!fault; p.in.safety_ok=!fault; p.tick();
        check(!p.machine.status(p.in.now_ms).timer_expired && !p.out.fuel_command,"STOP/FAULT lost priority to expiry");
    }
}
void NVS_history_reboot() {
    prepare(); const auto c=test_config(); bool ok{};
    // Actual v1 blob layout/hash, not a new struct disguised as legacy.
    std::array<uint32_t,6> old{1,1,22500,3000,static_cast<uint32_t>(-1250),0};
    uint32_t h=2166136261u; const auto* b=reinterpret_cast<const uint8_t*>(old.data());
    for(size_t i=0;i<20;++i) { h^=b[i]; h*=16777619u; } old[5]=h;
    fake_idf::nvs["operator_v1"]={b,b+sizeof(old)};
    auto settings=g_store.load_operator_settings(c,&ok);
    check(ok && settings.schema==kOperatorSchema && settings.setpoint_milli_c==22500 && !settings.duration_minutes && !settings.delay_minutes,"v1 migration/defaults");
    const auto legacy=fake_idf::nvs["operator_v1"];
    settings.duration_minutes=1440; settings.delay_minutes=60;
    check(g_store.save_operator_settings(settings,c),"save timer settings");
    settings=g_store.load_operator_settings(c,&ok);
    check(ok && settings.duration_minutes==1440 && settings.delay_minutes==60 && fake_idf::nvs["operator_v1"]==legacy,"v2 settings corrupt or legacy overwritten");
    auto invalid=settings; invalid.delay_minutes=1441; check(!g_store.save_operator_settings(invalid,c),"invalid saved interval");
    fake_idf::nvs["operator_v2"][0]=3; g_store.load_operator_settings(c,&ok); check(!ok,"newer record silently fell back to v1");
    CommandHistory hist; hist.begin(1);
    std::array<EventRecord,2> events{{{1,1,100,State::RUN_HIGH,State::POST_PURGE,StopReason::BOOT,0}, {1,1,200,State::RUN_HIGH,State::POST_PURGE,StopReason::TIMER,0}}};
    check(hist.restore_events(events.data(),events.size()) && g_store.save_history(hist),"append-only TIMER history");
    CommandHistory restored; restored.begin(2); check(g_store.load_history(&restored) && restored.count()==2 && restored.event(1).reason==StopReason::TIMER,"saved TIMER decode");
    for(bool active:{false,true}) {
        Sim before; before.in.delay_minutes=1; before.in.duration_minutes=1; before.boot_wait();
        if(active) { before.in.heat_enable=true; before.tick(); }
        else { before.in.arm_delayed_start=true; before.tick(); }
        Machine reboot(c); Inputs in=before.in; in.heat_enable=false; in.arm_delayed_start=false; in.now_ms=0;
        in.exhaust=in.hot_air=in.room={18000,0,SensorQuality::OK};
        const auto out=reboot.tick(in); const auto status=reboot.status(0);
        check(status.state==State::RECOVERY_PURGE && !status.delay_armed && !status.duration_active && !out.fuel_command && !out.glow_command,"reboot restored timer/RUN");
    }
}
void autosave_arm_races() {
    for(int timing:{0,1,2}) { // request during LONG qualification / after stale snapshot / on due pass
        prepare(); g_initial_settings.delay_minutes=1;
        unsigned phase{}; uint32_t pressed{};
        sirokko::UiFrame stale{};
        timer_run([&] {
            const auto& s=g_frame.status;
            if(phase==0 && timer_passes>8 && s.state==State::WAIT_COOLDOWN && g_outputs.arm_applied()) {
                stale=g_frame; fake_idf::levels[7]=0; pressed=ms(); phase=1;
            } else if(phase==1 && ((timing==0 && !s.delay_armed && ms()-pressed>=1400) || (timing!=0 && s.delay_armed))) {
                check(!stale.status.delay_armed,"race requires pre-arm storage snapshot");
                if(timing==2) jump(s.delay_remaining_ms);
                g_lease_purpose.store(FlashPurpose::AUTOSAVE); g_lease_state.store(LeaseState::REQUESTED); phase=2;
            } else if(phase==2) {
                check(g_lease_state.load()==LeaseState::REJECTED && g_lease_result.load()==ServiceResult::BUSY,"stale autosave granted flash or consumed START");
                if(timing==2) { check(s.state==State::PREPURGE && g_frame.counters.starts==1,"autosave displaced due delayed START"); throw rt_fake::Done{}; }
                g_lease_state.store(LeaseState::IDLE); phase=3;
            } else if(phase==3 && s.delay_armed) {
                check(s.delay_cancel_reason==StartBlock::NONE && g_frame.counters.starts==0,"autosave cancelled explicit arm"); safe_wait(); throw rt_fake::Done{};
            }
        });
    }
}
void menu_fields_bounds_timeout_and_fresh_gesture() {
    for(uint16_t value:{0,1,59,60,61,1439,1440}) {
        OperatorSettings settings{}; settings.duration_minutes=value;
        LocalMenu m; uint32_t now=UINT32_MAX-1000;
        auto key=[&](UiKeys k) { return m.tick(k,++now,settings,test_config()); };
        key({.mode_short=true}); key({.up_press=true}); key({.up_press=true}); key({.mode_short=true});
        check(m.page()==UiPage::TIMER_HOURS && m.staged_minutes()==value,"hours editor initial");
        key({.down_press=true});
        check(m.staged_minutes()==(value<60?value:value-60),"hours decrement altered minute field");
        for(int i=0;i<30;++i) key({.up_press=true});
        check(m.staged_minutes()==1440,"hours bound 24:00");
        key({.mode_short=true}); key({.up_press=true}); check(m.staged_minutes()==1440,"24h minutes exceeded 00");
        now+=30001; // Published menu timeout is 30s; exercise wrap as well.
        check(!key({}).settings_changed && m.page()==UiPage::HOME && settings.duration_minutes==value,"timeout applied staged value");
    }
    prepare(); Panel panel({7,39,7,7,7,39}); // delay editor, confirm unchanged nonzero delay
    g_initial_settings.delay_minutes=1; unsigned phase{}; uint32_t since{};
    timer_run([&] {
        const auto& s=g_frame.status;
        if(timer_passes<=8) return;
        if(phase==0 && g_frame.page==UiPage::TIMER_CONFIRM) {
            fake_idf::levels[7]=0; since=ms(); phase=1; // Held BEFORE Apply is not a new gesture.
        }
        if(phase<=1 && panel.tick()) phase=2;
        else if(phase==2 && ms()-since>=2000) {
            check(!s.delay_armed && g_frame.counters.starts==0,"pre-Apply held START became a new arm");
            fake_idf::levels[7]=1; since=ms(); phase=3;
        } else if(phase==3 && ms()-since>=100) { fake_idf::levels[7]=0; phase=4; }
        else if(phase==4 && s.delay_armed) { safe_wait(); throw rt_fake::Done{}; }
    });
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        {"real_app_duration_OFF_1m_1h_24h_before_at_after_wrap",duration_boundaries},
        {"real_app_duration_expiry_all_seven_active_states",duration_all_active_states},
        {"real_app_delay_1m_1h_24h_then_independent_duration_no_repeat",delayed_two_timers},
        {"real_app_fresh_blockers_and_STOP_RESET_FAULT_OTA_service_deadline_priority",delayed_cancel_and_fresh_conditions},
        {"real_app_long_cancel_hold_no_purge_and_new_explicit_rearm",delayed_long_cancel_rearm},
        {"real_app_physical_menu_staged_apply_DOWN_short_cancel",real_menu_apply_cancel},
        {"real_app_editor_STOP_FAULT_local_long_and_restart_now",editing_stop_fault_and_restart_now},
        {"core_staging_OFF_expired_apply_and_STOP_FAULT_priority",core_apply_OFF_and_priority},
        {"NVS_legacy_newer_corrupt_bounds_history_and_reboot",NVS_history_reboot},
        {"real_app_autosave_stale_snapshot_qualification_and_deadline_races",autosave_arm_races},
        {"menu_independent_fields_bounds_wrap_timeout_and_fresh_post_apply_LONG",menu_fields_bounds_timeout_and_fresh_gesture}};
    size_t passed{};
    for(const auto& [name,test]:tests) try { test(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& e) { rt_fake::tick={}; rt_fake::delay={}; fake_idf::observer={}; std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    std::cout<<"RESULT "<<passed<<'/'<<tests.size()<<" passed (production app/core/outputs; virtual monotonic time; NO physical qualification)\n";
    return passed==tests.size()?0:1;
}
