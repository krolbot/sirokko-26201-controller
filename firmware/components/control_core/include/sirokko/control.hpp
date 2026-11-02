#pragma once

#include <cstddef>
#include <cstdint>

namespace sirokko {

constexpr uint16_t kPwmMaximum = 4014;
constexpr uint32_t kControlPeriodMs = 20;
constexpr uint64_t kCommandFreshnessMs = 5000;
constexpr uint64_t kSensorStaleMs = 600;
constexpr uint64_t kRoomSensorStaleMs = 2000;
constexpr uint64_t kMaximumTimerMs = 7ULL * 24 * 60 * 60 * 1000;

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
    PROFILE_NOT_QUALIFIED,
    RECOVERY_REQUIRED,
    SENSOR_INVALID,
    TEMPERATURE_OUT_OF_RANGE,
    FAULT_LATCHED,
    BUSY,
    MAINTENANCE,
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

struct ProcessProfile {
    bool qualified{};
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
    uint16_t run_speed_code{};
};

struct Settings {
    int32_t room_setpoint_milli_c{20000};
    int32_t room_hysteresis_milli_c{1000};
    uint64_t duration_ms{3600000};
};

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
};

struct Snapshot {
    State state{State::RECOVERY_PURGE};
    Block start_block{Block::RECOVERY_REQUIRED};
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
    bool profile_qualified{};
};

enum class Action : uint8_t { START, ARM, RESET_FAULT, SETTINGS, OTA };

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
    Controller(ProcessProfile profile, Settings settings, uint64_t boot_id);
    Snapshot tick(const TickInput& input);
    Snapshot snapshot(uint64_t now_ms) const;
    void update_settings(const Settings& settings);
private:
    bool sensors_fresh(const Sensors& sensors, uint64_t now_ms) const;
    uint32_t sensor_faults(const Sensors& sensors, uint64_t now_ms) const;
    Block start_guard(const Sensors& sensors, uint64_t now_ms) const;
    void transition(State state, uint64_t now_ms);
    void begin_purge(State state, uint64_t now_ms);
    bool purge_ready(const Sensors& sensors, uint64_t now_ms) const;
    void begin_start(uint64_t now_ms, const Sensors& sensors);
    void begin_stop(uint64_t now_ms);
    void latch_fault(uint32_t faults, uint64_t now_ms);
    Outputs outputs() const;

    ProcessProfile profile_;
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
    Outputs current() const { return current_; }
private:
    Outputs current_{};
};
const char* state_name(State state);
const char* block_name(Block block);

}  // namespace sirokko
