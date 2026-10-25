#pragma once

#include <cstdint>

namespace sirokko {

inline constexpr uint32_t kControlLoopPeriodMs = 20;
inline constexpr uint32_t kControlLoopJitterBudgetMs = 20;
inline constexpr uint32_t kWatchdogMinimumTimeoutMs = 170;
inline constexpr uint32_t kHeartbeatMaximumFallingEdgeMs = 100;
inline constexpr uint32_t kConfigSchemaVersion = 3;
inline constexpr uint32_t kPwmMaximumFrequencyHz = 20000;
inline constexpr uint32_t kPwmClockHz = 80000000;
inline constexpr uint32_t kPwmResolutionBits = 10;
inline constexpr uint16_t kPwmMaximumPermille = 800;
// ESP-IDF 5.5.2: 18-bit divider with 8 fractional bits, APB clock, 10-bit duty.
inline constexpr bool pwm_frequency_supported(uint32_t hz) {
    if (hz == 0 || hz > kPwmMaximumFrequencyHz) return false;
    const uint64_t precision = 1u << kPwmResolutionBits;
    const uint64_t divider = ((static_cast<uint64_t>(kPwmClockHz) << 8) + hz * precision / 2) /
                             (hz * precision);
    return divider >= 256 && divider <= 0x3ffff;
}
inline constexpr bool speed_supported(uint16_t permille) {
    return permille <= kPwmMaximumPermille || permille == 1000;
}

enum class State : uint8_t {
    BOOT_SELF_TEST,
    RECOVERY_PURGE,
    WAIT_COOLDOWN,
    PREPURGE,
    PREHEAT,
    SPINUP,
    IGNITION_TRIAL,
    FLAME_CONFIRM,
    RUN_HIGH,
    RUN_LOW,
    POST_PURGE,
    LOCKOUT,
};

enum class SensorQuality : uint8_t {
    OK,
    MISSING,
    HARDWARE_FAULT,
    CRC_ERROR,
    TIMEOUT,
    IMPLAUSIBLE,
    POWER_ON_DEFAULT,
};

struct SensorSample {
    int32_t milli_c{};
    uint32_t sample_ms{};
    SensorQuality quality{SensorQuality::MISSING};
};

struct Config {
    uint32_t schema_version{};
    bool commissioning_accepted{};

    uint32_t boot_self_test_timeout_ms{};
    uint32_t recovery_purge_min_ms{};
    uint32_t prepurge_ms{};
    uint32_t preheat_ms{};
    uint32_t spinup_ms{};
    uint32_t ignition_window_ms{};
    uint32_t flame_confirm_ms{};
    uint32_t postpurge_min_ms{};
    uint32_t stage_min_ms{};
    uint32_t restart_inhibit_ms{};
    uint32_t glow_max_ms{};
    uint32_t sensor_stale_ms{};
    uint32_t heartbeat_toggle_ms{};

    int32_t sensor_min_milli_c{};
    int32_t sensor_max_milli_c{};
    int32_t exhaust_overtemp_milli_c{};
    int32_t air_overtemp_milli_c{};
    int32_t cool_exhaust_milli_c{};
    int32_t cool_air_milli_c{};
    int32_t hot_start_exhaust_milli_c{};
    int32_t hot_start_air_milli_c{};
    int32_t flame_rise_milli_c{};
    int32_t flame_min_exhaust_milli_c{};
    int32_t flame_hold_exhaust_milli_c{};

