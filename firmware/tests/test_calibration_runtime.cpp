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
static size_t event_index(const Controller& controller,const char* kind) {
    for(size_t n=0;n<controller.event_count();++n)
        if(!std::strcmp(controller.events()[n].kind,kind)) return n;
    return controller.event_count();
}
static void assert_operator_context_before_fault(const Controller& controller,State state,uint16_t speed_code) {
    const size_t operator_event=event_index(controller,"OPERATOR_COMBUSTION_PROBLEM");
    const size_t fault_event=event_index(controller,"FAULT");
    assert(operator_event<controller.event_count() && fault_event<controller.event_count());
    assert(operator_event<fault_event);
    const auto& event=controller.events()[operator_event];
    assert(event.state==state && event.speed_code==speed_code && event.faults==FAULT_NONE);
}
static void assert_same_range(const OperatingRange& actual,const OperatingRange& expected) {
    assert(actual.valid==expected.valid);
    assert(actual.minimum_speed_code==expected.minimum_speed_code);
    assert(actual.maximum_speed_code==expected.maximum_speed_code);
    assert(actual.slew_up_per_s==expected.slew_up_per_s);
    assert(actual.slew_down_per_s==expected.slew_down_per_s);
    assert(actual.process_fingerprint==expected.process_fingerprint);
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
static void storage_transaction_contract(const Controller& baseline,const OperatingRange& candidate,
                                         const OperatingRange& old,uint64_t final_time) {
    constexpr uint64_t kStorageId=701;
    OperatingRange range{};
    auto i=input(final_time+1,20000,20000);

    Controller pre_stage_stop=baseline;
    i.stop=true;
    auto s=pre_stage_stop.tick(i);
    assert(s.state==State::IDLE && !s.calibration_active);
    assert(!pre_stage_stop.pending_calibration_range(&range));
    assert_same_range(pre_stage_stop.operating_range(),old);

    Controller invalid_sensor=baseline;
    i=input(final_time+1,20000,20000);i.sensors.air.valid=false;
    s=invalid_sensor.tick(i);
    assert(s.state==State::IDLE && !s.calibration_active);
    assert(!std::strcmp(s.calibration_reason,"SENSOR_INVALID"));
    assert_same_range(invalid_sensor.operating_range(),old);

    Controller staged_stop=baseline;
    assert(!staged_stop.begin_calibration_storage(0));
    assert(staged_stop.begin_calibration_storage(kStorageId));
    s=staged_stop.snapshot(final_time);
    assert(s.calibration_active && !std::strcmp(s.calibration_phase,"PERSIST_STAGING"));
    assert(!staged_stop.pending_calibration_range(&range));
    assert(!staged_stop.calibration_storage_ready(kStorageId));
    assert(!staged_stop.finish_calibration_storage(true)); // true cannot bypass a staging id
    i=input(final_time+1,20000,20000);i.stop=true;
    s=staged_stop.tick(i);
    assert(s.state==State::IDLE && !s.calibration_active);
    assert(!staged_stop.seal_calibration_storage(kStorageId));
    assert_same_range(staged_stop.operating_range(),old);

    Controller staged_cancel=baseline;
    assert(staged_cancel.begin_calibration_storage(kStorageId+1));
    assert(!staged_cancel.finish_calibration_storage(false)); // parent may abandon staging before seal
    s=staged_cancel.snapshot(final_time);
    assert(!s.calibration_active && !std::strcmp(s.calibration_phase,"CANCELLED") &&
           !std::strcmp(s.calibration_reason,"EXTERNAL_CANCEL"));
    assert_same_range(staged_cancel.operating_range(),old);

    Controller staged_fault=baseline;
    assert(staged_fault.begin_calibration_storage(kStorageId+2));
    i=input(final_time+1,20000,20000);i.operator_combustion_problem=true;
    i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=staged_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && !s.calibration_active &&
           !s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
    assert_operator_context_before_fault(staged_fault,State::IDLE,0);
    assert_same_range(staged_fault.operating_range(),old);

    Controller sealing=baseline;
    assert(sealing.begin_calibration_storage(kStorageId));
    assert(!sealing.seal_calibration_storage(kStorageId+1));
    i=input(final_time+1,60001,50001); // Fresh, but not cold enough to commit.
    assert(sealing.tick(i).state==State::IDLE);
    assert(!sealing.seal_calibration_storage(kStorageId));
    const uint64_t seal_time=final_time+2;
    assert(sealing.tick(input(seal_time,20000,20000)).state==State::IDLE);
    assert(sealing.seal_calibration_storage(kStorageId));
    s=sealing.snapshot(seal_time);
    assert(s.calibration_active && !std::strcmp(s.calibration_phase,"PERSIST_COMMITTING"));
    assert(sealing.calibration_storage_ready(kStorageId));
    assert(!sealing.calibration_storage_ready(kStorageId+1));
    assert(!sealing.finish_calibration_storage(true)); // sealed transactions require complete(id,...)
    assert(!sealing.complete_calibration_storage(kStorageId+1,true));
    assert(sealing.calibration_storage_ready(kStorageId));

    Controller sealed_controls=sealing;
    Settings changed=sealed_controls.settings();changed.room_setpoint_milli_c+=1000;
    assert(!sealed_controls.update_settings(changed));
    assert(!sealed_controls.update_process_profile(fixture(),seal_time));
    i=input(seal_time+1,20000,20000);i.enter_maintenance=true;
    s=sealed_controls.tick(i);
    assert(s.state==State::IDLE && s.calibration_active);
    i=input(seal_time+2,20000,20000);i.start=true;
    s=sealed_controls.tick(i);
    assert(s.state==State::IDLE && s.calibration_active && !s.outputs.fuel && !s.outputs.glow);
    assert(sealed_controls.calibration_storage_ready(kStorageId));

    Controller sealed_stop=sealing;
    i=input(seal_time+1,20000,20000);i.stop=true;
    s=sealed_stop.tick(i);
    assert(s.state==State::IDLE && s.calibration_active &&
           !std::strcmp(s.calibration_phase,"PERSIST_COMMITTING"));
    assert(sealed_stop.calibration_storage_ready(kStorageId));
    assert(event_index(sealed_stop,"CALIBRATION_CANCELLED")==sealed_stop.event_count());

    Controller sealed_failure=sealing;
    i=input(seal_time+1,20000,20000);i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=sealed_failure.tick(i);
    assert(s.state==State::FAULT_PURGE && s.calibration_active);
    assert(!sealed_failure.calibration_storage_ready(kStorageId));
    assert(!sealed_failure.complete_calibration_storage(kStorageId,false)); // allowed cleanup during FAULT_PURGE
    s=sealed_failure.snapshot(seal_time+1);
    assert(!s.calibration_active && !std::strcmp(s.calibration_phase,"PERSIST_FAILED") &&
           !std::strcmp(s.calibration_reason,"STORAGE_UNCONFIRMED"));
    assert(event_index(sealed_failure,"CALIBRATION_CANCELLED")==sealed_failure.event_count());
    assert_same_range(sealed_failure.operating_range(),old);
    assert(!sealed_failure.complete_calibration_storage(kStorageId,false)); // stale completion
    Settings after_unconfirmed=sealed_failure.settings();after_unconfirmed.room_setpoint_milli_c+=1000;
    assert(sealed_failure.update_settings(after_unconfirmed));
    s=sealed_failure.snapshot(seal_time+1);
    assert(!std::strcmp(s.calibration_phase,"PERSIST_FAILED") &&
           !std::strcmp(s.calibration_reason,"STORAGE_UNCONFIRMED"));
    Controller unconfirmed_launch=sealed_failure;
    assert(unconfirmed_launch.tick(input(seal_time+21,20000,20000)).state==State::FAULT);
    i=input(seal_time+22,20000,20000);i.reset_fault=true;
    assert(unconfirmed_launch.tick(i).state==State::IDLE);
    i=input(seal_time+23,20000,20000);i.start_calibration=true;
    s=unconfirmed_launch.tick(i);
    assert(s.state==State::PREPURGE && s.calibration_active &&
           !std::strcmp(s.calibration_phase,"STARTING"));
    assert(sealed_failure.tick(input(seal_time+21,20000,20000)).state==State::FAULT);
    assert(sealed_failure.update_process_profile(fixture(),seal_time+22));
    s=sealed_failure.snapshot(seal_time+22);
    assert(std::strcmp(s.calibration_phase,"PERSIST_FAILED") &&
           std::strcmp(s.calibration_reason,"STORAGE_UNCONFIRMED"));

    Controller sealed_fault=sealing;
    i=input(seal_time+1,20000,20000);i.operator_combustion_problem=true;
    i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=sealed_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && s.calibration_active && !s.outputs.fuel && !s.outputs.glow);
    assert_operator_context_before_fault(sealed_fault,State::IDLE,0);
    assert(event_index(sealed_fault,"CALIBRATION_CANCELLED")==sealed_fault.event_count());
    assert(!sealed_fault.complete_calibration_storage(kStorageId,true)); // verified range waits out purge
    s=sealed_fault.snapshot(seal_time+1);
    assert(s.calibration_active && !std::strcmp(s.calibration_phase,"PERSIST_COMMITTING"));
    assert_same_range(sealed_fault.operating_range(),old);
    s=sealed_fault.tick(input(seal_time+21,20000,20000));
    assert(s.state==State::FAULT && sealed_fault.calibration_storage_ready(kStorageId));
    assert(sealed_fault.complete_calibration_storage(kStorageId,true));
    s=sealed_fault.snapshot(seal_time+21);
    assert(s.state==State::FAULT && s.range_calibrated && !s.calibration_active &&
           !std::strcmp(s.calibration_phase,"SAVED"));
    assert_same_range(sealed_fault.operating_range(),candidate);
    assert(!sealed_fault.complete_calibration_storage(kStorageId,true)); // late ACK never replays

    Controller legacy_failure=baseline;
    assert(!legacy_failure.finish_calibration_storage(false));
    assert_same_range(legacy_failure.operating_range(),old);
    Controller legacy_success=baseline;
    assert(legacy_success.finish_calibration_storage(true));
    assert_same_range(legacy_success.operating_range(),candidate);
    s=legacy_success.snapshot(final_time);
    assert(!s.calibration_active && !std::strcmp(s.calibration_phase,"SAVED"));
}
static void success_and_cooldown_storage() {
    Controller c(fixture(),{},1);
    const OperatingRange old={true,fixture().calibration_floor_speed_code,fixture().run_speed_code,
                              0.005,0.005,process_fingerprint(fixture())};
    assert(c.install_range(old));
    ignite(c);
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
            assert_same_range(c.operating_range(),old); // analysis never overwrites a prior saved range
        }
        if(pending && s.state==State::IDLE) {final_time=t;break;}
    }
    assert(lower && opposite && pending && s.state==State::IDLE);
    assert(c.pending_calibration_range(&candidate));
    assert(candidate.minimum_speed_code>fixture().calibration_floor_speed_code);
    assert(candidate.maximum_speed_code==fixture().run_speed_code);
    assert(std::strcmp(s.calibration_phase,"SAVED"));
    assert(s.start_block!=Block::NONE); // cannot start in the persist/readback interval
    storage_transaction_contract(c,candidate,old,final_time);
    std::cout<<"PASS integrated calibration storage transaction: pre-seal cancellation, sealed STOP/fault preservation, deferred readback, exact range replacement\n";
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
    Controller sensor_fault(fixture(),{},4);ignite(sensor_fault);
    i=input(161,270000,70000);i.operator_combustion_problem=true;i.sensors.air.valid=false;
    s=sensor_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && (s.faults&FAULT_AIR) &&
           !s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
    assert_operator_context_before_fault(sensor_fault,State::RUN,fixture().run_speed_code);
    Controller external_fault(fixture(),{},5);ignite(external_fault);
    i=input(161,270000,70000);i.operator_combustion_problem=true;i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=external_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && (s.faults&FAULT_CONTROL_WATCHDOG) &&
           !s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
    assert_operator_context_before_fault(external_fault,State::RUN,fixture().run_speed_code);
    Controller timer(fixture(),{},6);ignite(timer);
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
    assert(!valid_process_profile(narrow));
    assert(calibration_configuration_block(narrow,{})==Block::CALIBRATION_RANGE_INVALID);
    Controller invalid(narrow,{},7);
    s=invalid.tick(input(100));
    assert(s.state==State::RECOVERY_PURGE && s.start_block==Block::PROFILE_NOT_QUALIFIED &&
           s.calibration_block==Block::PROFILE_NOT_QUALIFIED && !s.outputs.fuel);
    i=input(101);i.start_calibration=true;
    s=invalid.tick(i);
    assert(s.state==State::RECOVERY_PURGE && !s.outputs.fuel && !s.calibration_active);
    std::cout<<"PASS calibration integration negatives: short operator STOP/event, simultaneous sensor/external faults retain operator context, timer cutoff, failed sensor, fresh constants rejected, no invalid trial startup\n";
}
static void calibration_admission_budget() {
    const auto profile=fixture();
    const Settings defaults{};
    assert(valid_process_profile(profile));
    assert(calibration_configuration_block(profile,defaults)==Block::NONE);
    assert(!std::strcmp(block_name(Block::CALIBRATION_DISABLED),"CALIBRATION_DISABLED"));
    assert(!std::strcmp(block_name(Block::CALIBRATION_RANGE_INVALID),"CALIBRATION_RANGE_INVALID"));
    assert(!std::strcmp(block_name(Block::CALIBRATION_TIME_BUDGET),"CALIBRATION_TIME_BUDGET"));

    auto disabled=profile;disabled.calibration_floor_speed_code=0;
    assert(valid_process_profile(disabled));
    assert(calibration_configuration_block(disabled,defaults)==Block::CALIBRATION_DISABLED);
    Controller disabled_controller(disabled,{},8);
    auto s=disabled_controller.snapshot(0);
    assert(s.calibration_block==Block::RECOVERY_REQUIRED);
    s=disabled_controller.tick(input(100));
    assert(s.state==State::IDLE && s.start_block==Block::NONE &&
           s.calibration_block==Block::CALIBRATION_DISABLED);
    auto i=input(101);i.start_calibration=true;
    s=disabled_controller.tick(i);
    assert(s.state==State::IDLE && !s.calibration_active && !s.outputs.fuel &&
           s.calibration_block==Block::CALIBRATION_DISABLED);

    Settings slow{};slow.slew_up_per_s=0.0001;slow.slew_down_per_s=0.0001;
    assert(valid_settings(slow));
    assert(calibration_configuration_block(profile,slow)==Block::CALIBRATION_TIME_BUDGET);
    Controller ordinary_slow(profile,slow,9);
    assert(ordinary_slow.tick(input(100)).state==State::IDLE);
    i=input(101);i.start=true;
    s=ordinary_slow.tick(i);
    assert(s.state==State::PREPURGE && s.outputs.fan_request && !s.outputs.fuel);
    Controller rejected_calibration(profile,slow,10);
    assert(rejected_calibration.tick(input(100)).state==State::IDLE);
    s=rejected_calibration.snapshot(100);
    assert(s.start_block==Block::NONE && s.calibration_block==Block::CALIBRATION_TIME_BUDGET);
    i=input(101);i.start_calibration=true;
    s=rejected_calibration.tick(i);
    assert(s.state==State::IDLE && !s.calibration_active && !s.outputs.fuel &&
           s.calibration_block==Block::CALIBRATION_TIME_BUDGET);

    Settings just_fits{};just_fits.slew_up_per_s=0.00146;just_fits.slew_down_per_s=0.00146;
    Settings over_budget{};over_budget.slew_up_per_s=0.00145;over_budget.slew_down_per_s=0.00145;
    assert(valid_settings(just_fits) && valid_settings(over_budget));
    assert(calibration_configuration_block(profile,just_fits)==Block::NONE);
    assert(calibration_configuration_block(profile,over_budget)==Block::CALIBRATION_TIME_BUDGET);

    auto smallest=profile;smallest.calibration_floor_speed_code=smallest.run_speed_code-2;
    assert(valid_process_profile(smallest));
    assert(calibration_configuration_block(smallest,defaults)==Block::NONE);
    assert(calibration_configuration_block(smallest,slow)==Block::CALIBRATION_TIME_BUDGET);
    auto widest=profile;widest.run_speed_code=kPwmMaximum;widest.calibration_floor_speed_code=1;
    assert(valid_process_profile(widest));
    assert(calibration_configuration_block(widest,defaults)==Block::NONE);
    assert(calibration_configuration_block(widest,slow)==Block::CALIBRATION_TIME_BUDGET);
    auto long_start=profile;long_start.prepurge_ms=6ULL*60*60*1000;
    assert(valid_process_profile(long_start));
    assert(calibration_configuration_block(long_start,defaults)==Block::CALIBRATION_TIME_BUDGET);
    std::cout<<"PASS calibration admission: disabled floor, invalid span, fixed phases, slow ordinary start, pre-ignition budget rejection, near boundary and minimum/maximum ranges\n";
}
int main(){calibration_admission_budget();success_and_cooldown_storage();cancellations_and_ambiguity();}
