#include "sirokko/control.hpp"
#include <cmath>

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

uint8_t minimum_manual_percent(const SafetyEnvelope &profile) {
    if (!profile.maximum_speed_code || !profile.minimum_speed_code)
        return kManualStepPercent;
    const unsigned first_step =
        (20U * profile.minimum_speed_code + profile.maximum_speed_code - 1) / profile.maximum_speed_code;
    return static_cast<uint8_t>(first_step * kManualStepPercent);
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
    if (p.exhaust_low_delay_ms < 1000 || p.exhaust_low_delay_ms > 30000) return false;
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
    return p.purge_speed_code <= kPwmMaximum && p.maximum_speed_code <= kPwmMaximum &&
           p.minimum_speed_code <= kPwmMaximum &&
           (!p.minimum_speed_code || !p.maximum_speed_code || p.minimum_speed_code <= p.maximum_speed_code);
}
bool valid_safety_envelope(const SafetyEnvelope &p) {
    return valid_profile_draft(p) && p.postpurge_ms && p.purge_speed_code && p.preheat_ms &&
           p.ignition_timeout_ms > kIgnitionGlowMs + kTemperatureConfirmationMs &&
           p.minimum_run_exhaust_milli_c && p.maximum_speed_code && p.minimum_speed_code;
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
                               p.maximum_speed_code,
                               p.minimum_speed_code, p.exhaust_low_delay_ms};
    uint64_t hash = 1469598103934665603ULL;
    for (uint64_t v : fields)
        for (unsigned b = 0; b < 8; ++b)
            hash = (hash ^ static_cast<uint8_t>(v >> (b * 8))) * 1099511628211ULL;
    return hash;
}

} // namespace sirokko
