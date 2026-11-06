#include "ota_activation.hpp"
#include <cassert>
#include <iostream>
using namespace sirokko;
int main() {
    OtaActivationGate gate;int partition{};
    assert(!gate.queue(0,&partition,10));
    gate.observe(true);const auto first=gate.epoch();assert(first);
    assert(!gate.queue(first,nullptr,10));
    assert(gate.queue(first,&partition,10));
    assert(!gate.queue(first,&partition,10));
    assert(!gate.take_due(9));assert(!gate.take_due(209));
    assert(gate.take_due(210)==&partition);assert(!gate.take_due(999));
    assert(gate.queue(first,&partition,1000));
    gate.observe(false);assert(!gate.epoch());assert(!gate.take_due(1200));
    gate.observe(true);assert(gate.epoch() && gate.epoch()!=first);
    assert(!gate.queue(first,&partition,1300));
    assert(gate.queue(gate.epoch(),&partition,1300));
    gate.observe(true);assert(gate.take_due(1500)==&partition);
    // Synthetic values exercise admission only; not heater qualification limits.
    Snapshot state{};state.state=State::MAINTENANCE;
    ProcessProfile profile{};profile.qualified=true;
    profile.maximum_purge_exit_air_milli_c=30000;
    profile.maximum_purge_exit_exhaust_milli_c=40000;
    Sensors sensors{{20000,1000,0,true},{20000,1000,0,true},{20000,1000,0,true}};
    assert(ota_control_safe(state,sensors,profile,1000));
    assert(!ota_control_safe(state,sensors,profile,999));
    assert(!ota_control_safe(state,sensors,profile,1000+kSensorStaleMs+1));
    sensors.air.milli_c=30001;assert(!ota_control_safe(state,sensors,profile,1000));
    sensors.air.milli_c=20000;sensors.exhaust.status=1;
    assert(!ota_control_safe(state,sensors,profile,1000));sensors.exhaust.status=0;
    sensors.exhaust.valid=false;assert(!ota_control_safe(state,sensors,profile,1000));sensors.exhaust.valid=true;
    state.outputs.fuel=true;assert(!ota_control_safe(state,sensors,profile,1000));state.outputs.fuel=false;
    state.outputs.glow=true;assert(!ota_control_safe(state,sensors,profile,1000));state.outputs.glow=false;
    state.outputs.fan_request=true;assert(!ota_control_safe(state,sensors,profile,1000));state.outputs.fan_request=false;
    state.faults=FAULT_STORAGE;assert(!ota_control_safe(state,sensors,profile,1000));state.faults=0;
    state.state=State::IDLE;assert(!ota_control_safe(state,sensors,profile,1000));state.state=State::MAINTENANCE;
    profile.qualified=false;assert(!ota_control_safe(state,sensors,profile,1000));
    std::cout<<"PASS: OTA control-owned lease, revocation, stale epochs, delay and fresh/cold/output gates\n";
}
