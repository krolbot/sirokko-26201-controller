#include "sirokko/control.hpp"
#include <algorithm>
#include <limits>

namespace sirokko {
void TrialRecord::begin(uint64_t now, const SafetyEnvelope &profile) {
    result_ = {};
    result_.reason = TrialRecordReason::SELECT_PWM;
    started_ = last_observed_ = now;
    last_sample_ = 0;
    fingerprint_ = envelope_fingerprint(profile);
    ignition_timeout_ = profile.ignition_timeout_ms;
    cooling_ = profile.cooling_exhaust_milli_c;
    maximum_ = profile.maximum_exhaust_milli_c;
    maximum_pwm_ = profile.maximum_speed_code;
    collecting_ = true;
    have_sample_ = invalid_ = false;
}

void TrialRecord::observe(uint64_t now, State state, const Outputs &outputs, const Sensors &sensors,
                          uint32_t faults, bool active) {
    if (!collecting_)
        return;
    if (now < last_observed_ || now - last_observed_ > kSensorStaleMs || faults ||
        !thermocouple_fresh(sensors.exhaust, now) || !thermocouple_fresh(sensors.air, now)) {
        invalid_ = true;
        result_.reason = TrialRecordReason::INVALID;
    }
    last_observed_ = now;
    if (!active) {
        collecting_ = false;
        result_.complete = true;
        calculate();
        return;
    }
    // Preserve the record throughout cooling without consuming sample capacity.
    if (state != State::TRIAL_READY && state != State::PREHEAT && state != State::IGNITION &&
        state != State::RUN)
        return;
    if (invalid_)
        return;
    const auto phase = state == State::RUN ? Phase::RUN
                       : outputs.fuel      ? (outputs.glow ? Phase::GLOW : Phase::IGNITION)
                                           : Phase::COLD;
    const auto elapsed = now - started_;
    if (elapsed > UINT32_MAX) {
        invalid_ = true;
        result_.reason = TrialRecordReason::INVALID;
        return;
    }
    const bool boundary = result_.samples && (points_[result_.samples - 1].phase != phase ||
                                              points_[result_.samples - 1].pwm != outputs.speed_code);
    if (have_sample_ && sensors.exhaust.sample_ms == last_sample_ && !boundary)
        return;
    if (result_.samples && !boundary && elapsed - points_[result_.samples - 1].ms < 1000)
        return;
    if (result_.samples == capacity) {
        invalid_ = true;
        result_.reason = TrialRecordReason::FULL;
        return;
    }
    points_[result_.samples++] = {static_cast<uint32_t>(elapsed), sensors.exhaust.milli_c, outputs.speed_code,
                                  phase};
    last_sample_ = sensors.exhaust.sample_ms;
    have_sample_ = true;
    calculate();
}

bool TrialRecord::select(uint16_t pwm) {
    if (!collecting_ || !pwm || pwm > kPwmMaximum)
        return false;
    result_.selected_pwm = pwm;
    calculate();
    return true;
}

void TrialRecord::calculate() {
    if (invalid_)
        return;
    if (!result_.selected_pwm) {
        result_.reason = TrialRecordReason::SELECT_PWM;
        return;
    }
    result_.reason = TrialRecordReason::SETTLING;
    const auto pwm = result_.selected_pwm;
    if (pwm > maximum_pwm_) {
        result_.reason = TrialRecordReason::RANGE;
        return;
    }
    int32_t baseline = std::numeric_limits<int32_t>::min();
    int32_t low = std::numeric_limits<int32_t>::max();
    size_t first_fuel = result_.samples;
    size_t segment_start = 0, segment_end = 0;
    bool in_segment = false, have_segment = false;
    for (size_t i = 0; i < result_.samples; ++i) {
        const auto &p = points_[i];
        if (p.phase != Phase::COLD && first_fuel == result_.samples)
            first_fuel = i;
        if (first_fuel == result_.samples)
            baseline = std::max(baseline, p.exhaust);
        if (p.phase == Phase::RUN && p.pwm >= pwm)
            low = std::min(low, p.exhaust);
        if (p.phase != Phase::RUN || p.pwm != pwm) {
            in_segment = false;
            continue;
        }
        if (!in_segment)
            segment_start = i;
        in_segment = have_segment = true;
        segment_end = i;
    }
    if (!have_segment || points_[segment_end].ms - points_[segment_start].ms < 120000)
        return;
    while (segment_start < segment_end && points_[segment_end].ms - points_[segment_start + 1].ms >= 120000)
        ++segment_start;
    auto lo = points_[segment_end].exhaust, hi = lo;
    for (size_t i = segment_start; i <= segment_end; ++i) {
        lo = std::min(lo, points_[i].exhaust);
        hi = std::max(hi, points_[i].exhaust);
    }
    if (hi - lo > 5000)
        return;
    if (!first_fuel || first_fuel == result_.samples || low == std::numeric_limits<int32_t>::max())
        return;
    const auto reference = std::max(cooling_, baseline);
    // Initial proposal, not a flame detector: midpoint between the cold/cooling
    // reference and the minimum confirmed operation, including PWM transitions.
    const auto proposed = static_cast<int32_t>((static_cast<int64_t>(reference) + low) / 2000) * 1000;
    if (proposed <= reference || proposed >= low || (maximum_ && proposed >= maximum_)) {
        result_.reason = TrialRecordReason::RANGE;
        return;
    }
    uint32_t reached = 0;
    bool reaching = false, ignition_ok = false;
    for (size_t i = first_fuel; i < result_.samples; ++i) {
        const auto &p = points_[i];
        if (p.ms - points_[first_fuel].ms >= ignition_timeout_)
            break;
        if ((p.phase != Phase::IGNITION && p.phase != Phase::RUN) || p.exhaust < proposed)
            reaching = false;
        else {
            if (!reaching) {
                reaching = true;
                reached = p.ms;
            }
            if (p.ms - reached >= kTemperatureConfirmationMs)
                ignition_ok = true;
        }
    }
    if (!ignition_ok) {
        result_.reason = TrialRecordReason::IGNITION;
        return;
    }
    result_.temperature_milli_c = proposed;
    result_.minimum_milli_c = low;
    result_.reference_milli_c = reference;
    result_.reason = TrialRecordReason::READY;
}

void TrialRecord::profile_changed(const SafetyEnvelope &profile) {
    if (!result_.samples)
        return;
    if (result_.complete && result_.reason == TrialRecordReason::READY &&
        profile.minimum_speed_code == result_.selected_pwm &&
        profile.minimum_run_exhaust_milli_c == result_.temperature_milli_c) {
        result_.saved = true;
    } else if (envelope_fingerprint(profile) != fingerprint_) {
        result_.saved = false;
        invalid_ = true;
        result_.reason = TrialRecordReason::PROFILE_CHANGED;
    }
}
} // namespace sirokko
