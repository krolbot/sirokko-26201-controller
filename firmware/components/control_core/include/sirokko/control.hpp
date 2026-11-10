#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include "sirokko/regulator.hpp"
#include "sirokko/calibration.hpp"

namespace sirokko {

constexpr uint16_t kPwmMaximum = 4014;
constexpr uint32_t kControlPeriodMs = 20;
constexpr uint64_t kCommandFreshnessMs = 5000;
constexpr uint64_t kSensorStaleMs = 600;
constexpr uint64_t kRoomSensorStaleMs = 2000;
constexpr uint64_t kMaximumTimerMs = 7ULL * 24 * 60 * 60 * 1000;

enum class Mode : uint8_t { TEMPERATURE, MANUAL };

enum class State : uint8_t {
    RECOVERY_PURGE,
    IDLE,
    PREPURGE,
    PREHEAT,
    IGNITION,
    VERIFY,
    RUN,
    STOP_PURGE,
    FAULT_PURGE,
    FAULT,
    MAINTENANCE,
};

enum class Block : uint8_t {
    NONE,
    ENVELOPE_MISSING,
    ENVELOPE_INVALID,
    ENVELOPE_HARDWARE_MISMATCH,
    RECOVERY_PURGE,
    SENSOR_INVALID,
    SENSOR_STALE,
    SENSOR_SKEW,
    INITIAL_THERMAL_STATE,
    FAULT_LATCHED,
    BUSY,
    MAINTENANCE,
    STORAGE_UNCONFIRMED,
    MODEL_UNAVAILABLE,
    STALE_COMMAND,
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
    FAULT_COMMISSIONING = 1U << 7,
    FAULT_STORAGE = 1U << 8,
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

struct SafetyEnvelope {
    bool provisioned{};
    uint64_t recovery_purge_ms{};
    uint64_t prepurge_ms{};
    uint64_t preheat_ms{};
    uint64_t ignition_ms{};
    uint64_t verify_ms{};
    uint64_t postpurge_ms{};
    int32_t maximum_purge_exit_exhaust_milli_c{};
    int32_t maximum_purge_exit_air_milli_c{};
    int32_t maximum_exhaust_milli_c{};
    int32_t maximum_air_milli_c{};
    int32_t minimum_ignition_rise_milli_c{};
    int32_t minimum_run_exhaust_milli_c{};
    uint16_t purge_speed_code{};
    uint16_t reference_speed_code{};
    // Explicit trial envelope; zero forbids automatic exploration, not an invented motor minimum.
    uint16_t exploration_floor_code{};
    int32_t minimum_ignition_air_rise_milli_c{};
};

struct Settings {
    int32_t room_setpoint_milli_c{15000};
    int32_t room_hysteresis_milli_c{1000};
    uint64_t duration_ms{};  // 0 = no automatic shutdown; never persisted as an active deadline.
    Mode mode{Mode::TEMPERATURE};
    uint8_t manual_percent{100};
    double slew_up_per_s{0.005};
    double slew_down_per_s{0.005};
};

enum class ModelStorageAbort : uint8_t { ABORTED, NO_CANDIDATE, COMMIT_IN_PROGRESS };

// Typed public state.  The calibration implementation's strings remain internal
// diagnostics; transports must serialize these enums deliberately.
enum class CommissioningPhase : uint8_t {
    IDLE, STARTING, WAIT_REFERENCE_COMMAND, SETTLE_REFERENCE,
    WAIT_DESCENT_COMMAND, SETTLE_POINT, WAIT_RETURN_COMMAND,
    SETTLE_RETURN_REFERENCE, WAIT_REPEAT_COMMAND, SETTLE_REPEAT_POINT,
    WAIT_POST_REPEAT_REFERENCE_COMMAND, SETTLE_POST_REPEAT_REFERENCE,
    COMPLETE_PENDING_STOP, FAILED, CANCELLED, PERSIST_STAGING,
    PERSIST_COMMITTING, SAVED, PERSIST_FAILED,
};

enum class CommissioningReason : uint8_t {
    NONE, COMPLETE, INVALID_CONFIG, ALREADY_ACTIVE, BACKDATED_TIME,
    OVERALL_TIMEOUT, RUN_CONTEXT_LOST, COMMAND_OUT_OF_RANGE,
    COMMAND_SLEW_VIOLATION, COMMAND_NOT_REACHED, SENSOR_INVALID,
    STALE_SAMPLE, SAMPLE_TIMESTAMP_REGRESSION, SAMPLE_SKEW,
    REPEATED_SAMPLE, SAMPLE_GAP, REFERENCE_TIMEOUT, POINT_TIMEOUT,
    REFERENCE_NOT_REPRODUCIBLE, NO_COMMAND_RESPONSE,
    POINT_NOT_REPRODUCIBLE, DYNAMIC_NOT_REPRODUCIBLE, INSUFFICIENT_MARGIN,
    OPERATOR_NEGATIVE, EXTERNAL_CANCEL, STORAGE_UNCONFIRMED,
};

// Measurements copied from accepted calibration observations, never inferred
// from a configured envelope or a selected PWM range.
struct CommissioningMeasurements {
    uint64_t elapsed_ms{};
    uint64_t reference_settled_after_ms{};
    uint64_t lowest_tested_settled_after_ms{};
    int32_t reference_exhaust_mean_milli_c{};
    int32_t reference_air_mean_milli_c{};
    int32_t lowest_tested_exhaust_mean_milli_c{};
    int32_t lowest_tested_air_mean_milli_c{};
    uint32_t reference_exhaust_noise_milli_c{};
    uint32_t reference_air_noise_milli_c{};
    uint32_t reference_exhaust_drift_milli_c{};
    uint32_t reference_air_drift_milli_c{};
    double observed_slew_up_codes_per_s{};
    double observed_slew_down_codes_per_s{};
    uint8_t tested_point_count{};
};

struct ModelProvenance {
    uint64_t safety_envelope_fingerprint{};
    uint64_t commissioning_started_ms{};
    uint64_t commissioning_completed_ms{};
    uint32_t revision{};
};

struct LearnedOperatingModel {
    bool valid{};
    uint16_t minimum_speed_code{};
    uint16_t maximum_speed_code{};
    double slew_up_per_s{0.005};
    double slew_down_per_s{0.005};
    ModelProvenance provenance{};
    CommissioningMeasurements measurements{};
    uint64_t fingerprint{};
};

// Manual correction may only narrow the observed operating interval or slow
// its motion.  It cannot manufacture wider/stronger learned evidence.
struct LearnedModelRevision {
    uint32_t expected_revision{};
    uint16_t minimum_speed_code{};
    uint16_t maximum_speed_code{};
    double slew_up_per_s{};
    double slew_down_per_s{};
};

struct CommissioningSnapshot {
    CommissioningPhase phase{CommissioningPhase::IDLE};
    CommissioningReason reason{CommissioningReason::NONE};
    Block block{Block::ENVELOPE_MISSING};
    bool active{};
    uint16_t target_speed_code{};
    uint8_t accepted_point_count{};
    uint64_t elapsed_ms{};
    uint64_t source_envelope_fingerprint{};
    uint64_t installed_model_fingerprint{};
    uint32_t installed_model_revision{};
};

struct ControlEvent {
    uint64_t at_ms{};
    const char* kind{"NONE"};
    State state{State::IDLE};
    uint16_t speed_code{};
    int32_t exhaust_milli_c{};
    int32_t air_milli_c{};
    uint32_t faults{};
};

bool valid_model_revision(const LearnedOperatingModel& model, const LearnedModelRevision& revision);
bool valid_settings(const Settings& settings);
bool valid_safety_envelope(const SafetyEnvelope& profile);
uint64_t envelope_fingerprint(const SafetyEnvelope& profile);
uint64_t learned_model_fingerprint(const LearnedOperatingModel& model);
// Calibration-specific admission after the common start guard has passed.
Block commissioning_configuration_block(const SafetyEnvelope& profile, const Settings& settings);

struct Outputs {
    bool fan_request{};
    uint16_t speed_code{};
    bool glow{};
    bool fuel{};
};

struct TickInput {
    uint64_t now_ms{};
    Sensors sensors;
    uint32_t external_faults{};
    bool stop{};
    bool start{};
    bool reset_fault{};
    bool arm_delayed{};
    bool cancel_delayed{};
    uint64_t delay_ms{};
    bool enter_maintenance{};
    bool exit_maintenance{};
    bool start_commissioning{};
    bool set_shutdown_timer{};
    uint64_t shutdown_after_ms{};  // 0 cancels an active timer.
    bool operator_combustion_problem{};
};

struct Snapshot {
    State state{State::RECOVERY_PURGE};
    Block start_block{Block::RECOVERY_PURGE};
    Outputs outputs;
    uint32_t faults{};
    uint64_t state_version{1};
    uint64_t boot_id{};
    uint64_t run_id{};
    uint64_t arm_id{};
    bool duration_active{};
    uint64_t duration_remaining_ms{};
    bool delayed_armed{};
    uint64_t delayed_remaining_ms{};
    bool purge_active{};
    uint64_t purge_id{};
    uint64_t purge_minimum_remaining_ms{};
    bool purge_sensors_fresh{};
    bool purge_temperature_ready{};
    double command_percent{};
    double working_span_fraction{};
    bool room_excess{};
    bool envelope_provisioned{};
    LearnedOperatingModel learned_model{};
    CommissioningSnapshot commissioning{};
};

enum class Action : uint8_t { START, ARM, RESET_FAULT, SETTINGS, OTA, CALIBRATE, TIMER, CANCEL_ARM, MODEL_REVISION };

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
    Grant issue(Action action, uint64_t params_hash, uint64_t now_ms,
                uint64_t state_version, uint64_t random_token);
    bool consume(const Grant& candidate, uint64_t now_ms, uint64_t state_version);
    void invalidate();
private:
    Grant current_{};
};

class Controller {
public:
    Controller(SafetyEnvelope profile, Settings settings, uint64_t boot_id);
    Snapshot tick(const TickInput& input);
    Snapshot snapshot(uint64_t now_ms) const;
    bool update_settings(const Settings& settings);
    bool install_learned_model(const LearnedOperatingModel& range);
    bool revise_learned_model(const LearnedModelRevision& revision);
    bool pending_learned_model(LearnedOperatingModel* range) const;
    // A candidate becomes irrevocable to STOP/fault cancellation only after seal succeeds.
    bool begin_model_storage(uint64_t tx_id);
    bool seal_model_storage(uint64_t tx_id);
    bool complete_model_storage(uint64_t tx_id, bool verified);
    bool model_storage_ready(uint64_t tx_id) const;
    // Abort never activates a candidate and cannot revoke an already sealed commit.
    ModelStorageAbort abort_model_storage();
    LearnedOperatingModel learned_model() const { return model_; }
    SafetyEnvelope safety_envelope() const { return envelope_; }
    Settings settings() const { return settings_; }
    const std::array<ControlEvent,16>& events() const { return events_; }
    size_t event_count() const { return event_count_; }
private:
    bool sensors_fresh(const Sensors& sensors, uint64_t now_ms) const;
    bool thermal_sensors_fresh(const Sensors& sensors, uint64_t now_ms) const;
    void record(const char* kind, uint64_t now_ms);
    void regulate(uint64_t now_ms, const Sensors& sensors);
    void start_commissioning_run(uint64_t now_ms);
    void run_commissioning(uint64_t now_ms, const Sensors& sensors);
    void cancel_commissioning(calibration::Reason reason, uint64_t now_ms);
    uint32_t sensor_faults(const Sensors& sensors, uint64_t now_ms) const;
    Block start_guard(const Sensors& sensors, uint64_t now_ms) const;
    void transition(State state, uint64_t now_ms);
    void begin_purge(State state, uint64_t now_ms);
    bool purge_ready(const Sensors& sensors, uint64_t now_ms) const;
    void begin_start(uint64_t now_ms, const Sensors& sensors);
    void begin_stop(uint64_t now_ms, bool preserve_calibration_candidate = false);
    void latch_fault(uint32_t faults, uint64_t now_ms);
    Outputs outputs() const;

