// Real production button/core/output/app code; GPIO/RTOS/latch are explicitly host models.
#define SIROKKO_FOUR_BUTTON_HOST_TEST
#include "runtime_tests.cpp"
#include <fstream>
namespace {
void gestures() {
    for (uint32_t origin : {0u, UINT32_MAX-100u}) {
        StartStopButton b;
        auto step=[&](bool high,uint32_t t,StartStopContext c=StartStopContext::START) { return b.update(high,origin+t,c); };
        check(step(true,0)==ButtonGesture::NONE && step(true,30)==ButtonGesture::NONE,"released qualification");
        step(false,40); step(true,55); step(false,60); // Contact bounce restarts debounce.
        check(step(false,89)==ButtonGesture::NONE && step(false,90)==ButtonGesture::NONE,"30ms debounce");
        step(true,150); check(step(true,179)==ButtonGesture::NONE && step(true,180)==ButtonGesture::SHORT,"short only on stable release");
        step(false,200); step(false,230);
        check(step(false,1729)==ButtonGesture::NONE && step(false,1730)==ButtonGesture::LONG,"1500ms accepted hold boundary/wrap");
        for(uint32_t t=1750;t<4000;t+=20) check(step(false,t)==ButtonGesture::NONE,"one long per hold");
        step(true,4000); check(step(true,4030)==ButtonGesture::NONE,"no short after long");
        step(false,4100); step(false,4130); step(false,4200,StartStopContext::BLOCKED);
        check(step(false,6000)==ButtonGesture::NONE,"inhibit transition cannot queue start");
        step(true,6010); step(true,6040); step(false,6100); step(false,6130);
        check(step(false,7630)==ButtonGesture::LONG,"fresh gesture after inhibit");
    }
}
void held_boot_and_role_change() {
    for(auto context:{StartStopContext::BLOCKED,StartStopContext::START,StartStopContext::STOP}) {
        StartStopButton b;
        for(uint32_t t=0;t<4000;t+=10) check(b.update(false,t,context)==ButtonGesture::NONE,"boot-held gesture");
        b.update(true,4000,StartStopContext::START); b.update(true,4030,StartStopContext::START);
        b.update(false,4040,StartStopContext::START); b.update(false,4070,StartStopContext::START);
        check(b.update(false,5570,StartStopContext::START)==ButtonGesture::LONG,"new boot gesture");
        check(b.update(false,5600,StartStopContext::STOP)==ButtonGesture::NONE,"START cannot become STOP in same hold");
        check(b.update(false,8000,StartStopContext::STOP)==ButtonGesture::NONE,"no repeat after role change");
    }
}
void od_sequence() {
    fake_idf::reset(); OutputDriver d; check(d.initialize(test_config()),"OD initialize");
    check(fake_idf::modes[8]==GPIO_MODE_INPUT_OUTPUT_OD && fake_idf::levels[8]==1,"input-enabled open drain/released");
    check(std::find(fake_idf::trace.begin(),fake_idf::trace.end(),"gpio_level:8:0")==fake_idf::trace.end(),"no software LOW during init");
    Outputs out{}; out.arm_ready=true; out.watchdog_heartbeat=true;
    fake_idf::trace.clear(); check(d.apply(out),"OD request LOW");
    check(fake_idf::levels[8]==0 && fake_idf::levels[41]==0 && fake_idf::levels[42]==1 && fake_idf::levels[18]==0 && !d.arm_applied(),"settle pass must remain inhibited");
    check(std::find(fake_idf::trace.begin(),fake_idf::trace.end(),"gpio_read:8")==fake_idf::trace.end(),"no immediate LOW confirmation");
    check(d.apply(out) && d.arm_applied() && fake_idf::levels[41]==1 && fake_idf::levels[42]==0,"next pass input LOW before ARM");
    check(fake_idf::trace.back()=="gpio_level:18:1","heartbeat last");
    fake_idf::forced_pad[8]=1;
    check(!d.apply(out) && !d.healthy() && !d.arm_applied(),"real HIGH must override stale output LOW"); gpio_off();
    check(fake_idf::levels[8]==1 && fake_idf::levels[42]==1 && !d.apply(out),"latched failure/released output");
}
void od_failures() {
    for(const char* call:{"gpio_level:8:1","gpio_level:8:0","gpio_level:41:1","gpio_level:42:0","gpio_level:18:1"}) {
        fake_idf::reset(); OutputDriver d; check(d.initialize(test_config()),"init before API injection");
        Outputs out{}; out.arm_ready=true; out.watchdog_heartbeat=true;
        if(std::string(call)=="gpio_level:8:1") out.arm_ready=false;
        fake_idf::failing_call=call;
        const bool first=d.apply(out);
        check(!first || !d.apply(out),"API failure not propagated");
        check(!d.healthy() && !d.arm_applied(),"API failure not latched"); gpio_off();
    }
    fake_idf::reset(); fake_idf::forced_pad[8]=0; OutputDriver d;
    check(!d.initialize(test_config()),"released stuck LOW must fail init"); gpio_off();
    for(bool watchdog:{false,true}) for(bool thermal:{false,true}) for(bool arm:{false,true}) {
        HardwareSafetyLatch latch; latch.watchdog_ok=watchdog; latch.thermal_ok=thermal; latch.stop_ok=true;
        Outputs out{}; out.arm_ready=arm; latch.reset_rising_edge(out);
        check(latch.safety_ok(out)==(watchdog && thermal && arm),"stuck GPIO8 LOW cannot override independent barriers");
    }
}
void reset_gate_and_recovery() {
    Sim s; s.in.safety_ok=false; s.in.arm_applied=false; s.in.reset_request=true;
    s.tick(); s.advance(s.config.recovery_purge_min_ms);
    check(s.state()==State::LOCKOUT && s.machine.status(s.in.now_ms).recovery_complete && s.out.arm_ready,"recovery must not wait for own GPIO8/SAFETY_OK");
    s.in.arm_applied=true; s.in.safety_ok=true; s.advance(100); s.in.reset_request=false; s.tick();
    check(s.state()==State::LOCKOUT,"held RESET before readiness must not qualify");
    s.in.start_pressed=true; s.in.reset_request=true; s.tick(); s.in.reset_request=false; s.tick();
    check(s.state()==State::LOCKOUT,"RESET with held START");
    s.in.start_pressed=false; s.in.reset_request=true; s.tick(); s.in.reset_request=false; s.tick();
    check(s.state()==State::WAIT_COOLDOWN && !s.out.fuel_command,"new manual RESET, no automatic ignition");
    Sim active; active.reach_run_high(); active.in.output_ok=false; active.tick(); active.in.output_ok=true;
    active.in.heat_enable=false; active.in.exhaust.milli_c=20000; active.in.reset_request=true; active.tick();
    check(active.state()==State::LOCKOUT && !active.out.arm_ready,"RESET cannot truncate mandatory purge");
}
void legacy_acceptance() {
    fake_idf::reset(); CommissioningStore store;
    // Bytes emitted by the real schema-3 CommissioningStore at baseline 27bc2ea (see fixture provenance).
    std::ifstream f("tests/fixtures/accepted-draft05-config.bin",std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    check(!bytes.empty(),"missing actual legacy fixture"); fake_idf::nvs["config"]=bytes;
    bool integrity{}; const auto c=store.load_config(&integrity);
    check(!c.commissioning_accepted && fake_idf::nvs["config"]==bytes,"accepted legacy NVS auto migrated or erased");
    OutputDriver d; check(!d.initialize(c) && fake_idf::levels[8]==1,"legacy profile drove GPIO8 LOW");
    auto candidate=test_config(); candidate.hardware_contract_id=5;
    check(!store.accept_config(candidate,store.candidate_token(candidate)),"legacy hardware ID accepted");
    candidate.hardware_contract_id=6; candidate.commissioning_accepted=false;
    check(store.accept_config(candidate,store.candidate_token(candidate)),"explicit new draft06 acceptance");
}
void short_UI_only() {
    prepare(); unsigned phase{}; uint32_t since{};
    run([&] {
        check(g_frame.status.state!=State::PREPURGE,"short press started heater");
        if(phase==0 && ms()>=160) { fake_idf::levels[7]=0; since=ms(); phase=1; }
        else if(phase==1 && ms()-since>=100) { fake_idf::levels[7]=1; phase=2; }
        else if(phase==2 && g_frame.page==UiPage::SETTINGS) { check(off(g_frame.status.outputs),"short enabled fuel"); throw rt_fake::Done{}; }
    });
}
// Longer real stages let a 1500ms STOP gesture finish within every target stage.
void long_STOP_stage_matrix() {
    for(auto target:{State::PREPURGE,State::PREHEAT,State::SPINUP,State::IGNITION_TRIAL,State::FLAME_CONFIRM,State::RUN_HIGH,State::RUN_LOW}) {
        prepare(); g_config.prepurge_ms=g_config.preheat_ms=g_config.spinup_ms=2200;
        g_config.ignition_window_ms=g_config.flame_confirm_ms=2200; g_config.glow_max_ms=10000;
        unsigned phase{}; uint32_t since{}; bool reached{};
        auto inspect=[&] {
            const auto state=g_frame.status.state;
            if(ms()>40000) throw std::runtime_error(std::string("long STOP stage timeout: ")+state_name(target)+" actual="+state_name(state));
            g_display_progress.store(ms());
            if(phase==0 && ms()>=160) { fake_idf::levels[7]=0; phase=1; }
            if(phase==1 && state!=State::WAIT_COOLDOWN && state!=State::RECOVERY_PURGE) { fake_idf::levels[7]=1; since=ms(); phase=2; }
            if(phase==2 && state==target && ms()-since>=80) { fake_idf::levels[7]=0; phase=3; reached=true; }
            if(phase==3 && state==State::POST_PURGE) {
                check(off(g_frame.status.outputs) && g_frame.status.faults==0 && fake_idf::levels[8]==0,"normal STOP fabricated fault or lost OD policy");
                check(g_frame.status.outputs.fan_request,"long STOP lost purge"); throw rt_fake::Done{};
            }
            SensorFrame frame{{target==State::RUN_LOW && (state==State::RUN_HIGH || state==State::RUN_LOW)?20000:18000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},{20000,ms(),SensorQuality::OK},0,0};
            if((state==State::IGNITION_TRIAL && target!=State::IGNITION_TRIAL) || state==State::FLAME_CONFIRM || state==State::RUN_HIGH || state==State::RUN_LOW) frame.exhaust.milli_c=110000;
            xQueueOverwrite(g_sensor_queue,&frame);
        };
        rt_fake::tick=inspect; inspect();
        try { control_task(nullptr); } catch(const rt_fake::Done&) {}
        rt_fake::tick={}; check(reached,"long STOP target not exercised");
    }
}
void START_priority_matrix() {
    for(int blocker:{0,1,2,3}) {
        prepare(); unsigned phase{}; uint32_t since{};
        run([&] {
            check(g_frame.status.state!=State::PREPURGE,"START defeated same-pass inhibit");
            if(phase==0 && ms()>=160) { fake_idf::levels[7]=0; since=ms(); phase=1; }
            // Inject before the pass that would reach the long threshold (press debounces at +40).
            if(phase==1 && ms()-since>=1500) {
                if(blocker==0) fake_idf::levels[6]=0;
                if(blocker==1) fake_idf::levels[5]=0;
                if(blocker==2) g_ota_pending.store(true);
                if(blocker==3) { g_lease_purpose.store(FlashPurpose::OTA); g_lease_state.store(LeaseState::REQUESTED); }
                since=ms(); phase=2;
            } else if(phase==2 && ms()-since>=1800) {
                check(off(g_frame.status.outputs),"inhibit allowed fuel");
                if(blocker==2) g_ota_pending.store(false);
                if(blocker==3) g_lease_state.store(LeaseState::RELEASE);
                phase=3; since=ms();
            } else if(phase==3 && ms()-since>=1800) throw rt_fake::Done{};
        });
    }
}
void runtime_recovery_and_held_RESET() {
    prepare(); fake_idf::levels[5]=0; fake_idf::levels[6]=0;
    HardwareSafetyLatch latch; unsigned phase{}; uint32_t since{};
    run([&] {
        const auto state=g_frame.status.state;
        check(off(g_frame.status.outputs),"RESET must not initiate combustion");
        if(phase==0 && ms()>=300) { check(state==State::LOCKOUT && g_outputs.arm_applied(),"recovery deadlock"); fake_idf::levels[6]=1; since=ms(); phase=1; }
        else if(phase==1 && ms()-since>=100) {
            check(state==State::LOCKOUT,"held boot RESET accepted");
            fake_idf::levels[6]=0; latch.reset_rising_edge(g_outputs.applied(),true);
            fake_idf::levels[5]=latch.safety_ok(g_outputs.applied()); since=ms(); phase=2;
        } else if(phase==2 && ms()-since>=100) { fake_idf::levels[6]=1; phase=3; }
        else if(phase==3 && state==State::WAIT_COOLDOWN) throw rt_fake::Done{};
    });
}
void runtime_held_boot_START_and_new_gesture() {
    prepare(); g_config.recovery_purge_min_ms=2000; fake_idf::levels[7]=0;
    unsigned phase{}; uint32_t since{};
    run([&] {
        if(phase<2) check(g_frame.status.state!=State::PREPURGE,"held START crossed recovery");
        if(phase==0 && ms()>=4000) { fake_idf::levels[7]=1; phase=1; since=ms(); }
        else if(phase==1 && ms()-since>=100) { fake_idf::levels[7]=0; phase=2; }
        else if(phase==2 && g_frame.status.state==State::PREPURGE) throw rt_fake::Done{};
    });
}
void runtime_held_STOP_cannot_restart_after_purge() {
    prepare(); g_config.postpurge_min_ms=2000;
    unsigned phase{}; uint32_t since{};
    run([&] {
        const auto state=g_frame.status.state;
        if(phase==0 && ms()>=160) { fake_idf::levels[7]=0; phase=1; }
        else if(phase==1 && state==State::RUN_HIGH) { fake_idf::levels[7]=1; phase=2; since=ms(); }
        else if(phase==2 && ms()-since>=100) { fake_idf::levels[7]=0; phase=3; }
        else if(phase==3 && state==State::POST_PURGE) { since=ms(); phase=4; }
        else if(phase==4) {
            check(off(g_frame.status.outputs) && g_frame.status.faults==0,"normal STOP must not fault");
            if(ms()-since<2000) check(state==State::POST_PURGE,"long hold truncated postpurge");
            if(ms()-since>=4000) { check(state==State::WAIT_COOLDOWN,"held STOP restarted"); fake_idf::levels[7]=1; since=ms(); phase=5; }
        } else if(phase==5 && ms()-since>=100) { fake_idf::levels[7]=0; phase=6; }
        else if(phase==6 && state==State::PREPURGE) throw rt_fake::Done{};
    });
}
void runtime_GPIO8_fault_is_latched() {
    prepare(); bool injected{};
    run([&] {
        if(g_frame.status.state==State::WAIT_COOLDOWN && ms()>=160) fake_idf::levels[7]=0;
        if(!injected && g_frame.status.state==State::RUN_HIGH) { fake_idf::forced_pad[8]=1; injected=true; }
        else if(injected) {
            check(g_frame.status.state==State::LOCKOUT && (g_frame.status.faults & FAULT_OUTPUT),"GPIO8 failure not routed to core");
            check(!g_outputs.healthy() && off(g_frame.status.outputs) && fake_idf::levels[41]==0 && fake_idf::levels[42]==1 && fake_idf::levels[18]==0,"GPIO8 failure kept permission");
            throw rt_fake::Done{};
        }
    });
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> cases={
        {"gesture_short_long_bounce_repeat_wrap",gestures},{"gesture_boot_hold_inhibit_role_change",held_boot_and_role_change},
        {"GPIO8_released_init_settle_real_pad_heartbeat_order",od_sequence},{"GPIO8_API_and_stuck_LOW_independent_barriers",od_failures},
        {"recovery_no_deadlock_RESET_START_and_purge_gates",reset_gate_and_recovery},{"actual_draft05_NVS_rejected_new_acceptance",legacy_acceptance},
        {"real_app_short_is_UI_only",short_UI_only},{"real_app_long_STOP_all_seven_active_stages",long_STOP_stage_matrix},
        {"real_app_START_loses_to_RESET_FAULT_OTA_service",START_priority_matrix},{"real_app_recovery_held_RESET_new_physical_edge",runtime_recovery_and_held_RESET},
        {"real_app_held_boot_START_requires_new_gesture",runtime_held_boot_START_and_new_gesture},
        {"real_app_held_STOP_preserves_purge_no_restart",runtime_held_STOP_cannot_restart_after_purge},
        {"real_app_GPIO8_pad_failure_latches_output_fault",runtime_GPIO8_fault_is_latched}};
    unsigned passed{};
    for(const auto& [name,test]:cases) try { test(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception& e) { rt_fake::tick={}; rt_fake::delay={}; fake_idf::observer={}; std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    std::cout<<"RESULT "<<passed<<'/'<<cases.size()<<" passed (DRAFT-06 host models, NOT physical qualification)\n";
    return passed==cases.size()?0:1;
}
