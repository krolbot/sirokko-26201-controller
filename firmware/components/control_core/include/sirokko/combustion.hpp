#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sirokko {
struct Sensors;
namespace combustion {
// Analysis policy, not certified flame-detector timing or physical heater limits.
constexpr size_t kWindowSamples = 60;
constexpr uint64_t kSamplePeriodMs = 500;
constexpr uint64_t kConfirmationMs = 1500;
enum class Phase : uint8_t { OFF, LEARNING, WATCHING, SUSPECT, LOST };
struct Snapshot {
    Phase phase{Phase::OFF};
    uint8_t samples{};
    int32_t reference_exhaust_milli_c{};
    int32_t reference_air_milli_c{};
    int32_t drop_limit_milli_c{};
    int32_t exhaust_slope_milli_c_per_s{};
    int32_t air_slope_milli_c_per_s{};
    uint64_t suspect_ms{};
};

// A frozen reference for comparable commands. A falling temperature never lowers it.
// Independent absolute limits remain owned by Controller.
class Monitor {
  public:
    void reset(int32_t minimum_exhaust_milli_c, uint16_t reference_speed_code);
    Snapshot observe(uint64_t now_ms, const Sensors &sensors, uint16_t command);
    Snapshot snapshot() const { return snapshot_; }

  private:
    struct Point {
        int32_t exhaust{}, air{};
        uint64_t at_ms{};
    };
    void clear_window();
    void learn();
    const Point &point(size_t index) const;
    std::array<Point, kWindowSamples> window_{};
    size_t count_{}, next_{};
    uint16_t command_{}, command_tolerance_{1};
    bool have_command_{}, have_timestamps_{};
    uint64_t last_exhaust_ms_{}, last_air_ms_{}, last_now_ms_{}, suspect_started_ms_{};
    uint8_t suspect_samples_{};
    int32_t minimum_exhaust_milli_c_{};
    Snapshot snapshot_{};
};
} // namespace combustion
} // namespace sirokko
