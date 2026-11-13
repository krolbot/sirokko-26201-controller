#include "sirokko/control.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace sirokko {
namespace {
bool sample_fresh(const SensorSample &s, uint64_t now, uint64_t age) {
    return s.sample_ms <= now && now - s.sample_ms <= age;
}
} // namespace
bool thermocouple_valid(const SensorSample &s) {
    return s.valid && !s.status && s.milli_c >= kThermocoupleMinimumMilliC &&
           s.milli_c < kProcessMeasurementCeilingMilliC;
}
bool room_temperature_valid(const SensorSample &s) {
    return s.valid && !s.status && s.milli_c >= -55000 && s.milli_c <= 125000;
}
bool thermocouple_fresh(const SensorSample &s, uint64_t now) {
    return thermocouple_valid(s) && sample_fresh(s, now, kSensorStaleMs);
}
bool room_temperature_fresh(const SensorSample &s, uint64_t now) {
    return room_temperature_valid(s) && sample_fresh(s, now, kRoomSensorStaleMs);
}

uint8_t minimum_manual_percent(const LearnedOperatingModel &model) {
    if (!model.valid || !model.maximum_speed_code)
        return kManualStepPercent;
    const unsigned first_step =
        (20U * model.minimum_speed_code + model.maximum_speed_code - 1) / model.maximum_speed_code;
    return static_cast<uint8_t>(first_step * kManualStepPercent);
}

bool valid_model_revision(const LearnedOperatingModel &model, const LearnedModelRevision &revision) {
    return model.valid && revision.expected_revision == model.provenance.revision &&
           model.provenance.revision < std::numeric_limits<uint32_t>::max() &&
           revision.minimum_speed_code >= model.minimum_speed_code &&
           revision.maximum_speed_code <= model.maximum_speed_code &&
           revision.minimum_speed_code < revision.maximum_speed_code &&
           std::isfinite(revision.slew_up_per_s) && std::isfinite(revision.slew_down_per_s) &&
           revision.slew_up_per_s >= kMinimumSlewPerS && revision.slew_down_per_s >= kMinimumSlewPerS &&
           revision.slew_up_per_s <= model.slew_up_per_s && revision.slew_down_per_s <= model.slew_down_per_s;
}

bool valid_settings(const Settings &s) {
    return s.room_setpoint_milli_c >= kRoomSetpointMinimumMilliC &&
           s.room_setpoint_milli_c <= kRoomSetpointMaximumMilliC &&
           s.room_setpoint_milli_c % kRoomSetpointStepMilliC == 0 && s.duration_ms <= kMaximumTimerMs &&
           (s.mode == Mode::TEMPERATURE || s.mode == Mode::MANUAL) &&
           s.manual_percent >= kManualStepPercent && s.manual_percent <= 100 &&
           s.manual_percent % kManualStepPercent == 0 && std::isfinite(s.slew_up_per_s) &&
           std::isfinite(s.slew_down_per_s) && s.slew_up_per_s >= kMinimumSlewPerS &&
           s.slew_up_per_s <= kMaximumSlewPerS && s.slew_down_per_s >= kMinimumSlewPerS &&
           s.slew_down_per_s <= kMaximumSlewPerS;
}
bool valid_profile_draft(const SafetyEnvelope &p) {
    for (auto t : {p.preheat_ms, p.ignition_timeout_ms, p.postpurge_ms})
        if (t > kMaximumTimerMs)
            return false;
    if ((p.preheat_ms && (p.preheat_ms < kPreheatMinimumMs || p.preheat_ms > kPreheatMaximumMs)) ||
        (p.postpurge_ms && p.postpurge_ms < kPostpurgeMinimumMs) ||
        p.ignition_timeout_ms > kFiringStartupMaximumMs ||
        (p.ignition_timeout_ms && p.ignition_timeout_ms <= kTemperatureConfirmationMs))
        return false;
    for (auto t : {p.maximum_exhaust_milli_c, p.minimum_run_exhaust_milli_c})
        if (t < 0 || t >= kProcessMeasurementCeilingMilliC)
            return false;
    if (p.cooling_exhaust_milli_c <= 0 || p.cooling_exhaust_milli_c >= kProcessMeasurementCeilingMilliC)
        return false;
    if ((p.maximum_exhaust_milli_c && (p.cooling_exhaust_milli_c >= p.maximum_exhaust_milli_c ||
                                       p.minimum_run_exhaust_milli_c >= p.maximum_exhaust_milli_c)))
        return false;
    if (p.minimum_run_exhaust_milli_c && p.minimum_run_exhaust_milli_c <= p.cooling_exhaust_milli_c)
        return false;
    return p.purge_speed_code <= kPwmMaximum && p.reference_speed_code <= kPwmMaximum &&
           p.exploration_floor_code <= kPwmMaximum &&
           (!p.exploration_floor_code || !p.reference_speed_code ||
            static_cast<int>(p.reference_speed_code) - p.exploration_floor_code >= 2);
}
bool valid_safety_envelope(const SafetyEnvelope &p) {
    return valid_profile_draft(p) && p.maximum_exhaust_milli_c && p.postpurge_ms && p.purge_speed_code &&
           p.preheat_ms && p.ignition_timeout_ms && p.minimum_run_exhaust_milli_c && p.reference_speed_code &&
           p.exploration_floor_code;
}
bool cooling_complete(const SafetyEnvelope &p, const Sensors &s, uint64_t now) {
    return p.cooling_exhaust_milli_c > 0 && p.cooling_exhaust_milli_c < kProcessMeasurementCeilingMilliC &&
           thermocouple_fresh(s.exhaust, now) && thermocouple_fresh(s.air, now) &&
           s.exhaust.milli_c < p.cooling_exhaust_milli_c;
}

