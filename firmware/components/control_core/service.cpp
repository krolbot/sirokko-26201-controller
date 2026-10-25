#include "sirokko/service.hpp"
namespace sirokko {
ServiceResult ServiceLease::eligible(const Status& s, const Inputs& in, const Config& c, bool healthy) {
    if (in.heat_enable || in.stop_active || (s.state != State::WAIT_COOLDOWN && s.state != State::LOCKOUT) ||
        !s.recovery_complete) return ServiceResult::BUSY;
    if (validate_config(c) != CONFIG_OK || !in.configuration_ok) return ServiceResult::CONFIG;
    if (!healthy) return ServiceResult::OUTPUT;
    const SensorSample samples[] = {in.exhaust, in.hot_air, in.room};
    for (const auto& v : samples) {
        if (v.quality != SensorQuality::OK || static_cast<uint32_t>(in.now_ms - v.sample_ms) > c.sensor_stale_ms ||
            v.milli_c < c.sensor_min_milli_c || v.milli_c > c.sensor_max_milli_c) return ServiceResult::SENSOR;
    }
    if (in.exhaust.milli_c > c.cool_exhaust_milli_c || in.hot_air.milli_c > c.cool_air_milli_c) return ServiceResult::HOT;
    const auto& o = s.outputs;
    if (o.fuel_command || o.fuel_permit || o.glow_command || o.fan_request || o.speed_permille) return ServiceResult::OUTPUT;
    return ServiceResult::OK;
}
ServiceResult ServiceLease::acquire(const Status& s, const Inputs& in, const Config& c, bool healthy) {
    if (held_) return ServiceResult::BUSY;
    const auto result = eligible(s, in, c, healthy);
    held_ = result == ServiceResult::OK;
    return result;
}
const char* service_result_name(ServiceResult r) {
    switch (r) {
        case ServiceResult::OK: return "OK";
        case ServiceResult::BUSY: return "BUSY";
        case ServiceResult::CONFIG: return "CONFIG";
        case ServiceResult::SENSOR: return "SENSOR";
        case ServiceResult::HOT: return "HOT";
        case ServiceResult::OUTPUT: return "OUTPUT";
        case ServiceResult::REPLAY: return "REPLAY";
        case ServiceResult::INVALID: return "INVALID";
        case ServiceResult::UNAUTHORIZED: return "UNAUTHORIZED";
        case ServiceResult::IO: return "IO";
        case ServiceResult::HASH: return "HASH";
        case ServiceResult::TARGET: return "TARGET";
        case ServiceResult::VERSION: return "VERSION";
    }
    return "INVALID";
}
} // namespace sirokko