    SafetyEnvelope envelope_;
    Settings settings_;
    Sensors last_sensors_;
    State state_{State::RECOVERY_PURGE};
    uint32_t faults_{};
    uint64_t state_started_ms_{};
    uint64_t state_version_{1};
    uint64_t boot_id_{};
    uint64_t next_id_{1};
    uint64_t run_id_{};
    uint64_t arm_id_{};
    uint64_t purge_id_{};
    uint64_t purge_started_ms_{};
    uint64_t purge_minimum_ms_{};
    uint64_t duration_deadline_ms_{};
    uint64_t delayed_due_ms_{};
    int32_t ignition_baseline_milli_c_{};
    bool duration_active_{};
    bool delayed_armed_{};
    uint32_t previous_external_faults_{};
    LearnedOperatingModel model_;
    Regulator regulator_;
    calibration::Calibrator calibrator_;
    calibration::Config commissioning_config_;
    LearnedOperatingModel model_candidate_;
    bool commissioning_requested_{};
    bool model_saved_{};
    uint64_t model_storage_id_{};
    bool model_storage_sealed_{};
    // A sealed write failure is indeterminate until a later boot/load verifies NVS.
    bool model_storage_unconfirmed_{};
    uint64_t commissioning_started_ms_{};
    uint64_t commissioning_control_ms_{};
    calibration::Phase commissioning_logged_phase_{calibration::Phase::IDLE};
    double run_speed_code_{};
    int32_t ignition_air_baseline_milli_c_{};
    uint64_t last_tick_ms_{};
    bool have_tick_{};
    std::array<ControlEvent,16> events_{};
    size_t event_count_{};
};

int32_t decode_max31856_signed19(uint8_t msb, uint8_t mid, uint8_t lsb);
int32_t max31856_milli_c(uint8_t msb, uint8_t mid, uint8_t lsb);
uint8_t onewire_crc8(const uint8_t* data, size_t size);
bool decode_ds18b20_scratchpad(const uint8_t frame[9], unsigned requested_resolution_bits,
                               bool conversion_completed, int32_t* milli_c);
bool max31856_sample_within_deadline(uint64_t started_ms, uint64_t ready_observed_ms,
                                     uint64_t processing_completed_ms);

enum class OutputLine : uint8_t { FAN_REQUEST, GLOW, FUEL };
using DigitalWriter = bool (*)(void* context, OutputLine line, bool enabled);
using PwmWriter = bool (*)(void* context, uint16_t speed_code);

class FailClosedOutputDriver {
public:
    bool apply(const Outputs& requested, void* context, DigitalWriter digital_write, PwmWriter pwm_write);
    // Last values confirmed by successful writer calls; a failed PWM-off request is not represented as zero.
    Outputs current() const { return current_; }
private:
    Outputs current_{};
};
const char* state_name(State state);
const char* block_name(Block block);

}  // namespace sirokko
