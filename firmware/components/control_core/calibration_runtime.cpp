#include "sirokko/control.hpp"
#include <algorithm>
#include <cmath>

namespace sirokko {
void Controller::start_calibration_run(uint64_t now_ms) {
    // Analysis-policy defaults, NOT physical ignition/flame/overtemperature limits.
    // The trial interval itself must be explicitly supplied in the process profile.
    const uint16_t span = profile_.run_speed_code-profile_.calibration_floor_speed_code;
    calibration::Config c{};
    c.floor_code=profile_.calibration_floor_speed_code;
    c.ceiling_code=profile_.run_speed_code;
    c.step_code=std::max<uint16_t>(1,(span+9)/10);
    c.samples_per_window=60;
    c.sample_period_ms=1000;
    c.maximum_sample_gap_ms=1600;
    c.maximum_sample_age_ms=kSensorStaleMs;
    c.maximum_no_new_sample_ms=kSensorStaleMs;
    c.maximum_channel_skew_ms=250;
    c.command_arrival_timeout_ms=static_cast<uint64_t>(1000.0/std::min(settings_.slew_up_per_s,settings_.slew_down_per_s))+120000;
    c.reference_timeout_ms=1800000;
    c.point_timeout_ms=1800000;
    c.maximum_total_duration_ms=6ULL*60*60*1000;
    c.maximum_transition_settle_difference_ms=120000;
    c.reference_noise_floor_milli_c=250;
    c.reference_noise_ceiling_milli_c=2000;
    c.reference_drift_floor_milli_c=250;
    c.reference_drift_ceiling_milli_c=1000;
    c.minimum_command_response_milli_c=2000;
    c.reference_noise_multiplier=3;
    c.reference_drift_multiplier=3;
    c.repeat_mean_multiplier=3;
    c.response_multiplier=3;
    c.maximum_slew_up_codes_per_s=span*settings_.slew_up_per_s;
    c.maximum_slew_down_codes_per_s=span*settings_.slew_down_per_s;
    calibration_config_=c;
    calibration_control_ms_=now_ms;
    calibration_logged_phase_=calibration::Phase::IDLE;
    if(!calibrator_.start(c,now_ms)) latch_fault(FAULT_CALIBRATION,now_ms);
}

void Controller::cancel_calibration(calibration::Reason reason,uint64_t now_ms) {
    calibrator_.cancel(reason);
    calibration_requested_=false;
    calibration_candidate_={};
    calibration_saved_=false;
    record("CALIBRATION_CANCELLED",now_ms);
}

void Controller::run_calibration(uint64_t now_ms,const Sensors& sensors) {
    calibration::Observation observation{};
    observation.now_ms=now_ms;
    observation.exhaust={sensors.exhaust.milli_c,sensors.exhaust.sample_ms,sensors.exhaust.status,sensors.exhaust.valid};
    observation.hot_air={sensors.air.milli_c,sensors.air.sample_ms,sensors.air.status,sensors.air.valid};
    observation.commanded_code=static_cast<uint16_t>(std::lround(run_speed_code_));
    observation.run_active=state_==State::RUN;
    observation.fuel_commanded=outputs().fuel;
    observation.glow_commanded=outputs().glow;
    const auto cal=calibrator_.tick(observation);
    if(cal.phase!=calibration_logged_phase_) {
        calibration_logged_phase_=cal.phase;
        record(calibration::phase_name(cal.phase),now_ms);
    }
    if(cal.stop_requested) {
        if(cal.completed_pending_stop && cal.result.profile.valid) {
            const auto& p=cal.result.profile;
            calibration_candidate_={true,p.min_code,p.max_code,
                settings_.slew_up_per_s,settings_.slew_down_per_s,process_fingerprint(profile_)};
            // The usable range is narrower than the tested interval: the same fractional
            // limit implies a slower absolute motion, never a faster untested transition.
            calibration_requested_=false;
            record("CALIBRATION_PENDING_COOLDOWN",now_ms);
            begin_stop(now_ms);
        } else {
            // Keep the calibrator's exact failure reason, not a generic cancel reason.
            calibration_requested_=false;
            record("CALIBRATION_REJECTED",now_ms);
            latch_fault(FAULT_CALIBRATION,now_ms);
        }
        return;
    }
    const double span=calibration_config_.ceiling_code-calibration_config_.floor_code;
    const double target=std::clamp((cal.target_code-calibration_config_.floor_code)/span,0.0,1.0);
    const auto command=regulator_.tick(now_ms,20.0,20.0,false,target,
        settings_.slew_up_per_s,settings_.slew_down_per_s);
    run_speed_code_=calibration_config_.floor_code+command.fraction*span;
    calibration_control_ms_=now_ms;
}

bool Controller::pending_calibration_range(OperatingRange* range) const {
    if(!range || !calibration_candidate_.valid || state_!=State::IDLE || faults_!=0) return false;
    *range=calibration_candidate_;
    return true;
}

bool Controller::finish_calibration_storage(bool success) {
    const bool accepted=success && state_==State::IDLE && faults_==0 &&
        calibration_candidate_.valid && install_range(calibration_candidate_);
    calibration_candidate_={};
    calibration_saved_=accepted;
    if(!accepted) calibrator_.cancel(calibration::Reason::EXTERNAL_CANCEL);
    record(accepted?"CALIBRATION_SAVED":"CALIBRATION_STORAGE_FAILED",last_tick_ms_);
    return accepted;
}
} // namespace sirokko
