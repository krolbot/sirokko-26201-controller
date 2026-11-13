#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include "sirokko/regulator.hpp"
#include "sirokko/combustion.hpp"

namespace sirokko {

constexpr uint16_t kPwmMaximum = 4014;
// Sirokko 241-265 RU manual, printed pp. 7-8 and 10; type 262 always requires preheat.
constexpr uint64_t kPreheatMinimumMs = 30000;
constexpr uint64_t kPreheatMaximumMs = 60000;
constexpr uint64_t kFiringStartupMaximumMs = 90000;
// Glow plug overlap after motor and fuel start.
constexpr uint64_t kIgnitionGlowMs = 10000;
// Software debounce across distinct fresh thermocouple samples, not a flame-detector rating.
constexpr uint64_t kTemperatureConfirmationMs = 1000;
constexpr uint64_t kPostpurgeMinimumMs = 150000;
// MAX31856 datasheet, Table 1: K-type measurement range starts at -200 C.
// Sensor plausibility bound, not a qualified heater operating temperature.
constexpr int32_t kThermocoupleMinimumMilliC = -200000;
constexpr int32_t kProcessMeasurementCeilingMilliC = 600000;
// Product settings policy; these are not heater specifications.
constexpr int32_t kRoomSetpointMinimumMilliC = 15000;
constexpr int32_t kRoomSetpointMaximumMilliC = 30000;
constexpr int32_t kRoomSetpointStepMilliC = 1000;
constexpr uint8_t kManualStepPercent = 5;
constexpr double kMinimumSlewPerS = 0.0001;
constexpr double kMaximumSlewPerS = 0.005;
constexpr uint32_t kControlPeriodMs = 20;
constexpr uint64_t kCommandFreshnessMs = 5000;
constexpr uint64_t kSensorStaleMs = 600;
constexpr uint64_t kRoomSensorStaleMs = 2000;
constexpr uint64_t kTrialArmMs = 30000;
constexpr uint64_t kTrialMeasurementMs = 90000;
constexpr uint64_t kLocalButtonFreshMs = 100;
constexpr uint64_t kMaximumTimerMs = 7ULL * 24 * 60 * 60 * 1000;

enum class Mode : uint8_t { TEMPERATURE, MANUAL };

enum class State : uint8_t {
    UNCONFIGURED,
    RECOVERY_PURGE,
    IDLE,
    PREHEAT,
    IGNITION,
    RUN,
    STOP_PURGE,
    FAULT_PURGE,
    FAULT,
    MAINTENANCE,
    TRIAL_READY,
};

enum class Block : uint8_t {
    NONE,
    ENVELOPE_MISSING,
    ENVELOPE_INVALID,
    RECOVERY_PURGE,
    SENSOR_INVALID,
    SENSOR_STALE,
    INITIAL_THERMAL_STATE,
    FAULT_LATCHED,
    BUSY,
    MAINTENANCE,
    STALE_COMMAND,
    LOCAL_CONTROLS_UNAVAILABLE,
    INITIALIZING,
    STORAGE_UNAVAILABLE,
    BUTTON_HELD,
};

enum Fault : uint32_t {
    FAULT_NONE = 0,
    FAULT_EXHAUST = 1U << 0,
    FAULT_AIR = 1U << 1,
    FAULT_ROOM = 1U << 2,
    FAULT_OVER_TEMPERATURE = 1U << 3,
    FAULT_IGNITION_TIMEOUT = 1U << 4,
    FAULT_FLAME_LOST = 1U << 5,
    FAULT_CONTROL_WATCHDOG = 1U << 6,
    FAULT_STORAGE = 1U << 8,
    FAULT_LOCAL_CONTROLS = 1U << 9,
    FAULT_COMBUSTION_DROP = 1U << 10,
};

struct SensorSample {
    int32_t milli_c{};
    uint64_t sample_ms{};
    uint8_t status{};
    bool valid{};
};

struct Sensors {
    SensorSample exhaust;
    SensorSample air;
    SensorSample room;
};

bool thermocouple_valid(const SensorSample &sample);
bool room_temperature_valid(const SensorSample &sample);
bool thermocouple_fresh(const SensorSample &sample, uint64_t now_ms);
bool room_temperature_fresh(const SensorSample &sample, uint64_t now_ms);

struct SafetyEnvelope {
    bool provisioned{};
    uint64_t preheat_ms{kPreheatMinimumMs};
    uint64_t ignition_timeout_ms{kFiringStartupMaximumMs};
    uint64_t postpurge_ms{kPostpurgeMinimumMs};
    int32_t cooling_exhaust_milli_c{40000};
    int32_t maximum_exhaust_milli_c{};
    int32_t minimum_run_exhaust_milli_c{};
    // Initial command for the installed two-wire drive on a regulated 24 V supply.
    // This is the controller ceiling, not measured RPM or a qualified lower limit.
    uint16_t purge_speed_code{kPwmMaximum};
    uint16_t maximum_speed_code{kPwmMaximum};
    // Configured lower command for continuous operation; zero means unset.
    uint16_t minimum_speed_code{};
};

struct Settings {
    uint64_t duration_ms{}; // 0 = no automatic shutdown; never persisted as an active deadline.
    int32_t room_setpoint_milli_c{kRoomSetpointMinimumMilliC};
    Mode mode{Mode::TEMPERATURE};
    uint8_t manual_percent{100};
    double slew_up_per_s{0.005};
    double slew_down_per_s{0.005};
};

struct ControlEvent {
    uint64_t at_ms{};
    const char *kind{"NONE"};
    State state{State::IDLE};
    uint16_t speed_code{};
    int32_t exhaust_milli_c{};
    int32_t air_milli_c{};
    uint32_t faults{};
};

bool valid_settings(const Settings &settings);
uint8_t minimum_manual_percent(const SafetyEnvelope &profile);
bool valid_safety_envelope(const SafetyEnvelope &profile);
uint64_t envelope_fingerprint(const SafetyEnvelope &profile);
struct Outputs {
    bool fan_request{};
    uint16_t speed_code{};
    bool glow{};
    bool fuel{};
};

bool valid_profile_draft(const SafetyEnvelope &profile);
bool cooling_complete(const SafetyEnvelope &profile, const Sensors &sensors, uint64_t now_ms);

struct TickInput {
    uint64_t now_ms{};
    Sensors sensors;
    uint32_t external_faults{};
    bool stop{};
    bool start{};
    bool reset_fault{};
    bool enter_maintenance{};
    bool exit_maintenance{};
    bool cycle_record_ready{};
    bool trial_request{};
    bool local_button_held{};
    bool local_button_valid{};
    uint64_t local_button_sample_ms{};
    bool set_shutdown_timer{};
    uint64_t shutdown_after_ms{}; // 0 cancels an active timer.
};

struct Snapshot {
    State state{State::RECOVERY_PURGE};
    Block start_block{Block::RECOVERY_PURGE};
    Block reset_fault_block{Block::BUSY};
    bool trial_active{};
    uint64_t trial_remaining_ms{};
    Block trial_start_block{Block::BUSY};
    Outputs outputs;
    uint32_t faults{};
    uint64_t state_version{1};
    uint64_t boot_id{};
    uint64_t run_id{};
    bool duration_active{};
    uint64_t duration_remaining_ms{};
    bool purge_active{};
    uint64_t purge_id{};
    uint64_t purge_minimum_remaining_ms{};
    bool purge_sensors_fresh{};
    bool purge_temperature_ready{};
    double command_percent{};
    double working_span_fraction{};
    bool room_excess{};
    bool envelope_provisioned{};
    combustion::Snapshot combustion{};
};

enum class Action : uint8_t {
    START,
    RESET_FAULT,
    SETTINGS,
    OTA,
    TIMER,
    PROFILE,
    TRIAL_START,
};

struct Grant {
    uint64_t token{};
    uint64_t state_version{};
    uint64_t params_hash{};
    uint64_t expires_ms{};
    Action action{Action::START};
    bool valid{};
};

class CommandGate {
  public:
    Grant issue(Action action, uint64_t params_hash, uint64_t now_ms, uint64_t state_version,
                uint64_t random_token);
    bool consume(const Grant &candidate, uint64_t now_ms, uint64_t state_version);
    void invalidate();

