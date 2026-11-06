#include "sirokko/control.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace sirokko;

// Every temperature and millisecond process value below belongs to a SYNTHETIC
// plant. No test fixture is deployed as heater process configuration.
static ProcessProfile fixture() {
    ProcessProfile p{};p.qualified=true;
    p.recovery_purge_ms=100;p.prepurge_ms=10;p.preheat_ms=10;
    p.ignition_ms=10;p.verify_ms=10;p.postpurge_ms=20;
    p.maximum_purge_exit_exhaust_milli_c=60000;p.maximum_purge_exit_air_milli_c=50000;
    p.maximum_exhaust_milli_c=500000;p.maximum_air_milli_c=250000;
    p.minimum_ignition_rise_milli_c=10000;p.minimum_ignition_air_rise_milli_c=5000;
    p.minimum_run_exhaust_milli_c=110000;
    p.purge_speed_code=1400;p.run_speed_code=1000;p.calibration_floor_speed_code=400;
    return p;
}
static TickInput input(uint64_t now,int exh=20000,int air=20000) {
    TickInput i{};i.now_ms=now;i.sensors={{exh,now,0,true},{air,now,0,true},{20000,now,0,true}};return i;
}
static void ignite(Controller& c) {
    assert(c.tick(input(100)).state==State::IDLE);
    auto i=input(101);i.start_calibration=true;
    auto s=c.tick(i);assert(s.state==State::PREPURGE && s.calibration_active);
    assert(c.tick(input(111)).state==State::PREHEAT);
    assert(c.tick(input(121)).state==State::IGNITION);
    assert(c.tick(input(131,270000,70000)).state==State::VERIFY);
    s=c.tick(input(141,270000,70000));
    assert(s.state==State::RUN && s.calibration_active);
    assert(!c.update_settings(Settings{}));
}
static void success_and_cooldown_storage() {
    Controller c(fixture(),{},1);ignite(c);
    double exh=270000,air=70000;
    auto i=input(141,270000,70000);
    Snapshot s=c.snapshot(141);
    bool lower=false,opposite=false,pending=false;
    OperatingRange candidate{};
    uint64_t final_time=141;
    for(uint64_t t=161;t<21600000;t+=20) {
        i.now_ms=t;
        const double te=s.outputs.fuel?160000+110.0*s.outputs.speed_code:20000;
        const double ta=s.outputs.fuel?70000+15.0*(1000-s.outputs.speed_code):20000;
        exh+=(te-exh)*(-std::expm1(-0.02/5.0));
        air+=(ta-air)*(-std::expm1(-0.02/5.0));
        if(t-i.sensors.exhaust.sample_ms>=200) {
            i.sensors=input(t,static_cast<int>(std::lround(exh)),static_cast<int>(std::lround(air))).sensors;
        }
        const auto previous=s;
        s=c.tick(i);
        if(s.state==State::RUN) {
            assert(s.outputs.fuel && !s.outputs.glow);
            assert(s.outputs.speed_code>=400 && s.outputs.speed_code<=1000);
            assert(std::abs(static_cast<int>(s.outputs.speed_code)-previous.outputs.speed_code)<=1);
            lower|=s.outputs.speed_code<1000;
            opposite|=exh<265000 && air>71000;
        }
        if(s.state==State::FAULT_PURGE || s.state==State::FAULT) {
            std::cerr<<"unexpected calibration failure "<<s.calibration_reason<<" at "<<t<<"\n";
            assert(false);
        }
        if(s.state==State::STOP_PURGE) {
            pending=true;
            assert(!s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
            assert(!c.pending_calibration_range(&candidate));
            assert(!c.operating_range().valid); // successful analysis is not yet a persisted range
        }
        if(pending && s.state==State::IDLE) {final_time=t;break;}
    }
    assert(lower && opposite && pending && s.state==State::IDLE);
    assert(c.pending_calibration_range(&candidate));
    assert(candidate.minimum_speed_code>fixture().calibration_floor_speed_code);
    assert(candidate.maximum_speed_code==fixture().run_speed_code);
    assert(s.start_block!=Block::NONE); // cannot start in the persist/readback interval
    Controller failed_store=c;
    assert(!failed_store.finish_calibration_storage(false));
    assert(!failed_store.operating_range().valid);
    assert(!failed_store.pending_calibration_range(&candidate));
    assert(c.finish_calibration_storage(true));
    s=c.snapshot(final_time);
    assert(s.range_calibrated && !s.calibration_active && !std::strcmp(s.calibration_phase,"SAVED"));
    assert(!c.pending_calibration_range(&candidate));
    Controller reboot(fixture(),{},2);
    assert(reboot.install_range(c.operating_range()));
    assert(!reboot.snapshot(0).duration_active && !reboot.snapshot(0).delayed_armed);
    std::cout<<"PASS integrated automatic calibration: one START, opposite-channel dynamics, 20ms quantized slew, cooldown then storage, restart stays off; simulated_ms="<<final_time<<"\n";
}
static void cancellations_and_ambiguity() {
    Controller cancel(fixture(),{},3);ignite(cancel);
    auto i=input(161,270000,70000);i.operator_combustion_problem=true;
    auto s=cancel.tick(i);
    assert(s.state==State::STOP_PURGE && !s.outputs.fuel && s.outputs.fan_request);
    assert(!s.calibration_active && !cancel.operating_range().valid);
    bool event=false;
    for(size_t n=0;n<cancel.event_count();++n)
        event|=!std::strcmp(cancel.events()[n].kind,"OPERATOR_COMBUSTION_PROBLEM");
    assert(event);
    Controller timer(fixture(),{},4);ignite(timer);
    i=input(161,270000,70000);i.set_shutdown_timer=true;i.shutdown_after_ms=20;timer.tick(i);
    s=timer.tick(input(181,270000,70000));
    assert(s.state==State::STOP_PURGE && !s.outputs.fuel && !s.calibration_active);
    Controller stale(fixture(),{},5);ignite(stale);
    i=input(161,270000,70000);i.sensors.exhaust.valid=false;s=stale.tick(i);
    assert(s.state==State::FAULT_PURGE && !s.outputs.fuel && !s.calibration_active);
    Controller constant(fixture(),{},6);ignite(constant);
    for(uint64_t t=161;t<3000000;t+=20) {
        s=constant.tick(input(t,270000,70000));
        if(s.state!=State::RUN) break;
    }
    assert(s.state==State::FAULT_PURGE && (s.faults&FAULT_CALIBRATION) && !s.outputs.fuel);
    assert(!std::strcmp(s.calibration_reason,"NO_COMMAND_RESPONSE"));
    assert(!constant.operating_range().valid);
    auto narrow=fixture();narrow.calibration_floor_speed_code=narrow.run_speed_code-1;
    Controller invalid(narrow,{},7);invalid.tick(input(100));i=input(101);i.start_calibration=true;
    s=invalid.tick(i);assert(s.state==State::IDLE && !s.outputs.fuel);
    std::cout<<"PASS calibration integration negatives: short operator STOP/event, timer cutoff, failed sensor, fresh constants rejected, no invalid trial startup\n";
}
int main(){success_and_cooldown_storage();cancellations_and_ambiguity();}
