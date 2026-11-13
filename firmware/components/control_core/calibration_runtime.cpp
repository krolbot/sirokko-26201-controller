#include "sirokko/control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sirokko {
namespace {

// This is an algorithmic deadline, not a combustion or equipment setting.
constexpr uint64_t kCalibrationOverallDurationMs = 6ULL * 60 * 60 * 1000;
constexpr uint8_t kCalibrationSamplesPerWindow = 60;
constexpr uint64_t kCalibrationSamplePeriodMs = 1000;

struct CalibrationTiming {
    uint64_t analysis_budget_ms{};
    bool fits{};
};

CommissioningMeasurements measurements_from(const calibration::Profile &profile, uint64_t elapsed_ms) {
    CommissioningMeasurements measured{};
    measured.elapsed_ms = elapsed_ms;
    measured.reference_settled_after_ms = profile.reference.settled_after_ms;
    measured.reference_exhaust_mean_milli_c = profile.reference.exhaust_mean_milli_c;
    measured.reference_air_mean_milli_c = profile.reference.hot_air_mean_milli_c;
    measured.reference_exhaust_noise_milli_c = profile.reference.exhaust_max_window_noise_milli_c;
    measured.reference_air_noise_milli_c = profile.reference.hot_air_max_window_noise_milli_c;
    measured.reference_exhaust_drift_milli_c = profile.reference.exhaust_max_window_drift_milli_c;
    measured.reference_air_drift_milli_c = profile.reference.hot_air_max_window_drift_milli_c;
    measured.observed_slew_up_codes_per_s = profile.tested_slew_up_codes_per_s;
    measured.observed_slew_down_codes_per_s = profile.tested_slew_down_codes_per_s;
    measured.tested_point_count = profile.tested_point_count;
    if (profile.tested_point_count != 0) {
        const auto &lowest = profile.tested_points[profile.tested_point_count - 1].repeat;
        measured.lowest_tested_settled_after_ms = lowest.settled_after_ms;
        measured.lowest_tested_exhaust_mean_milli_c = lowest.exhaust_mean_milli_c;
        measured.lowest_tested_air_mean_milli_c = lowest.hot_air_mean_milli_c;
    }
    return measured;
}

bool add_duration(uint64_t *total, uint64_t duration) {
    if (!total || duration > std::numeric_limits<uint64_t>::max() - *total)
        return false;
    *total += duration;
    return true;
}

// The regulator cannot traverse this distance faster than the configured fractional slew.
// Floor keeps the value a lower bound even when a non-integral millisecond is required.
uint64_t movement_lower_bound_ms(uint16_t distance, uint16_t span, double slew_per_s) {
    if (distance == 0)
        return 0;
    if (span == 0 || !std::isfinite(slew_per_s) || !(slew_per_s > 0.0))
        return std::numeric_limits<uint64_t>::max();
    const double codes_per_second = static_cast<double>(span) * slew_per_s;
    const double milliseconds = static_cast<double>(distance) * 1000.0 / codes_per_second;
    if (!std::isfinite(milliseconds) ||
        milliseconds >= static_cast<double>(std::numeric_limits<uint64_t>::max()))
        return std::numeric_limits<uint64_t>::max();
    return static_cast<uint64_t>(std::floor(milliseconds));
}

bool calibration_timing(const SafetyEnvelope &profile, const Settings &settings, CalibrationTiming *timing) {
    if (!timing || !valid_settings(settings) || profile.exploration_floor_code == 0 ||
        profile.exploration_floor_code >= profile.reference_speed_code)
        return false;

    const uint16_t span =
        static_cast<uint16_t>(profile.reference_speed_code - profile.exploration_floor_code);
    if (span < 2)
        return false;
    const uint16_t step = std::max<uint16_t>(1, static_cast<uint16_t>((span + 9U) / 10U));
    // The first distinct sample may arrive one millisecond after phase entry;
    // only the intervals BETWEEN accepted samples require sample_period_ms.
    // Keep admission a true lower bound rather than rounding every phase up.
    const uint64_t settle_ms =
        (static_cast<uint64_t>(kCalibrationSamplesPerWindow) * calibration::kRequiredAnalysisWindows - 1U) *
            kCalibrationSamplePeriodMs +
        1U;

    uint64_t fixed_phases_ms = 0;
    if (!add_duration(&fixed_phases_ms, profile.preheat_ms) ||
        !add_duration(&fixed_phases_ms, profile.ignition_timeout_ms) ||
        !add_duration(&fixed_phases_ms, profile.postpurge_ms))
        return false;

    uint64_t analysis_minimum_ms = 0;
    if (!add_duration(&analysis_minimum_ms, settle_ms))
        return false; // Initial reference.

    uint16_t target = profile.reference_speed_code;
    uint8_t point_count = 0;
    while (target > profile.exploration_floor_code) {
        const uint16_t remaining = static_cast<uint16_t>(target - profile.exploration_floor_code);
        const uint16_t descent = std::min(step, remaining);
        target = static_cast<uint16_t>(target - descent);
        if (++point_count > calibration::kMaximumTestedPoints)
            return false;

        const uint16_t distance = static_cast<uint16_t>(profile.reference_speed_code - target);
        const uint64_t down_ms = movement_lower_bound_ms(distance, span, settings.slew_down_per_s);
        const uint64_t up_ms = movement_lower_bound_ms(distance, span, settings.slew_up_per_s);
        if (down_ms == std::numeric_limits<uint64_t>::max() || up_ms == std::numeric_limits<uint64_t>::max())
            return false;

        // First lower point, return to the reference, then its repeat.
        if (!add_duration(&analysis_minimum_ms, down_ms) || !add_duration(&analysis_minimum_ms, settle_ms) ||
            !add_duration(&analysis_minimum_ms, up_ms) || !add_duration(&analysis_minimum_ms, settle_ms) ||
            !add_duration(&analysis_minimum_ms, down_ms) || !add_duration(&analysis_minimum_ms, settle_ms))
            return false;

        // Every non-final point has one more return/reference phase before the next descent.
        if (target != profile.exploration_floor_code &&
            (!add_duration(&analysis_minimum_ms, up_ms) || !add_duration(&analysis_minimum_ms, settle_ms)))
            return false;
    }

    if (fixed_phases_ms >= kCalibrationOverallDurationMs ||
        analysis_minimum_ms >= kCalibrationOverallDurationMs - fixed_phases_ms) {
        timing->fits = false;
        return true;
    }
    timing->analysis_budget_ms = kCalibrationOverallDurationMs - fixed_phases_ms;
    timing->fits = true;
    return true;
}

} // namespace