    int32_t setpoint_min_milli_c{};
    int32_t setpoint_max_milli_c{};
    int32_t setpoint_default_milli_c{};
    int32_t start_delta_milli_c{};
    int32_t high_to_low_delta_milli_c{};
    int32_t low_to_high_delta_milli_c{};
    int32_t stop_delta_milli_c{};
    uint32_t pwm_frequency_hz{};
    uint16_t recovery_purge_pwm_permille{};
    uint16_t low_pwm_permille{};
    uint32_t profile_revision{};
    bool run_low_qualified{}; // Measured combustion qualification, not electrical PWM capability.
};

enum ConfigError : uint32_t {
    CONFIG_OK = 0,
    CONFIG_SCHEMA = 1u << 0,
    CONFIG_NOT_ACCEPTED = 1u << 1,
    CONFIG_DURATION = 1u << 2,
    CONFIG_GLOW_BUDGET = 1u << 3,
    CONFIG_SENSOR_RANGE = 1u << 4,
    CONFIG_TEMPERATURE_ORDER = 1u << 5,
    CONFIG_FLAME_CRITERIA = 1u << 6,
    CONFIG_SETPOINT = 1u << 7,
    CONFIG_REGULATOR_ORDER = 1u << 8,
    CONFIG_PWM = 1u << 9,
    CONFIG_WATCHDOG_TIMING = 1u << 10,
};

uint32_t heartbeat_worst_case_falling_edge_ms(uint32_t toggle_ms);
uint32_t validate_config(const Config& config, bool require_acceptance = true);

struct Inputs {
    uint32_t now_ms{};
    // Latched automatic-heat enable produced by the debounced local START/STOP adapter.
    bool heat_enable{};
    bool stop_active{};
    bool reset_request{};
    bool safety_ok{};
    bool local_ui_ok{true};
    bool output_ok{true};
    bool configuration_ok{true};
    int32_t setpoint_milli_c{};
    int32_t hysteresis_milli_c{1000};
    bool maintenance_inhibit{};
    SensorSample exhaust{};
    SensorSample hot_air{};
    SensorSample room{};
};

enum Fault : uint32_t {
    FAULT_NONE = 0,
    FAULT_CONFIG = 1u << 0,
    FAULT_SAFETY_OPEN = 1u << 1,
    FAULT_EXHAUST_SENSOR = 1u << 2,
    FAULT_AIR_SENSOR = 1u << 3,
    FAULT_ROOM_SENSOR = 1u << 4,
    FAULT_EXHAUST_OVERTEMP = 1u << 5,
    FAULT_AIR_OVERTEMP = 1u << 6,
    FAULT_IGNITION_NOT_PROVEN = 1u << 7,
    FAULT_FLAME_LOST = 1u << 8,
    FAULT_GLOW_TIMEOUT = 1u << 9,
    FAULT_BROWNOUT_RESET = 1u << 10,
    FAULT_INTERNAL_INVARIANT = 1u << 11,
    FAULT_SELF_TEST_TIMEOUT = 1u << 12,
    FAULT_LOCAL_UI = 1u << 13,
    FAULT_OUTPUT = 1u << 14,
};

struct Outputs {
    bool glow_command{};
    bool fuel_command{};
    bool fan_request{};
    bool fuel_permit{};
    bool arm_ready{};
    bool sw_trip{};
    bool watchdog_heartbeat{};
    // Requested speed: 0=static LOW, 1..800=PWM, 1000=static HIGH; 801..999 forbidden.
    uint16_t speed_permille{};
};

enum class StartBlock : uint8_t { NONE, PROFILE, FAULT, RECOVERY, ACTIVE, SERVICE, STOP, SAFETY, RESTART_DELAY, HOT, ROOM };

struct Status {
    StartBlock start_block{StartBlock::RECOVERY};
    State state{State::BOOT_SELF_TEST};
    uint32_t faults{FAULT_NONE};
    uint32_t state_age_ms{};
    uint32_t stage_remaining_ms{};
    bool stage_timed{};
    int32_t ignition_baseline_milli_c{};
    bool commissioning_ready{};
    bool recovery_complete{};
    Outputs outputs{};
};

class Machine {
public:
    explicit Machine(const Config& config, uint32_t startup_faults = FAULT_NONE);
    Outputs tick(const Inputs& inputs);
    Status status(uint32_t now_ms) const;

private:
    Config config_;
    State state_{State::BOOT_SELF_TEST};
    uint32_t faults_{FAULT_NONE};
    uint32_t state_entered_ms_{};
    uint32_t glow_started_ms_{};
    uint32_t purge_started_ms_{};
    uint32_t restart_started_ms_{};
    uint32_t heartbeat_changed_ms_{};
    int32_t ignition_baseline_milli_c_{};
    bool initialized_{};
    bool glow_timer_started_{};
    bool purge_required_{};
    bool restart_inhibit_{};
    bool previous_reset_request_{};
    bool reset_pending_{};
    bool reset_qualified_{};
    bool recovery_complete_{};
    bool start_release_seen_{};
    bool previous_heat_enable_{};
    bool heartbeat_level_{};
    StartBlock start_block_{StartBlock::RECOVERY};
    Outputs outputs_{};

    static uint32_t elapsed(uint32_t now, uint32_t since);
    bool sample_ok(const SensorSample& sample, uint32_t now) const;
    uint32_t current_input_faults(const Inputs& inputs) const;
    bool cold(const Inputs& inputs) const;
    bool flame_rise_proven(const Inputs& inputs) const;
    bool state_may_need_purge(State state) const;
    void transition(State state, uint32_t now);
    void trip(uint32_t fault, uint32_t now);
    void run_state(const Inputs& inputs);
    Outputs make_outputs(const Inputs& inputs);
    void update_heartbeat(const Inputs& inputs, bool healthy);
};

const char* state_name(State state);

}  // namespace sirokko
