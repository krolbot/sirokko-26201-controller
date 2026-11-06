#include "sirokko/regulator.hpp"
#include <algorithm>
#include <cmath>

namespace sirokko {
namespace {
double unit(double x) { return std::clamp(x, 0.0, 1.0); }
}
Regulator::Regulator(RegulatorConfig config) : config_(config) {}
void Regulator::reset(uint64_t now_ms, double room_c, double applied_fraction) {
    previous_ms_ = now_ms;
    value_ = {unit(applied_fraction), unit(applied_fraction), room_c, 0};
    initialized_ = std::isfinite(room_c) && std::isfinite(applied_fraction);
    have_mode_ = false;
}
Regulation Regulator::tick(uint64_t now_ms, double room_c, double setpoint_c, bool automatic,
                           double manual_fraction, double up_per_s, double down_per_s) {
    if (!std::isfinite(room_c) || !std::isfinite(setpoint_c) ||
        !std::isfinite(manual_fraction) || !std::isfinite(up_per_s) || !std::isfinite(down_per_s) ||
        up_per_s <= 0 || down_per_s <= 0) return value_;
    if (!initialized_) reset(now_ms, room_c, manual_fraction);
    if (now_ms <= previous_ms_) return value_;
    // A delayed call cannot catch up with a large discontinuous output step.
    const double dt = std::min(static_cast<double>(now_ms - previous_ms_) / 1000.0, 1.0);
    previous_ms_ = now_ms;
    value_.filtered_room_c += (room_c - value_.filtered_room_c) * (-std::expm1(-dt / config_.temperature_filter_s));
    const double error = setpoint_c - value_.filtered_room_c;
    const double proportional = config_.kp * error;
    const bool entering_auto = automatic && have_mode_ && !previous_automatic_;
    if (entering_auto) value_.integral = value_.fraction - proportional;
    previous_automatic_ = automatic;
    have_mode_ = true;
    if (automatic) {
        const double raw = proportional + value_.integral;
        value_.target_fraction = unit(raw);
        const double i_error = std::copysign(std::max(std::abs(error) - config_.integration_deadband_c, 0.0), error);
        value_.integral += dt * (config_.kp * i_error / config_.integral_time_s +
                                (value_.fraction - raw) / config_.tracking_time_s);
        // Signed I is intentional: [0,1] clamping would break bumpless transfer.
    } else {
        value_.target_fraction = unit(manual_fraction);
        value_.integral = value_.fraction - proportional;
    }
    const double change = (value_.target_fraction - value_.fraction) * (-std::expm1(-dt / config_.command_filter_s));
    value_.fraction = unit(value_.fraction + std::clamp(change, -down_per_s * dt, up_per_s * dt));
    return value_;
}
} // namespace sirokko