Block commissioning_configuration_block(const SafetyEnvelope &profile, const Settings &settings) {
    if (!profile.provisioned)
        return Block::ENVELOPE_MISSING;
    if (profile.exploration_floor_code != 0 &&
        (profile.exploration_floor_code >= profile.reference_speed_code ||
         static_cast<uint32_t>(profile.reference_speed_code) - profile.exploration_floor_code < 2U))
        return Block::ENVELOPE_INVALID;
    if (!valid_safety_envelope(profile))
        return Block::ENVELOPE_MISSING;
    CalibrationTiming timing{};
    if (!calibration_timing(profile, settings, &timing))
        return Block::ENVELOPE_INVALID;
    if (!timing.fits) {
        auto fastest = settings;
        fastest.slew_up_per_s = fastest.slew_down_per_s = kMaximumSlewPerS;
        return calibration_timing(profile, fastest, &timing) && timing.fits ? Block::CALIBRATION_TOO_SLOW
                                                                            : Block::ENVELOPE_INVALID;
    }
    return Block::NONE;
}

void Controller::start_commissioning_run(uint64_t now_ms) {
    // Analysis-policy defaults, NOT physical ignition/flame/overtemperature limits.
    // The trial interval itself must be explicitly supplied in the process profile.
    CalibrationTiming timing{};
    if (!calibration_timing(envelope_, settings_, &timing) || !timing.fits) {
        latch_fault(FAULT_COMMISSIONING, now_ms);
        return;
    }
    const uint16_t span = envelope_.reference_speed_code - envelope_.exploration_floor_code;
    calibration::Config c{};
    c.floor_code = envelope_.exploration_floor_code;
    c.ceiling_code = envelope_.reference_speed_code;
    c.step_code = std::max<uint16_t>(1, (span + 9) / 10);
    c.samples_per_window = 60;
    c.sample_period_ms = 1000;
    c.maximum_sample_gap_ms = 1600;
    c.maximum_sample_age_ms = kSensorStaleMs;
    c.maximum_no_new_sample_ms = kSensorStaleMs;
    c.maximum_channel_skew_ms = 250;
    c.command_arrival_timeout_ms =
        static_cast<uint64_t>(1000.0 / std::min(settings_.slew_up_per_s, settings_.slew_down_per_s)) + 120000;
    c.reference_timeout_ms = 1800000;
    c.point_timeout_ms = 1800000;
    // Reserve the pre-ignition and mandatory post-purge phases so the complete
    // deterministic procedure remains within the fixed six-hour global budget.
    c.maximum_total_duration_ms = timing.analysis_budget_ms;
    c.maximum_transition_settle_difference_ms = 120000;
    c.reference_noise_floor_milli_c = 250;
    c.reference_noise_ceiling_milli_c = 2000;
    c.reference_drift_floor_milli_c = 250;
    c.reference_drift_ceiling_milli_c = 1000;
    c.minimum_command_response_milli_c = 2000;
    c.reference_noise_multiplier = 3;
    c.reference_drift_multiplier = 3;
    c.repeat_mean_multiplier = 3;
    c.response_multiplier = 3;
    c.maximum_slew_up_codes_per_s = span * settings_.slew_up_per_s;
    c.maximum_slew_down_codes_per_s = span * settings_.slew_down_per_s;
    commissioning_config_ = c;
    commissioning_logged_phase_ = calibration::Phase::IDLE;
    if (!calibrator_.start(c, now_ms))
        latch_fault(FAULT_COMMISSIONING, now_ms);
}