uint64_t envelope_fingerprint(const SafetyEnvelope &p) {
    // Explicit field order, independent of padding, ABI, and bool representation.
    const uint64_t fields[] = {p.preheat_ms,
                               p.ignition_timeout_ms,
                               p.postpurge_ms,
                               static_cast<uint32_t>(p.cooling_exhaust_milli_c),
                               static_cast<uint32_t>(p.maximum_exhaust_milli_c),
                               static_cast<uint32_t>(p.minimum_run_exhaust_milli_c),
                               p.purge_speed_code,
                               p.reference_speed_code,
                               p.exploration_floor_code};
    uint64_t hash = 1469598103934665603ULL;
    for (uint64_t v : fields)
        for (unsigned b = 0; b < 8; ++b)
            hash = (hash ^ static_cast<uint8_t>(v >> (b * 8))) * 1099511628211ULL;
    return hash;
}

uint64_t learned_model_fingerprint(const LearnedOperatingModel &model) {
    const auto bits = [](double value) {
        uint64_t result{};
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    };
    const auto &p = model.provenance;
    const auto &m = model.measurements;
    const uint64_t fields[] = {
        model.valid,
        model.minimum_speed_code,
        model.maximum_speed_code,
        bits(model.slew_up_per_s),
        bits(model.slew_down_per_s),
        p.safety_envelope_fingerprint,
        p.commissioning_started_ms,
        p.commissioning_completed_ms,
        p.revision,
        m.elapsed_ms,
        m.reference_settled_after_ms,
        m.lowest_tested_settled_after_ms,
        static_cast<uint32_t>(m.reference_exhaust_mean_milli_c),
        static_cast<uint32_t>(m.reference_air_mean_milli_c),
        static_cast<uint32_t>(m.lowest_tested_exhaust_mean_milli_c),
        static_cast<uint32_t>(m.lowest_tested_air_mean_milli_c),
        m.reference_exhaust_noise_milli_c,
        m.reference_air_noise_milli_c,
        m.reference_exhaust_drift_milli_c,
        m.reference_air_drift_milli_c,
        bits(m.observed_slew_up_codes_per_s),
        bits(m.observed_slew_down_codes_per_s),
        m.tested_point_count,
    };
    uint64_t hash = 1469598103934665603ULL;
    for (uint64_t value : fields)
        for (unsigned byte = 0; byte < 8; ++byte)
            hash = (hash ^ static_cast<uint8_t>(value >> (byte * 8))) * 1099511628211ULL;
    return hash;
}
} // namespace sirokko
