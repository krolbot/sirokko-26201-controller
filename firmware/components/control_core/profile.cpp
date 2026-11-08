#include "sirokko/control.hpp"
#include <cmath>

namespace sirokko {
bool valid_settings(const Settings& s) {
    return s.room_setpoint_milli_c >= 15000 && s.room_setpoint_milli_c <= 30000 &&
        s.room_setpoint_milli_c % 1000 == 0 && s.duration_ms <= kMaximumTimerMs &&
        (s.mode == Mode::TEMPERATURE || s.mode == Mode::MANUAL) &&
        s.manual_percent >= 5 && s.manual_percent <= 100 && s.manual_percent % 5 == 0 &&
        std::isfinite(s.slew_up_per_s) && std::isfinite(s.slew_down_per_s) &&
        s.slew_up_per_s >= 0.0001 && s.slew_up_per_s <= 0.005 &&
        s.slew_down_per_s >= 0.0001 && s.slew_down_per_s <= 0.005;
}
bool valid_process_profile(const ProcessProfile& p) {
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
        p.run_speed_code > 0 && p.run_speed_code <= kPwmMaximum &&
        (p.calibration_floor_speed_code == 0 ||
         (p.calibration_floor_speed_code < p.run_speed_code &&
          static_cast<uint32_t>(p.run_speed_code) - p.calibration_floor_speed_code >= 2U));
}
uint64_t process_fingerprint(const ProcessProfile& p) {
    // Explicit field order, independent of padding, ABI, and bool representation.
    const uint64_t fields[] = {p.recovery_purge_ms,p.prepurge_ms,p.preheat_ms,p.ignition_ms,
        p.verify_ms,p.postpurge_ms,static_cast<uint32_t>(p.maximum_purge_exit_exhaust_milli_c),
        static_cast<uint32_t>(p.maximum_purge_exit_air_milli_c),static_cast<uint32_t>(p.maximum_exhaust_milli_c),
        static_cast<uint32_t>(p.maximum_air_milli_c),static_cast<uint32_t>(p.minimum_ignition_rise_milli_c),
        static_cast<uint32_t>(p.minimum_run_exhaust_milli_c),p.purge_speed_code,p.run_speed_code,
        p.calibration_floor_speed_code,static_cast<uint32_t>(p.minimum_ignition_air_rise_milli_c)};
    uint64_t hash = 1469598103934665603ULL;
    for (uint64_t v : fields) for (unsigned b=0;b<8;++b) hash=(hash^static_cast<uint8_t>(v>>(b*8)))*1099511628211ULL;
    return hash;
}
} // namespace sirokko