void Controller::cancel_commissioning(calibration::Reason reason, uint64_t now_ms) {
    if (model_storage_sealed_ ||
        (!commissioning_requested_ && !model_candidate_.valid && !calibrator_.active()))
        return;
    calibrator_.cancel(reason);
    commissioning_finished_ms_ = now_ms;
    commissioning_requested_ = false;
    model_candidate_ = {};
    model_saved_ = false;
    model_storage_id_ = 0;
    model_storage_sealed_ = false;
    record("CALIBRATION_CANCELLED", now_ms);
}

void Controller::run_commissioning(uint64_t now_ms, const Sensors &sensors) {
    calibration::Observation observation{};
    observation.now_ms = now_ms;
    observation.exhaust = {sensors.exhaust.milli_c, sensors.exhaust.sample_ms, sensors.exhaust.status,
                           sensors.exhaust.valid};
    observation.hot_air = {sensors.air.milli_c, sensors.air.sample_ms, sensors.air.status, sensors.air.valid};
    observation.commanded_code = static_cast<uint16_t>(std::lround(run_speed_code_));
    observation.run_active = state_ == State::RUN;
    observation.fuel_commanded = outputs().fuel;
    observation.glow_commanded = outputs().glow;
    const auto cal = calibrator_.tick(observation);
    if (cal.phase != commissioning_logged_phase_) {
        commissioning_logged_phase_ = cal.phase;
        record(calibration::phase_name(cal.phase), now_ms);
    }
    if (cal.stop_requested) {
        if (cal.completed_pending_stop && cal.result.profile.valid) {
            const auto &p = cal.result.profile;
            model_candidate_ = {};
            model_candidate_.valid = true;
            model_candidate_.minimum_speed_code = p.min_code;
            model_candidate_.maximum_speed_code = p.max_code;
            // These are configured fractional-range/s caps, NOT learned thermal limits.
            // measurements.observed_slew_* records the command trajectory in codes/s
            // for diagnostics; it cannot establish a safe thermal slew from two sensors.
            model_candidate_.slew_up_per_s = settings_.slew_up_per_s;
            model_candidate_.slew_down_per_s = settings_.slew_down_per_s;
            model_candidate_.provenance = {envelope_fingerprint(envelope_), commissioning_started_ms_, now_ms,
                                           1};
            model_candidate_.measurements = measurements_from(p, now_ms - commissioning_started_ms_);
            model_candidate_.fingerprint = learned_model_fingerprint(model_candidate_);
            commissioning_requested_ = false;
            model_saved_ = false;
            model_storage_id_ = 0;
            model_storage_sealed_ = false;
            // The usable range is narrower than the tested interval: the same fractional
            // limit implies a slower absolute motion, never a faster untested transition.
            record("CALIBRATION_PENDING_COOLDOWN", now_ms);
            begin_stop(now_ms, true);
        } else {
            // Keep the calibrator's exact failure reason, not a generic cancel reason.
            commissioning_finished_ms_ = now_ms;
            commissioning_requested_ = false;
            record("CALIBRATION_REJECTED", now_ms);
            latch_fault(FAULT_COMMISSIONING, now_ms);
        }
        return;
    }
    const double span = commissioning_config_.ceiling_code - commissioning_config_.floor_code;
    const double target = std::clamp((cal.target_code - commissioning_config_.floor_code) / span, 0.0, 1.0);
    const auto command = regulator_.tick(now_ms, 20.0, 20.0, false, target, settings_.slew_up_per_s,
                                         settings_.slew_down_per_s);
    run_speed_code_ = commissioning_config_.floor_code + command.fraction * span;
}

