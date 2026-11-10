#include "sirokko/control.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace sirokko {
bool valid_model_revision(const LearnedOperatingModel& model, const LearnedModelRevision& revision) {
    return model.valid && revision.expected_revision == model.provenance.revision &&
        model.provenance.revision < std::numeric_limits<uint32_t>::max() &&
        revision.minimum_speed_code >= model.minimum_speed_code &&
        revision.maximum_speed_code <= model.maximum_speed_code &&
        revision.minimum_speed_code < revision.maximum_speed_code &&
        std::isfinite(revision.slew_up_per_s) && std::isfinite(revision.slew_down_per_s) &&
        revision.slew_up_per_s >= 0.0001 && revision.slew_down_per_s >= 0.0001 &&
        revision.slew_up_per_s <= model.slew_up_per_s &&
        revision.slew_down_per_s <= model.slew_down_per_s;
}

bool valid_settings(const Settings& s) {
    return s.room_setpoint_milli_c >= 15000 && s.room_setpoint_milli_c <= 30000 &&
        s.room_setpoint_milli_c % 1000 == 0 && s.duration_ms <= kMaximumTimerMs &&
        (s.mode == Mode::TEMPERATURE || s.mode == Mode::MANUAL) &&
        s.manual_percent >= 5 && s.manual_percent <= 100 && s.manual_percent % 5 == 0 &&
        std::isfinite(s.slew_up_per_s) && std::isfinite(s.slew_down_per_s) &&
        s.slew_up_per_s >= 0.0001 && s.slew_up_per_s <= 0.005 &&
        s.slew_down_per_s >= 0.0001 && s.slew_down_per_s <= 0.005;
}
bool valid_safety_envelope(const SafetyEnvelope& p) {
    const auto timer = [](uint64_t v) { return v > 0 && v <= kMaximumTimerMs; };
    return timer(p.recovery_purge_ms) && timer(p.prepurge_ms) && timer(p.preheat_ms) &&
        timer(p.ignition_ms) && timer(p.verify_ms) && timer(p.postpurge_ms) &&
        p.maximum_exhaust_milli_c > 0 && p.maximum_exhaust_milli_c <= 600000 &&
        p.maximum_air_milli_c > 0 && p.maximum_air_milli_c <= 600000 &&
        p.maximum_purge_exit_exhaust_milli_c > 0 &&
        p.maximum_purge_exit_exhaust_milli_c < p.maximum_exhaust_milli_c &&
        p.maximum_purge_exit_air_milli_c > 0 &&
        p.maximum_purge_exit_air_milli_c < p.maximum_air_milli_c &&
        p.minimum_ignition_rise_milli_c > 0 && p.minimum_ignition_rise_milli_c < p.maximum_exhaust_milli_c &&
        p.minimum_ignition_air_rise_milli_c > 0 && p.minimum_ignition_air_rise_milli_c < p.maximum_air_milli_c &&
        p.minimum_run_exhaust_milli_c > 0 && p.minimum_run_exhaust_milli_c < p.maximum_exhaust_milli_c &&
        p.purge_speed_code > 0 && p.purge_speed_code <= kPwmMaximum &&
        p.reference_speed_code > 0 && p.reference_speed_code <= kPwmMaximum &&
        (p.exploration_floor_code == 0 ||
         (p.exploration_floor_code < p.reference_speed_code &&
          static_cast<uint32_t>(p.reference_speed_code) - p.exploration_floor_code >= 2U));
}
uint64_t envelope_fingerprint(const SafetyEnvelope& p) {
    // Explicit field order, independent of padding, ABI, and bool representation.
    const uint64_t fields[] = {p.recovery_purge_ms,p.prepurge_ms,p.preheat_ms,p.ignition_ms,
        p.verify_ms,p.postpurge_ms,static_cast<uint32_t>(p.maximum_purge_exit_exhaust_milli_c),
        static_cast<uint32_t>(p.maximum_purge_exit_air_milli_c),static_cast<uint32_t>(p.maximum_exhaust_milli_c),
        static_cast<uint32_t>(p.maximum_air_milli_c),static_cast<uint32_t>(p.minimum_ignition_rise_milli_c),
        static_cast<uint32_t>(p.minimum_run_exhaust_milli_c),p.purge_speed_code,p.reference_speed_code,
        p.exploration_floor_code,static_cast<uint32_t>(p.minimum_ignition_air_rise_milli_c)};
    uint64_t hash = 1469598103934665603ULL;
    for (uint64_t v : fields) for (unsigned b=0;b<8;++b) hash=(hash^static_cast<uint8_t>(v>>(b*8)))*1099511628211ULL;
    return hash;
}

uint64_t learned_model_fingerprint(const LearnedOperatingModel& model) {
    const auto bits = [](double value) {
        uint64_t result{};
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    };
    const auto& p = model.provenance;
    const auto& m = model.measurements;
    const uint64_t fields[] = {
        model.valid, model.minimum_speed_code, model.maximum_speed_code,
        bits(model.slew_up_per_s), bits(model.slew_down_per_s),
        p.safety_envelope_fingerprint, p.commissioning_started_ms,
        p.commissioning_completed_ms, p.revision,
        m.elapsed_ms, m.reference_settled_after_ms, m.lowest_tested_settled_after_ms,
        static_cast<uint32_t>(m.reference_exhaust_mean_milli_c),
        static_cast<uint32_t>(m.reference_air_mean_milli_c),
        static_cast<uint32_t>(m.lowest_tested_exhaust_mean_milli_c),
        static_cast<uint32_t>(m.lowest_tested_air_mean_milli_c),
        m.reference_exhaust_noise_milli_c, m.reference_air_noise_milli_c,
        m.reference_exhaust_drift_milli_c, m.reference_air_drift_milli_c,
        bits(m.observed_slew_up_codes_per_s), bits(m.observed_slew_down_codes_per_s),
        m.tested_point_count,
    };
    uint64_t hash = 1469598103934665603ULL;
    for (uint64_t value : fields)
        for (unsigned byte = 0; byte < 8; ++byte)
            hash = (hash ^ static_cast<uint8_t>(value >> (byte * 8))) * 1099511628211ULL;
    return hash;
}
} // namespace sirokko
