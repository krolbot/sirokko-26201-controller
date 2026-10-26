// Reuse the baseline simulator and fixtures without duplicating the production machine.
#define main baseline_main
#include "sim_tests.cpp"
#undef main
#include "sirokko/operator.hpp"
#include "sirokko/service.hpp"

namespace {
void calibration() {
    const SensorSample raw{20000,100,SensorQuality::OK};
    auto corrected=calibrated_room(raw,-1200,200,1000);
    check(corrected.milli_c==18800 && raw.milli_c==20000 && corrected.sample_ms==100,"raw/calibrated separation");
    for(auto q:{SensorQuality::MISSING,SensorQuality::CRC_ERROR,SensorQuality::TIMEOUT,SensorQuality::POWER_ON_DEFAULT,SensorQuality::HARDWARE_FAULT}) {
        auto s=raw; s.quality=q; auto c=calibrated_room(s,-10000,200,1000); check(c.quality==q && c.milli_c==s.milli_c,"offset cannot repair invalid reading");
    }
    check(calibrated_room({85000,100,SensorQuality::OK},-10000,200,1000).quality==SensorQuality::POWER_ON_DEFAULT,"85C not hidden by calibration");
    check(calibrated_room(raw,0,1101,1000).quality==SensorQuality::TIMEOUT,"stale calibration input");
    check(calibrated_room({INT32_MAX,100,SensorQuality::OK},10000,200,1000).quality==SensorQuality::IMPLAUSIBLE,"raw range before addition");
    check(calibrated_room(raw,10001,200,1000).quality==SensorQuality::IMPLAUSIBLE,"invalid offset rejected");
}
void hysteresis() {
    for(int h:{500,2000,10000}) {
        Sim s; s.reach_run_high(); s.in.hysteresis_milli_c=h;
        s.in.room.milli_c=s.in.setpoint_milli_c+h/2-1; s.tick();
        check(s.state()!=State::POST_PURGE,"one below upper boundary");
        s.in.room.milli_c++; s.tick(); check(s.state()==State::POST_PURGE,"exact upper boundary"); expect_safe(s.out);
        OperatorSettings settings{kOperatorSchema,s.in.setpoint_milli_c,h,0};
        check(room_zone({settings.setpoint_milli_c-h/2,s.in.now_ms,SensorQuality::OK},settings,s.in.now_ms,1000)==RoomZone::BELOW,"display lower boundary matches controller");
        check(room_zone({settings.setpoint_milli_c+h/2,s.in.now_ms,SensorQuality::OK},settings,s.in.now_ms,1000)==RoomZone::ABOVE,"display upper boundary matches controller");
    }
}
void qualification() {
    Config c=test_config(); c.run_low_qualified=false; Sim s(c); s.reach_run_high();
    s.in.room.milli_c=20000; s.advance(c.stage_min_ms*5);
    check(s.state()==State::RUN_HIGH && s.out.speed_permille==1000,"electrical PWM permission cannot grant low combustion");
    s.in.room.milli_c=20500; s.tick(); check(s.state()==State::POST_PURGE,"unqualified low still supports safe thermostat stop"); expect_safe(s.out);
}
void noisy_room() {
    Sim s; s.reach_run_high();
    for(unsigned i=0;i<s.config.stage_min_ms;++i) { s.in.room.milli_c=i%2?19999:20001; s.tick(); ++s.in.now_ms; check(s.state()==State::RUN_HIGH,"minimum stage time"); }
    s.in.room.milli_c=20000; s.tick(); check(s.state()==State::RUN_LOW,"qualified lower stage");
    for(unsigned i=0;i<100;++i) { s.in.room.milli_c=i%2?19999:20001; s.tick(); ++s.in.now_ms; check(s.state()==State::RUN_LOW && s.out.fuel_command,"noise may not cycle clutch or stages"); }
}
void no_thermostat_restart() {
    Sim s; s.reach_run_high(); s.in.room.milli_c=20500; s.tick();
    s.in.exhaust.milli_c=s.in.hot_air.milli_c=20000; s.in.room.milli_c=18000;
    s.advance(s.config.postpurge_min_ms+s.config.restart_inhibit_ms+100);
    check(s.state()==State::WAIT_COOLDOWN,"held session does not restart after thermostat stop");
    s.in.heat_enable=false; s.tick(); s.in.heat_enable=true; s.tick(); check(s.state()==State::PREPURGE,"new manual START");
}
void service_states() {
    for(auto state:{State::BOOT_SELF_TEST,State::RECOVERY_PURGE,State::WAIT_COOLDOWN,State::PREPURGE,State::PREHEAT,State::SPINUP,State::IGNITION_TRIAL,State::FLAME_CONFIRM,State::RUN_HIGH,State::RUN_LOW,State::POST_PURGE,State::LOCKOUT}) {
        Sim s; drive_to_state(s,state); ServiceLease lease;
        const auto r=lease.acquire(s.machine.status(s.in.now_ms),s.in,s.config,true);
        check((r==ServiceResult::OK)==(state==State::WAIT_COOLDOWN),"OTA eligibility in every state");
        if(r==ServiceResult::OK) { s.in.maintenance_inhibit=true; s.in.heat_enable=true; s.tick(); expect_safe(s.out); check(s.state()==State::WAIT_COOLDOWN,"START cannot cross OTA lease"); check(lease.acquire(s.machine.status(s.in.now_ms),s.in,s.config,true)==ServiceResult::BUSY,"no nested leases"); }
    }
    Sim s; s.boot_wait(); ServiceLease lease; auto status=s.machine.status(s.in.now_ms);
    s.in.exhaust.milli_c=s.config.cool_exhaust_milli_c+1; check(lease.acquire(status,s.in,s.config,true)==ServiceResult::HOT,"hot OTA");
    s.in.exhaust.quality=SensorQuality::CRC_ERROR; check(lease.acquire(status,s.in,s.config,true)==ServiceResult::SENSOR,"invalid OTA sensor");
}
void counters_history() {
    CommandHistory h; h.begin(7,{UINT64_MAX,0,UINT64_MAX-2,UINT64_MAX-2});
    Status s{}; s.state=State::PREPURGE; h.observe(s,UINT32_MAX-10ULL); check(h.counters().starts==UINT64_MAX && h.session_id()==1,"saturating start counter");
    s.state=State::RUN_HIGH; s.outputs.glow_command=true; h.observe(s,UINT32_MAX+10ULL);
    h.observe(s,UINT32_MAX+20ULL); check(h.counters().run_ms==UINT64_MAX && h.counters().glow_ms==UINT64_MAX,"64bit accounting across uptime wrap and saturation");
    s.state=State::LOCKOUT; s.faults=FAULT_IGNITION_NOT_PROVEN; h.observe(s,UINT32_MAX+30ULL); h.observe(s,UINT32_MAX+40ULL);
    check(h.counters().failed_ignitions==1,"one failed ignition, not one per tick");
    for(unsigned i=0;i<100;++i) { s.state=i%2?State::WAIT_COOLDOWN:State::PREPURGE; h.observe(s,UINT32_MAX+50ULL+i); }
    check(h.count()==kEventCapacity && h.event(0).boot_id==7 && h.event(23).uptime_ms>UINT32_MAX,"bounded monotonic event ring");
    const auto event=h.event(23); h.reset_counters(); check(h.counters().starts==0 && h.event(23).faults==event.faults,"counter reset does not clear fault history");
}
void persisted_operator() {
    fake_idf::reset(); CommissioningStore store; check(store.begin(),"NVS begin"); const auto c=test_config();
    OperatorSettings settings{kOperatorSchema,22500,3000,-1250}; check(store.save_operator_settings(settings,c),"operator save");
    bool integrity{}; auto loaded=store.load_operator_settings(c,&integrity); check(integrity && loaded.room_offset_milli_c==-1250 && loaded.hysteresis_milli_c==3000,"operator readback");
    auto invalid=settings; invalid.hysteresis_milli_c=0; check(!store.save_operator_settings(invalid,c),"operator bounds persisted");
    fake_idf::nvs["operator_v2"].back()^=1; store.load_operator_settings(c,&integrity); check(!integrity,"corrupt settings cannot silently default and START");
    CommandCounters counters{4,2,123,456}; check(store.save_counters(counters),"save counters"); CommandCounters out{}; check(store.load_counters(&out) && out.glow_ms==456,"read counters");
    fake_idf::nvs["counters_v1"][0]=2; check(!store.load_counters(&out),"newer counter schema not erased");
}
void legacy_schema_preserved() {
    fake_idf::reset(); CommissioningStore store;
    std::vector<uint8_t> old(offsetof(Config,profile_revision)+8,0x33); auto original=old;
    fake_idf::nvs["config"]=old; bool integrity{}; auto c=store.load_config(&integrity);
    check(!integrity && !c.commissioning_accepted && fake_idf::nvs["config"]==original,"legacy blob preserved but never accepted");
    fake_idf::nvs["boot_count"]=std::vector<uint8_t>(4,0xff); check(!store.begin(),"boot ID never wraps and reuses identity");
}
void history_restoration() {
    fake_idf::reset(); CommissioningStore store; check(store.begin(),"history store");
    CommandHistory first; first.begin(7); Status s{}; s.state=State::IGNITION_TRIAL; first.observe(s,20);
    s.state=State::LOCKOUT; s.faults=FAULT_IGNITION_NOT_PROVEN; first.observe(s,30); check(first.counters().failed_ignitions==1,"first failure");
    check(store.save_history(first),"history save");
    CommandHistory reboot; reboot.begin(8,first.counters()); check(store.load_history(&reboot),"restore ring"); reboot.observe(s,0);
    check(reboot.count()==3 && reboot.event(0).boot_id==7 && reboot.event(2).boot_id==8 && reboot.counters().failed_ignitions==1,"boot restored failure counted twice or history lost");
    auto& history_blob=fake_idf::nvs["history_v1"]; history_blob[history_blob.size()/2]^=1; auto saved=history_blob;
    check(!store.load_history(&reboot) && saved==fake_idf::nvs["history_v1"],"corrupt history overwritten");
}
void countdown() {
    Sim s; s.start_to(State::PREHEAT); auto status=s.machine.status(s.in.now_ms);
    check(status.stage_timed && status.stage_remaining_ms==s.config.preheat_ms,"remaining stage time");
    s.advance(1); check(s.machine.status(s.in.now_ms).stage_remaining_ms==s.config.preheat_ms-1,"remaining decrements");
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        {"room_raw_calibration_quality_and_staleness",calibration},{"thermostat_hysteresis_boundaries",hysteresis},
        {"low_combustion_requires_explicit_qualification",qualification},{"room_noise_keeps_continuous_clutch",noisy_room},
        {"thermostat_stop_never_auto_reignites",no_thermostat_restart},{"OTA_lease_all_machine_states_and_START_race",service_states},
        {"command_counters_ring_wrap_saturation_reset",counters_history},{"real_NVS_operator_counters_corruption",persisted_operator},
        {"legacy_NVS_and_boot_identity_fail_closed",legacy_schema_preserved},{"history_across_reboot_without_duplicate_failures",history_restoration},{"real_stage_countdown",countdown}
    };
    size_t passed{};
    for(const auto& [name,test]:tests) try { test(); ++passed; std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    std::cout<<"RESULT "<<passed<<'/'<<tests.size()<<" passed\n"; return passed==tests.size()?0:1;
}
