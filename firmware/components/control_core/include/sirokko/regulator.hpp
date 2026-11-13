#pragma once
#include <cstdint>

namespace sirokko {
// Comfort regulator only. Safety logic must use unfiltered process sensors.
struct RegulatorConfig {
    double kp{0.05}; // working span / degree C
    double integral_time_s{1800};
    double tracking_time_s{300};
    double integration_deadband_c{0.2};
    double temperature_filter_s{10};
    double command_filter_s{10};
};
struct Regulation {
    double fraction{};
    double target_fraction{};
    double filtered_room_c{};
    double integral{};
};
class Regulator {
  public:
    explicit Regulator(RegulatorConfig config = {});
    void reset(uint64_t now_ms, double room_c, double applied_fraction);
    Regulation tick(uint64_t now_ms, double room_c, double setpoint_c, bool automatic, double manual_fraction,
                    double up_per_s, double down_per_s);
    Regulation value() const { return value_; }

  private:
    RegulatorConfig config_;
    Regulation value_{};
    uint64_t previous_ms_{};
    bool initialized_{};
    bool previous_automatic_{};
    bool have_mode_{};
};
} // namespace sirokko