bool Controller::pending_learned_model(LearnedOperatingModel *range) const {
    if (!range || !model_candidate_.valid || model_storage_id_ != 0 || state_ != State::IDLE || faults_ != 0)
        return false;
    *range = model_candidate_;
    return true;
}

bool Controller::begin_model_storage(uint64_t tx_id) {
    if (tx_id == 0 || !model_candidate_.valid || model_storage_id_ != 0 || model_storage_sealed_ ||
        state_ != State::IDLE || faults_ != 0)
        return false;
    model_storage_id_ = tx_id;
    model_saved_ = false;
    ++state_version_;
    if (state_version_ == 0)
        state_version_ = 1;
    return true;
}

bool Controller::seal_model_storage(uint64_t tx_id) {
    const bool cold = thermal_sensors_fresh(last_sensors_, last_tick_ms_) &&
                      cooling_complete(envelope_, last_sensors_, last_tick_ms_);
    if (tx_id == 0 || tx_id != model_storage_id_ || model_storage_sealed_ || !model_candidate_.valid ||
        state_ != State::IDLE || faults_ != 0 || !cold)
        return false;
    model_storage_sealed_ = true;
    ++state_version_;
    if (state_version_ == 0)
        state_version_ = 1;
    return true;
}

bool Controller::complete_model_storage(uint64_t tx_id, bool verified) {
    if (tx_id == 0 || tx_id != model_storage_id_ || !model_storage_sealed_ || !model_candidate_.valid)
        return false;
    if (verified && state_ != State::IDLE && state_ != State::FAULT)
        return false;

    const bool accepted = verified && install_learned_model(model_candidate_);
    commissioning_finished_ms_ = last_tick_ms_;
    model_candidate_ = {};
    commissioning_requested_ = false;
    model_storage_id_ = 0;
    model_storage_sealed_ = false;
    model_saved_ = accepted;
    model_storage_unconfirmed_ = !accepted;
    if (!accepted) {
        ++state_version_;
        if (state_version_ == 0)
            state_version_ = 1;
    }
    record(accepted ? "CALIBRATION_SAVED" : "CALIBRATION_STORAGE_UNCONFIRMED", last_tick_ms_);
    return accepted;
}

ModelStorageAbort Controller::abort_model_storage() {
    if (model_storage_sealed_)
        return ModelStorageAbort::COMMIT_IN_PROGRESS;
    if (!model_candidate_.valid)
        return ModelStorageAbort::NO_CANDIDATE;
    commissioning_finished_ms_ = last_tick_ms_;
    model_candidate_ = {};
    commissioning_requested_ = false;
    model_storage_id_ = 0;
    model_saved_ = false;
    model_storage_unconfirmed_ = true;
    calibrator_.cancel(calibration::Reason::EXTERNAL_CANCEL);
    ++state_version_;
    if (state_version_ == 0)
        state_version_ = 1;
    record("CALIBRATION_STORAGE_FAILED", last_tick_ms_);
    return ModelStorageAbort::ABORTED;
}
} // namespace sirokko