  private:
    std::array<Grant, 16> grants_{};
};

enum class Startup : uint8_t { CLEAN, RECOVER };

class Controller {
  public:
    Controller(SafetyEnvelope profile, Settings settings, uint64_t boot_id, Startup startup);
    Snapshot tick(const TickInput &input);
    Snapshot snapshot(uint64_t now_ms) const;
    bool update_settings(const Settings &settings);
    // Called by the control owner only after the profile is durably saved.
    bool install_safety_envelope(const SafetyEnvelope &profile, bool recover, uint64_t now_ms);
    SafetyEnvelope safety_envelope() const { return envelope_; }
    Settings settings() const { return settings_; }
    const std::array<ControlEvent, 16> &events() const { return events_; }
    size_t event_count() const { return event_count_; }

  private:
    bool thermal_sensors_fresh(const Sensors &sensors, uint64_t now_ms) const;
    void record(const char *kind, uint64_t now_ms);
    void regulate(uint64_t now_ms, const Sensors &sensors);
    uint32_t sensor_faults(const Sensors &sensors, uint64_t now_ms) const;
    Block reset_fault_guard(const Sensors &sensors, uint64_t now_ms, uint32_t external_faults) const;
    Block start_guard(const Sensors &sensors, uint64_t now_ms) const;
    Block trial_start_guard(const Sensors &sensors, uint64_t now_ms) const;
    bool local_button_fresh(uint64_t now_ms) const;
    bool cooling_ready(const Sensors &sensors, uint64_t now_ms) const;
    uint64_t purge_minimum_ms() const;
    void transition(State state, uint64_t now_ms);
    void begin_purge(State state, uint64_t now_ms);
    bool purge_ready(const Sensors &sensors, uint64_t now_ms) const;
    void begin_start(uint64_t now_ms);
    bool temperature_confirmed(bool condition, uint64_t sample_ms);
    void begin_stop(uint64_t now_ms);
    void latch_fault(uint32_t faults, uint64_t now_ms);
    Outputs outputs() const;

