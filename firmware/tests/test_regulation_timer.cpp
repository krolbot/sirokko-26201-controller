#include "sirokko/control.hpp"
#include "sirokko/regulator.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
using namespace sirokko;

// Deliberately synthetic millisecond-scale process fixture. NOT heater settings.
static ProcessProfile synthetic_profile() {
    ProcessProfile p{};
    p.qualified=true;
    p.recovery_purge_ms=100; p.prepurge_ms=10; p.preheat_ms=10;
    p.ignition_ms=10; p.verify_ms=10; p.postpurge_ms=20;
    p.maximum_purge_exit_exhaust_milli_c=150000; p.maximum_purge_exit_air_milli_c=120000;
    p.maximum_exhaust_milli_c=500000; p.maximum_air_milli_c=400000;
    p.minimum_ignition_rise_milli_c=20000; p.minimum_ignition_air_rise_milli_c=10000;
    p.minimum_run_exhaust_milli_c=110000; p.purge_speed_code=2000; p.run_speed_code=3000;
    p.calibration_floor_speed_code=1000;
    return p;
}
static Sensors readings(uint64_t now,int room=15000,int exh=130000) {
    return {{exh,now,0,true},{exh*4/5,now,0,true},{room,now,0,true}};
}
static TickInput observation(uint64_t now,int room=15000,int exh=130000) {
    TickInput i{};i.now_ms=now;i.sensors=readings(now,room,exh);return i;
}
static uint64_t start_running(Controller& c) {
    auto i=observation(100,15000,100000); assert(c.tick(i).state==State::IDLE);
    i=observation(101,15000,100000);i.start=true;assert(c.tick(i).state==State::PREPURGE);
    i=observation(111,15000,100000);assert(c.tick(i).state==State::PREHEAT);
    i=observation(121,15000,100000);assert(c.tick(i).state==State::IGNITION);
    i=observation(131);assert(c.tick(i).state==State::VERIFY);
    i=observation(141);assert(c.tick(i).state==State::RUN);return 141;
}
static void range_for(Controller& c) {
    const auto p=c.process_profile();
    assert(c.install_range({true,1500,3000,0.005,0.005,process_fingerprint(p)}));
}
static void test_regulator() {
    Regulator r;r.reset(0,10,0.2);
    auto v=r.tick(1000,10,20,false,0.2,0.005,0.003);
    assert(std::abs(v.fraction-0.2)<1e-12);
    v=r.tick(2000,10,20,true,0.2,0.005,0.003);
    assert(std::abs(v.fraction-0.2)<1e-12 && v.integral<0);
    double old=v.fraction;
    for(uint64_t t=2100;t<3000000;t+=100) {
        const bool high=t<1000000;
        v=r.tick(t,high?0:40,high?30:15,true,0.0,0.005,0.003);
        assert(std::isfinite(v.integral));
        assert(v.fraction-old<=0.005*0.1+1e-12);
        assert(old-v.fraction<=0.003*0.1+1e-12);
        assert(v.fraction>=0 && v.fraction<=1);
        old=v.fraction;
    }
    assert(v.fraction<0.01);
    const auto before=v;
    v=r.tick(10,20,20,true,0,0.005,0.005);
    assert(v.fraction==before.fraction && v.integral==before.integral);
    v=r.tick(4000000,std::numeric_limits<double>::quiet_NaN(),20,true,0,0.005,0.005);
    assert(v.fraction==before.fraction);
    std::cout<<"PASS regulator: signed I, bumpless manual-auto, both slew limits, saturation reversal, backward time, NaN\n";
}
static void test_continuous_burning_and_manual() {
    Controller c(synthetic_profile(),{},1);range_for(c);start_running(c);
    Snapshot s;
    for(uint64_t now=161;now<400000;now+=20) {
        auto i=observation(now,35000);s=c.tick(i);
        assert(s.state==State::RUN && s.outputs.fuel && !s.outputs.glow);
        assert(s.outputs.speed_code>=1500 && s.outputs.speed_code<=3000);
    }
    assert(s.outputs.speed_code<1510 && s.room_excess);
    auto settings=c.settings();settings.mode=Mode::MANUAL;assert(c.update_settings(settings));
    assert(c.settings().manual_percent==50); // capture current command, not an old 100% request
    settings=c.settings();settings.manual_percent=100;assert(c.update_settings(settings));
    auto i=observation(400001,35000);s=c.tick(i);assert(s.outputs.speed_code<1520);
    i=observation(400021);i.sensors.room.valid=false;s=c.tick(i);
    assert(s.state==State::RUN && s.outputs.fuel); // manual has no required room loop
    settings=c.settings();settings.mode=Mode::TEMPERATURE;assert(c.update_settings(settings));
    i=observation(400041);i.sensors.room.valid=false;s=c.tick(i);
    assert(s.state==State::FAULT_PURGE && !s.outputs.fuel && (s.faults&FAULT_ROOM));
    i=observation(400061);i.sensors.room.valid=false;s=c.tick(i);
    assert(s.state==State::FAULT); // broken ROOM cannot prevent thermally complete cooling
    std::cout<<"PASS regulation: no thermostat extinguishing, minimum bound, mode transfer, room failure independence\n";
}
static void test_shutdown_timer() {
    Controller c(synthetic_profile(),{},1);start_running(c);
    assert(!c.snapshot(141).duration_active);
    auto i=observation(161);i.set_shutdown_timer=true;i.shutdown_after_ms=100;
    auto s=c.tick(i);assert(s.duration_active && s.duration_remaining_ms==100);
    auto settings=c.settings();settings.duration_ms=9999;assert(c.update_settings(settings));
    i=observation(260);s=c.tick(i);assert(s.duration_remaining_ms==1);
    i=observation(261);i.set_shutdown_timer=true;i.shutdown_after_ms=2000;i.start=true;
    s=c.tick(i);assert(s.state==State::STOP_PURGE && !s.outputs.fuel && s.outputs.fan_request);
    assert(!s.duration_active); // cannot extend at/after the expired deadline
    i=observation(281);assert(c.tick(i).state==State::IDLE);
    Controller cancel(synthetic_profile(),{},2);start_running(cancel);
    i=observation(161);i.set_shutdown_timer=true;i.shutdown_after_ms=100;cancel.tick(i);
    i=observation(181);i.set_shutdown_timer=true;i.shutdown_after_ms=0;cancel.tick(i);
    i=observation(261);s=cancel.tick(i);assert(s.state==State::RUN && !s.duration_active);
    i=observation(281);i.set_shutdown_timer=true;i.shutdown_after_ms=1;i.stop=true;
    s=cancel.tick(i);assert(s.state==State::STOP_PURGE && !s.duration_active);
    Controller reset(synthetic_profile(),settings,3);
    s=reset.snapshot(0);assert(s.state==State::RECOVERY_PURGE && !s.duration_active && !s.delayed_armed);
    std::cout<<"PASS shutdown: disabled default, arm, remaining, immutable preset, exact expiry, priority, cancel, reset\n";
}
static void test_limits_and_ambiguity() {
    auto p=synthetic_profile();assert(valid_process_profile(p));
    p.run_speed_code=4015;assert(!valid_process_profile(p));p=synthetic_profile();
    p.minimum_ignition_air_rise_milli_c=0;assert(!valid_process_profile(p));
    p=synthetic_profile();Controller c(p,{},4);
    auto i=observation(100,15000,100000);c.tick(i);
    i=observation(101,15000,200000);i.start=true;
    auto s=c.tick(i);assert(s.state==State::IDLE && !s.outputs.fuel); // hot residual is not a new ignition
    i=observation(102,15000,100000);i.start=true;c.tick(i);
    i=observation(112,15000,100000);c.tick(i);i=observation(122,15000,100000);c.tick(i);
    i=observation(132,15000,130000);i.sensors.air.milli_c=80000;
    s=c.tick(i);assert(s.state==State::FAULT_PURGE && !s.outputs.fuel); // EXH rise alone is insufficient
    Controller unchanged(p,{},5);start_running(unchanged);
    i=observation(161);i.sensors.exhaust.milli_c=600000;
    s=unchanged.tick(i);assert((s.faults&FAULT_OVER_TEMPERATURE) && !s.outputs.fuel);
    auto bad=Settings{};bad.room_setpoint_milli_c=20001;assert(!valid_settings(bad));
    bad={};bad.slew_up_per_s=std::numeric_limits<double>::quiet_NaN();assert(!valid_settings(bad));
    Controller overflow(p,{},6);i=observation(UINT64_MAX-100,15000,100000);overflow.tick(i);
    i=observation(UINT64_MAX-50,15000,100000);i.arm_delayed=true;i.delay_ms=100;
    s=overflow.tick(i);assert(!s.delayed_armed);
    auto range=OperatingRange{true,1500,3000,0.005,0.005,process_fingerprint(p)};
    Controller fresh(p,{},7);range.process_fingerprint^=1;assert(!fresh.install_range(range));
    range.process_fingerprint=process_fingerprint(p);assert(fresh.install_range(range));
    i=observation(100,15000,100000);fresh.tick(i);
    auto p2=p;p2.calibration_floor_speed_code=1100;assert(fresh.update_process_profile(p2,101));
    assert(!fresh.operating_range().valid);
    std::cout<<"PASS limits: physical-profile placeholder rejected, raw 600 C, hot restart, two-channel evidence, settings, overflow, profile binding\n";
}
int main() {test_regulator();test_continuous_burning_and_manual();test_shutdown_timer();test_limits_and_ambiguity();}
