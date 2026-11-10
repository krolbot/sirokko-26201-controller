#include "sirokko/control.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace sirokko;

// Every temperature and millisecond process value below belongs to a SYNTHETIC
// plant. No test fixture is deployed as heater process configuration.
static SafetyEnvelope fixture() {
    SafetyEnvelope p{};p.provisioned=true;
    p.recovery_purge_ms=100;p.prepurge_ms=10;p.preheat_ms=10;
    p.ignition_ms=10;p.verify_ms=10;p.postpurge_ms=20;
    p.maximum_purge_exit_exhaust_milli_c=60000;p.maximum_purge_exit_air_milli_c=50000;
    p.maximum_exhaust_milli_c=500000;p.maximum_air_milli_c=250000;
    p.minimum_ignition_rise_milli_c=10000;p.minimum_ignition_air_rise_milli_c=5000;
    p.minimum_run_exhaust_milli_c=110000;
    p.purge_speed_code=1400;p.reference_speed_code=1000;p.exploration_floor_code=400;
    return p;
}
static TickInput input(uint64_t now,int exh=20000,int air=20000) {
    TickInput i{};i.now_ms=now;i.sensors={{exh,now,0,true},{air,now,0,true},{20000,now,0,true}};return i;
}
static LearnedOperatingModel stored_model(const SafetyEnvelope& envelope, uint16_t minimum,
                                          uint16_t maximum) {
    LearnedOperatingModel model{};
    model.valid=true; model.minimum_speed_code=minimum; model.maximum_speed_code=maximum;
    model.provenance={envelope_fingerprint(envelope),100,200,1};
    model.measurements.elapsed_ms=100; model.measurements.tested_point_count=1;
    model.fingerprint=learned_model_fingerprint(model);
    return model;
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
static void assert_same_range(const LearnedOperatingModel& actual,const LearnedOperatingModel& expected) {
    assert(actual.valid==expected.valid);
    assert(actual.minimum_speed_code==expected.minimum_speed_code);
    assert(actual.maximum_speed_code==expected.maximum_speed_code);
    assert(actual.slew_up_per_s==expected.slew_up_per_s);
    assert(actual.slew_down_per_s==expected.slew_down_per_s);
    assert(actual.provenance.safety_envelope_fingerprint==expected.provenance.safety_envelope_fingerprint);
    assert(actual.provenance.revision==expected.provenance.revision);
    assert(actual.fingerprint==expected.fingerprint);
}
static void ignite(Controller& c) {
    assert(c.tick(input(100)).state==State::IDLE);
    auto i=input(101);i.start_commissioning=true;
    auto s=c.tick(i);assert(s.state==State::PREPURGE && s.commissioning.active);
    assert(c.tick(input(111)).state==State::PREHEAT);
    assert(c.tick(input(121)).state==State::IGNITION);
    assert(c.tick(input(131,270000,70000)).state==State::VERIFY);
    s=c.tick(input(141,270000,70000));
    assert(s.state==State::RUN && s.commissioning.active);
    assert(!c.update_settings(Settings{}));
}
static void storage_transaction_contract(const Controller& baseline,const LearnedOperatingModel& candidate,
                                         const LearnedOperatingModel& old,uint64_t final_time) {
    constexpr uint64_t kStorageId=701;
    LearnedOperatingModel range{};
    auto i=input(final_time+1,20000,20000);

    Controller pre_stage_stop=baseline;
    i.stop=true;
    auto s=pre_stage_stop.tick(i);
    assert(s.state==State::IDLE && !s.commissioning.active);
    assert(!pre_stage_stop.pending_learned_model(&range));
    assert_same_range(pre_stage_stop.learned_model(),old);

    Controller invalid_sensor=baseline;
    i=input(final_time+1,20000,20000);i.sensors.air.valid=false;
    s=invalid_sensor.tick(i);
    assert(s.state==State::IDLE && !s.commissioning.active);
    assert(s.commissioning.reason==CommissioningReason::SENSOR_INVALID);
    assert_same_range(invalid_sensor.learned_model(),old);

    Controller staged_stop=baseline;
    assert(!staged_stop.begin_model_storage(0));
    assert(staged_stop.begin_model_storage(kStorageId));
    s=staged_stop.snapshot(final_time);
    assert(s.commissioning.active && s.commissioning.phase==CommissioningPhase::PERSIST_STAGING);
    assert(!staged_stop.pending_learned_model(&range));
    assert(!staged_stop.model_storage_ready(kStorageId));
    assert(!staged_stop.complete_model_storage(kStorageId,true)); // unsealed write cannot activate
    i=input(final_time+1,20000,20000);i.stop=true;
    s=staged_stop.tick(i);
    assert(s.state==State::IDLE && !s.commissioning.active);
    assert(!staged_stop.seal_model_storage(kStorageId));
    assert_same_range(staged_stop.learned_model(),old);

    Controller staged_cancel=baseline;
    assert(staged_cancel.begin_model_storage(kStorageId+1));
    assert(staged_cancel.abort_model_storage()==ModelStorageAbort::ABORTED);
    s=staged_cancel.snapshot(final_time);
    assert(!s.commissioning.active && s.commissioning.phase==CommissioningPhase::CANCELLED &&
           s.commissioning.reason==CommissioningReason::EXTERNAL_CANCEL);
    assert_same_range(staged_cancel.learned_model(),old);

    Controller staged_fault=baseline;
    assert(staged_fault.begin_model_storage(kStorageId+2));
    i=input(final_time+1,20000,20000);i.operator_combustion_problem=true;
    i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=staged_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && !s.commissioning.active &&
           !s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
    assert_operator_context_before_fault(staged_fault,State::IDLE,0);
    assert_same_range(staged_fault.learned_model(),old);

    Controller sealing=baseline;
    assert(sealing.begin_model_storage(kStorageId));
    assert(!sealing.seal_model_storage(kStorageId+1));
    i=input(final_time+1,60001,50001); // Fresh, but not cold enough to commit.
    assert(sealing.tick(i).state==State::IDLE);
    assert(!sealing.seal_model_storage(kStorageId));
    const uint64_t seal_time=final_time+2;
    assert(sealing.tick(input(seal_time,20000,20000)).state==State::IDLE);
    assert(sealing.seal_model_storage(kStorageId));
    s=sealing.snapshot(seal_time);
    assert(s.commissioning.active && s.commissioning.phase==CommissioningPhase::PERSIST_COMMITTING);
    assert(sealing.model_storage_ready(kStorageId));
    assert(!sealing.model_storage_ready(kStorageId+1));
    assert(sealing.abort_model_storage()==ModelStorageAbort::COMMIT_IN_PROGRESS);
    assert(!sealing.complete_model_storage(kStorageId+1,true));
    assert(sealing.model_storage_ready(kStorageId));

    Controller sealed_controls=sealing;
    Settings changed=sealed_controls.settings();changed.room_setpoint_milli_c+=1000;
    assert(!sealed_controls.update_settings(changed));

    i=input(seal_time+1,20000,20000);i.enter_maintenance=true;
    s=sealed_controls.tick(i);
    assert(s.state==State::IDLE && s.commissioning.active);
    i=input(seal_time+2,20000,20000);i.start=true;
    s=sealed_controls.tick(i);
    assert(s.state==State::IDLE && s.commissioning.active && !s.outputs.fuel && !s.outputs.glow);
    assert(sealed_controls.model_storage_ready(kStorageId));

    Controller sealed_stop=sealing;
    i=input(seal_time+1,20000,20000);i.stop=true;
    s=sealed_stop.tick(i);
    assert(s.state==State::IDLE && s.commissioning.active &&
           s.commissioning.phase==CommissioningPhase::PERSIST_COMMITTING);
    assert(sealed_stop.model_storage_ready(kStorageId));
    assert(event_index(sealed_stop,"CALIBRATION_CANCELLED")==sealed_stop.event_count());

    Controller sealed_failure=sealing;
    i=input(seal_time+1,20000,20000);i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=sealed_failure.tick(i);
    assert(s.state==State::FAULT_PURGE && s.commissioning.active);
    assert(!sealed_failure.model_storage_ready(kStorageId));
    assert(!sealed_failure.complete_model_storage(kStorageId,false)); // allowed cleanup during FAULT_PURGE
    s=sealed_failure.snapshot(seal_time+1);
    assert(!s.commissioning.active && s.commissioning.phase==CommissioningPhase::PERSIST_FAILED &&
           s.commissioning.reason==CommissioningReason::STORAGE_UNCONFIRMED);
    assert(event_index(sealed_failure,"CALIBRATION_CANCELLED")==sealed_failure.event_count());
    assert_same_range(sealed_failure.learned_model(),old);
    assert(!sealed_failure.complete_model_storage(kStorageId,false)); // stale completion
    Settings after_unconfirmed=sealed_failure.settings();after_unconfirmed.room_setpoint_milli_c+=1000;
    assert(sealed_failure.update_settings(after_unconfirmed));
    s=sealed_failure.snapshot(seal_time+1);
    assert(s.commissioning.phase==CommissioningPhase::PERSIST_FAILED &&
           s.commissioning.reason==CommissioningReason::STORAGE_UNCONFIRMED);
    Controller unconfirmed_launch=sealed_failure;
    assert(unconfirmed_launch.tick(input(seal_time+21,20000,20000)).state==State::FAULT);
    i=input(seal_time+22,20000,20000);i.reset_fault=true;
    assert(unconfirmed_launch.tick(i).state==State::IDLE);
    i=input(seal_time+23,20000,20000);i.start_commissioning=true;
    s=unconfirmed_launch.tick(i);
    assert(s.state==State::PREPURGE && s.commissioning.active &&
           s.commissioning.phase==CommissioningPhase::STARTING);
    assert(sealed_failure.tick(input(seal_time+21,20000,20000)).state==State::FAULT);
    s=sealed_failure.snapshot(seal_time+22);
    assert(s.commissioning.phase==CommissioningPhase::PERSIST_FAILED &&
           s.commissioning.reason==CommissioningReason::STORAGE_UNCONFIRMED);

    Controller sealed_fault=sealing;
    i=input(seal_time+1,20000,20000);i.operator_combustion_problem=true;
    i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=sealed_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && s.commissioning.active && !s.outputs.fuel && !s.outputs.glow);
    assert_operator_context_before_fault(sealed_fault,State::IDLE,0);
    assert(event_index(sealed_fault,"CALIBRATION_CANCELLED")==sealed_fault.event_count());
    assert(!sealed_fault.complete_model_storage(kStorageId,true)); // verified range waits out purge
    s=sealed_fault.snapshot(seal_time+1);
    assert(s.commissioning.active && s.commissioning.phase==CommissioningPhase::PERSIST_COMMITTING);
    assert_same_range(sealed_fault.learned_model(),old);
    s=sealed_fault.tick(input(seal_time+21,20000,20000));
    assert(s.state==State::FAULT && sealed_fault.model_storage_ready(kStorageId));
    assert(sealed_fault.complete_model_storage(kStorageId,true));
    s=sealed_fault.snapshot(seal_time+21);
    assert(s.state==State::FAULT && s.learned_model.valid && !s.commissioning.active &&
           s.commissioning.phase==CommissioningPhase::SAVED);
    assert_same_range(sealed_fault.learned_model(),candidate);
    assert(!sealed_fault.complete_model_storage(kStorageId,true)); // late ACK never replays

    Controller unstaged_abort=baseline;
    assert(unstaged_abort.abort_model_storage()==ModelStorageAbort::ABORTED);
    assert_same_range(unstaged_abort.learned_model(),old);
    assert(unstaged_abort.abort_model_storage()==ModelStorageAbort::NO_CANDIDATE);
    Controller no_transaction=baseline;
    assert(!no_transaction.complete_model_storage(kStorageId,true));
    assert_same_range(no_transaction.learned_model(),old);
    assert(no_transaction.pending_learned_model(&range));
}
static void success_and_cooldown_storage() {
    Controller c(fixture(),{},1);
    const LearnedOperatingModel old=stored_model(fixture(),fixture().exploration_floor_code,
                                                  fixture().reference_speed_code);
    assert(c.install_learned_model(old));
    ignite(c);
    double exh=270000,air=70000;
    auto i=input(141,270000,70000);
    Snapshot s=c.snapshot(141);
    bool lower=false,opposite=false,pending=false;
    LearnedOperatingModel candidate{};
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
            std::cerr<<"unexpected commissioning failure at "<<t<<"\n";
            assert(false);
        }
        if(s.state==State::STOP_PURGE) {
            pending=true;
            assert(!s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
            assert(!c.pending_learned_model(&candidate));
            assert_same_range(c.learned_model(),old); // analysis never overwrites a prior saved range
        }
        if(pending && s.state==State::IDLE) {final_time=t;break;}
    }
    assert(lower && opposite && pending && s.state==State::IDLE);
    assert(c.pending_learned_model(&candidate));
    assert(candidate.minimum_speed_code>fixture().exploration_floor_code);
    assert(candidate.maximum_speed_code==fixture().reference_speed_code);
    assert(candidate.provenance.safety_envelope_fingerprint==envelope_fingerprint(fixture()));
    assert(candidate.provenance.commissioning_completed_ms>=candidate.provenance.commissioning_started_ms);
    assert(candidate.measurements.elapsed_ms==candidate.provenance.commissioning_completed_ms-
           candidate.provenance.commissioning_started_ms);
    assert(candidate.measurements.tested_point_count>0);
    assert(candidate.measurements.reference_settled_after_ms>0);
    assert(candidate.fingerprint==learned_model_fingerprint(candidate));
    assert(s.commissioning.source_envelope_fingerprint==envelope_fingerprint(fixture()));
    assert(s.commissioning.phase!=CommissioningPhase::SAVED);
    assert(s.start_block!=Block::NONE); // cannot start in the persist/readback interval
    storage_transaction_contract(c,candidate,old,final_time);
    std::cout<<"PASS integrated calibration storage transaction: pre-seal cancellation, sealed STOP/fault preservation, deferred readback, exact range replacement\n";
}
static void cancellations_and_ambiguity() {
    Controller cancel(fixture(),{},3);ignite(cancel);
    auto i=input(161,270000,70000);i.operator_combustion_problem=true;
    auto s=cancel.tick(i);
    assert(s.state==State::STOP_PURGE && !s.outputs.fuel && s.outputs.fan_request);
    assert(!s.commissioning.active && !cancel.learned_model().valid);
    bool event=false;
    for(size_t n=0;n<cancel.event_count();++n)
        event|=!std::strcmp(cancel.events()[n].kind,"OPERATOR_COMBUSTION_PROBLEM");
    assert(event);
    Controller sensor_fault(fixture(),{},4);ignite(sensor_fault);
    i=input(161,270000,70000);i.operator_combustion_problem=true;i.sensors.air.valid=false;
    s=sensor_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && (s.faults&FAULT_AIR) &&
           !s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
    assert_operator_context_before_fault(sensor_fault,State::RUN,fixture().reference_speed_code);
    Controller external_fault(fixture(),{},5);ignite(external_fault);
    i=input(161,270000,70000);i.operator_combustion_problem=true;i.external_faults=FAULT_CONTROL_WATCHDOG;
    s=external_fault.tick(i);
    assert(s.state==State::FAULT_PURGE && (s.faults&FAULT_CONTROL_WATCHDOG) &&
           !s.outputs.fuel && !s.outputs.glow && s.outputs.fan_request);
    assert_operator_context_before_fault(external_fault,State::RUN,fixture().reference_speed_code);
    Controller timer(fixture(),{},6);ignite(timer);
    i=input(161,270000,70000);i.set_shutdown_timer=true;i.shutdown_after_ms=20;timer.tick(i);
    s=timer.tick(input(181,270000,70000));
    assert(s.state==State::STOP_PURGE && !s.outputs.fuel && !s.commissioning.active);
    Controller stale(fixture(),{},5);ignite(stale);
    i=input(161,270000,70000);i.sensors.exhaust.valid=false;s=stale.tick(i);
    assert(s.state==State::FAULT_PURGE && !s.outputs.fuel && !s.commissioning.active);
    Controller constant(fixture(),{},6);ignite(constant);
    for(uint64_t t=161;t<3000000;t+=20) {
        s=constant.tick(input(t,270000,70000));
        if(s.state!=State::RUN) break;
    }
    assert(s.state==State::FAULT_PURGE && (s.faults&FAULT_COMMISSIONING) && !s.outputs.fuel);
    assert(s.commissioning.reason==CommissioningReason::NO_COMMAND_RESPONSE);
    assert(!constant.learned_model().valid);
    auto narrow=fixture();narrow.exploration_floor_code=narrow.reference_speed_code-1;
    assert(!valid_safety_envelope(narrow));
    assert(commissioning_configuration_block(narrow,{})==Block::ENVELOPE_INVALID);
    Controller invalid(narrow,{},7);
    s=invalid.tick(input(100));
    assert(s.state==State::RECOVERY_PURGE && s.start_block==Block::ENVELOPE_MISSING &&
           s.commissioning.block==Block::ENVELOPE_MISSING && !s.outputs.fuel);
    i=input(101);i.start_commissioning=true;
    s=invalid.tick(i);
    assert(s.state==State::RECOVERY_PURGE && !s.outputs.fuel && !s.commissioning.active);
    std::cout<<"PASS calibration integration negatives: short operator STOP/event, simultaneous sensor/external faults retain operator context, timer cutoff, failed sensor, fresh constants rejected, no invalid trial startup\n";
}
static void calibration_admission_budget() {
    const auto profile=fixture();
    const Settings defaults{};
    assert(valid_safety_envelope(profile));
    assert(commissioning_configuration_block(profile,defaults)==Block::NONE);
    assert(!std::strcmp(block_name(Block::ENVELOPE_INVALID),"ENVELOPE_INVALID"));
    assert(!std::strcmp(block_name(Block::ENVELOPE_INVALID),"ENVELOPE_INVALID"));
    assert(!std::strcmp(block_name(Block::ENVELOPE_INVALID),"ENVELOPE_INVALID"));

    auto disabled=profile;disabled.exploration_floor_code=0;
    assert(valid_safety_envelope(disabled));
    assert(commissioning_configuration_block(disabled,defaults)==Block::ENVELOPE_INVALID);
    Controller disabled_controller(disabled,{},8);
    auto s=disabled_controller.snapshot(0);
    assert(s.commissioning.block==Block::RECOVERY_PURGE);
    s=disabled_controller.tick(input(100));
    assert(s.state==State::IDLE && s.start_block==Block::NONE &&
           s.commissioning.block==Block::ENVELOPE_INVALID);
    auto i=input(101);i.start_commissioning=true;
    s=disabled_controller.tick(i);
    assert(s.state==State::IDLE && !s.commissioning.active && !s.outputs.fuel &&
           s.commissioning.block==Block::ENVELOPE_INVALID);

    Settings slow{};slow.slew_up_per_s=0.0001;slow.slew_down_per_s=0.0001;
    assert(valid_settings(slow));
    assert(commissioning_configuration_block(profile,slow)==Block::ENVELOPE_INVALID);
    Controller ordinary_slow(profile,slow,9);
    assert(ordinary_slow.tick(input(100)).state==State::IDLE);
    i=input(101);i.start=true;
    s=ordinary_slow.tick(i);
    assert(s.state==State::PREPURGE && s.outputs.fan_request && !s.outputs.fuel);
    Controller rejected_calibration(profile,slow,10);
    assert(rejected_calibration.tick(input(100)).state==State::IDLE);
    s=rejected_calibration.snapshot(100);
    assert(s.start_block==Block::NONE && s.commissioning.block==Block::ENVELOPE_INVALID);
    i=input(101);i.start_commissioning=true;
    s=rejected_calibration.tick(i);
    assert(s.state==State::IDLE && !s.commissioning.active && !s.outputs.fuel &&
           s.commissioning.block==Block::ENVELOPE_INVALID);

    Settings just_fits{};just_fits.slew_up_per_s=0.00146;just_fits.slew_down_per_s=0.00146;
    Settings over_budget{};over_budget.slew_up_per_s=0.00145;over_budget.slew_down_per_s=0.00145;
    assert(valid_settings(just_fits) && valid_settings(over_budget));
    assert(commissioning_configuration_block(profile,just_fits)==Block::NONE);
    assert(commissioning_configuration_block(profile,over_budget)==Block::ENVELOPE_INVALID);

    auto smallest=profile;smallest.exploration_floor_code=smallest.reference_speed_code-2;
    assert(valid_safety_envelope(smallest));
    assert(commissioning_configuration_block(smallest,defaults)==Block::NONE);
    assert(commissioning_configuration_block(smallest,slow)==Block::ENVELOPE_INVALID);
    auto widest=profile;widest.reference_speed_code=kPwmMaximum;widest.exploration_floor_code=1;
    assert(valid_safety_envelope(widest));
    assert(commissioning_configuration_block(widest,defaults)==Block::NONE);
    assert(commissioning_configuration_block(widest,slow)==Block::ENVELOPE_INVALID);
    auto long_start=profile;long_start.prepurge_ms=6ULL*60*60*1000;
    assert(valid_safety_envelope(long_start));
    assert(commissioning_configuration_block(long_start,defaults)==Block::ENVELOPE_INVALID);
    std::cout<<"PASS calibration admission: disabled floor, invalid span, fixed phases, slow ordinary start, pre-ignition budget rejection, near boundary and minimum/maximum ranges\n";
}
int main(){calibration_admission_budget();success_and_cooldown_storage();cancellations_and_ambiguity();}