    SafetyEnvelope envelope_;
    Settings settings_;
    Sensors last_sensors_;
    State state_{State::UNCONFIGURED};
    bool trial_active_{};
    bool trial_fault_{};
    bool local_button_held_{};
    bool local_button_valid_{};
    uint64_t local_button_sample_ms_{};
    uint32_t faults_{};
    uint64_t state_started_ms_{};
    uint64_t state_version_{1};
    uint64_t boot_id_{};
    uint64_t next_id_{1};
    uint64_t run_id_{};
    uint64_t purge_id_{};
    uint64_t purge_started_ms_{};
    uint64_t duration_deadline_ms_{};
    uint64_t condition_started_ms_{}, condition_sample_ms_{};
    bool condition_active_{};
    bool duration_active_{};
    uint32_t previous_external_faults_{};
    Regulator regulator_;
    combustion::Monitor combustion_monitor_;
    double run_speed_code_{};
    uint64_t last_tick_ms_{};
    bool have_tick_{};
    std::array<ControlEvent, 16> events_{};
    size_t event_count_{};
};

int32_t decode_max31856_signed19(uint8_t msb, uint8_t mid, uint8_t lsb);
int32_t max31856_milli_c(uint8_t msb, uint8_t mid, uint8_t lsb);
uint8_t onewire_crc8(const uint8_t *data, size_t size);
bool decode_ds18b20_scratchpad(const uint8_t frame[9], unsigned requested_resolution_bits,
                               bool conversion_completed, int32_t *milli_c);
bool max31856_sample_within_deadline(uint64_t started_ms, uint64_t ready_observed_ms,
                                     uint64_t processing_completed_ms);

enum class OutputLine : uint8_t { FAN_REQUEST, GLOW, FUEL };
using DigitalWriter = bool (*)(void *context, OutputLine line, bool enabled);
using PwmWriter = bool (*)(void *context, uint16_t speed_code);

class FailClosedOutputDriver {
  public:
    bool apply(const Outputs &requested, void *context, DigitalWriter digital_write, PwmWriter pwm_write);
    // Last values confirmed by successful writer calls; a failed PWM-off request is not represented as zero.
    Outputs current() const { return current_; }

  private:
    Outputs current_{};
};
const char *state_name(State state);
const char *block_name(Block block);

} // namespace sirokko
