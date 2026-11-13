#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sirokko {
enum class State : uint8_t;
struct SafetyEnvelope;
struct Sensors;
struct Outputs;

enum class TrialRecordReason : uint8_t {
    EMPTY,
    SELECT_PWM,
    SETTLING,
    INVALID,
    FULL,
    RANGE,
    IGNITION,
    PROFILE_CHANGED,
    READY
};
struct TrialResult {
    TrialRecordReason reason{TrialRecordReason::EMPTY};
    uint16_t samples{};
    uint16_t selected_pwm{};
    int32_t temperature_milli_c{};
    int32_t minimum_milli_c{};
    int32_t reference_milli_c{};
    bool complete{};
    bool saved{};
};

// Fixed RAM only. One point/second plus phase/PWM boundaries; cooling is not
// part of the temperature fit. Results survive browser disconnects, not reboot.
class TrialRecord {
  public:
    void begin(uint64_t now, const SafetyEnvelope &profile);
    void observe(uint64_t now, State state, const Outputs &outputs, const Sensors &sensors, uint32_t faults,
                 bool active);
    bool select(uint16_t pwm);
    void profile_changed(const SafetyEnvelope &profile);
    TrialResult result() const { return result_; }

  private:
    enum class Phase : uint8_t { COLD, GLOW, IGNITION, RUN };
    struct Point {
        uint32_t ms{};
        int32_t exhaust{};
        uint16_t pwm{};
        Phase phase{};
    };
    void calculate();
    static constexpr size_t capacity = 2048;
    std::array<Point, capacity> points_{};
    TrialResult result_{};
    uint64_t started_{}, last_observed_{}, last_sample_{}, fingerprint_{};
    uint64_t ignition_timeout_{};
    int32_t cooling_{}, maximum_{};
    uint16_t maximum_pwm_{};
    bool collecting_{}, have_sample_{}, invalid_{};
};
} // namespace sirokko
