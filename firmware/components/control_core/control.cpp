#include "sirokko/control.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace sirokko {
namespace {

bool active(State state) {
    return state == State::PREPURGE || state == State::PREHEAT || state == State::IGNITION ||
           state == State::VERIFY || state == State::RUN;
}

bool purging(State state) {
    return state == State::RECOVERY_PURGE || state == State::STOP_PURGE || state == State::FAULT_PURGE;
}

bool valid_timer(uint64_t value) {
    return value > 0 && value <= kMaximumTimerMs;
}

uint64_t remaining(uint64_t now, uint64_t deadline) {
    return now >= deadline ? 0 : deadline - now;
}

}  // namespace

Grant CommandGate::issue(Action action, uint64_t params_hash, uint64_t now_ms,
                         uint64_t state_version, uint64_t random_token) {
    if (random_token == 0 || now_ms > std::numeric_limits<uint64_t>::max() - kCommandFreshnessMs) {
        current_ = {};
        return current_;
    }
    current_ = {random_token, state_version, params_hash, now_ms + kCommandFreshnessMs, action, true};
    return current_;
}

bool CommandGate::consume(const Grant& candidate, uint64_t now_ms, uint64_t state_version) {
    if (!current_.valid || now_ms >= current_.expires_ms) {
        current_ = {};
        return false;
    }
    const bool accepted = candidate.valid && candidate.token == current_.token &&
        candidate.action == current_.action && candidate.params_hash == current_.params_hash &&
        candidate.state_version == current_.state_version && state_version == current_.state_version &&
        candidate.expires_ms == current_.expires_ms;
    if (accepted) current_ = {};
    return accepted;
}

void CommandGate::invalidate() { current_ = {}; }

Controller::Controller(ProcessProfile profile, Settings settings, uint64_t boot_id)
    : profile_(profile), settings_(settings), boot_id_(boot_id ? boot_id : 1) {
    if (profile_.purge_speed_code > kPwmMaximum || profile_.run_speed_code > kPwmMaximum ||
        !valid_timer(profile_.recovery_purge_ms) || !valid_timer(profile_.prepurge_ms) ||
        !valid_timer(profile_.preheat_ms) || !valid_timer(profile_.ignition_ms) ||
        !valid_timer(profile_.verify_ms) || !valid_timer(profile_.postpurge_ms) ||
        profile_.maximum_purge_exit_exhaust_milli_c <= 0 ||
        profile_.maximum_purge_exit_air_milli_c <= 0) {
        profile_.qualified = false;
    }
    if (!valid_timer(settings_.duration_ms)) settings_.duration_ms = 3600000;
    purge_id_ = next_id_++;
    purge_minimum_ms_ = profile_.recovery_purge_ms;
}

bool Controller::sensors_fresh(const Sensors& sensors, uint64_t now_ms) const {
    const auto fresh = [now_ms](const SensorSample& sample, uint64_t limit) {
        return sample.valid && sample.status == 0 && sample.sample_ms <= now_ms &&
               now_ms - sample.sample_ms <= limit;
    };
    return fresh(sensors.exhaust, kSensorStaleMs) && fresh(sensors.air, kSensorStaleMs) &&
           fresh(sensors.room, kRoomSensorStaleMs);
}

uint32_t Controller::sensor_faults(const Sensors& sensors, uint64_t now_ms) const {
    const auto bad = [now_ms](const SensorSample& sample, uint64_t limit) {
        return !sample.valid || sample.status != 0 || sample.sample_ms > now_ms ||
               now_ms - sample.sample_ms > limit;
    };
    uint32_t result = FAULT_NONE;
    if (bad(sensors.exhaust, kSensorStaleMs)) result |= FAULT_EXHAUST;
    if (bad(sensors.air, kSensorStaleMs)) result |= FAULT_AIR;
    if (bad(sensors.room, kRoomSensorStaleMs)) result |= FAULT_ROOM;
    if (profile_.qualified) {
        if (sensors.exhaust.valid && sensors.exhaust.milli_c >= profile_.maximum_exhaust_milli_c)
            result |= FAULT_OVER_TEMPERATURE;
        if (sensors.air.valid && sensors.air.milli_c >= profile_.maximum_air_milli_c)
            result |= FAULT_OVER_TEMPERATURE;
    }
    return result;
}

Block Controller::start_guard(const Sensors& sensors, uint64_t now_ms) const {
    if (!profile_.qualified) return Block::PROFILE_NOT_QUALIFIED;
    if (faults_) return Block::FAULT_LATCHED;
    if (state_ != State::IDLE) return state_ == State::MAINTENANCE ? Block::MAINTENANCE : Block::BUSY;
    if (!sensors_fresh(sensors, now_ms)) return Block::SENSOR_INVALID;
    if (sensors.exhaust.milli_c < 0 || sensors.exhaust.milli_c >= profile_.maximum_exhaust_milli_c ||
        sensors.air.milli_c < 0 || sensors.air.milli_c >= profile_.maximum_air_milli_c)
        return Block::TEMPERATURE_OUT_OF_RANGE;
    return Block::NONE;
}

void Controller::transition(State state, uint64_t now_ms) {
    if (state_ == state) return;
    state_ = state;
    state_started_ms_ = now_ms;
    ++state_version_;
    if (state_version_ == 0) state_version_ = 1;
}

void Controller::begin_purge(State state, uint64_t now_ms) {
    if (!purging(state_)) {
        purge_started_ms_ = now_ms;
        purge_minimum_ms_ = state == State::RECOVERY_PURGE
            ? profile_.recovery_purge_ms : profile_.postpurge_ms;
        purge_id_ = next_id_++;
        if (next_id_ == 0) next_id_ = 1;
    }
    transition(state, now_ms);
}

bool Controller::purge_ready(const Sensors& sensors, uint64_t now_ms) const {
    return profile_.qualified && now_ms >= purge_started_ms_ &&
           now_ms - purge_started_ms_ >= purge_minimum_ms_ && sensors_fresh(sensors, now_ms) &&
           sensors.exhaust.milli_c <= profile_.maximum_purge_exit_exhaust_milli_c &&
           sensors.air.milli_c <= profile_.maximum_purge_exit_air_milli_c;
}

void Controller::begin_start(uint64_t now_ms, const Sensors& sensors) {
    run_id_ = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    ignition_baseline_milli_c_ = sensors.exhaust.milli_c;
    duration_active_ = true;
    duration_deadline_ms_ = now_ms + settings_.duration_ms;
    delayed_armed_ = false;
    arm_id_ = 0;
    transition(State::PREPURGE, now_ms);
}

void Controller::begin_stop(uint64_t now_ms) {
    duration_active_ = false;
    duration_deadline_ms_ = 0;
    run_id_ = 0;
    delayed_armed_ = false;
    delayed_due_ms_ = 0;
    arm_id_ = 0;
    if (active(state_)) begin_purge(State::STOP_PURGE, now_ms);
}

void Controller::latch_fault(uint32_t faults, uint64_t now_ms) {
    faults_ |= faults;
    duration_active_ = false;
    delayed_armed_ = false;
    duration_deadline_ms_ = delayed_due_ms_ = 0;
    run_id_ = arm_id_ = 0;
    begin_purge(State::FAULT_PURGE, now_ms);
}

Outputs Controller::outputs() const {
    Outputs result{};
    const uint16_t purge = profile_.purge_speed_code ? profile_.purge_speed_code : kPwmMaximum;
    switch (state_) {
        case State::RECOVERY_PURGE:
        case State::PREPURGE:
        case State::STOP_PURGE:
        case State::FAULT_PURGE:
            result.fan_request = true;
            result.speed_code = purge;
            break;
        case State::PREHEAT:
            result = {true, purge, true, false};
            break;
        case State::IGNITION:
            result = {true, profile_.run_speed_code, true, true};
            break;
        case State::VERIFY:
        case State::RUN:
            result = {true, profile_.run_speed_code, false, true};
            break;
        default:
            break;
    }
    result.speed_code = std::min<uint16_t>(result.speed_code, kPwmMaximum);
    return result;
}

Snapshot Controller::tick(const TickInput& input) {
    const uint64_t now = input.now_ms;
    last_sensors_ = input.sensors;

    const uint32_t current_sensor_faults = sensor_faults(input.sensors, now);
    const uint32_t new_external_faults = input.external_faults & ~previous_external_faults_;
    previous_external_faults_ = input.external_faults;
    uint32_t new_faults = new_external_faults;
    if (active(state_)) new_faults |= current_sensor_faults;
    if (new_faults) latch_fault(new_faults, now);

    // STOP is an independent priority barrier. It cancels queued/armed starts and never writes GPIO directly.
    if (input.stop) begin_stop(now);
    const bool stop_barrier = input.stop;

    if (input.enter_maintenance) {
        delayed_armed_ = false;
        arm_id_ = 0;
        if (state_ == State::IDLE || state_ == State::FAULT) transition(State::MAINTENANCE, now);
    } else if (input.exit_maintenance && state_ == State::MAINTENANCE) {
        transition(faults_ ? State::FAULT : State::IDLE, now);
    }

    if (input.cancel_delayed) {
        delayed_armed_ = false;
        delayed_due_ms_ = 0;
        arm_id_ = 0;
    }

    bool reset_barrier = false;
    if (input.reset_fault && state_ == State::FAULT && current_sensor_faults == FAULT_NONE &&
        input.external_faults == FAULT_NONE) {
        faults_ = FAULT_NONE;
        delayed_armed_ = false;
        arm_id_ = 0;
        transition(State::IDLE, now);
        reset_barrier = true;  // RESET_FAULT never consumes a START/ARM from the same control pass.
    }

    if (duration_active_ && now >= duration_deadline_ms_) begin_stop(now);

    if (!stop_barrier && !reset_barrier && input.arm_delayed && state_ == State::IDLE && valid_timer(input.delay_ms) &&
        start_guard(input.sensors, now) == Block::NONE) {
        delayed_armed_ = true;
        delayed_due_ms_ = now + input.delay_ms;
        arm_id_ = next_id_++;
        if (next_id_ == 0) next_id_ = 1;
    }

    bool delayed_start = false;
    if (delayed_armed_ && now >= delayed_due_ms_) {
        delayed_armed_ = false;
        delayed_due_ms_ = 0;
        delayed_start = true;
    }

    if (!stop_barrier && !reset_barrier && (input.start || delayed_start) &&
        start_guard(input.sensors, now) == Block::NONE) {
        begin_start(now, input.sensors);
    }

    const uint64_t elapsed = now >= state_started_ms_ ? now - state_started_ms_ : 0;
    switch (state_) {
        case State::RECOVERY_PURGE:
            if (purge_ready(input.sensors, now)) {
                transition(State::IDLE, now);
                purge_id_ = 0;
            }
            break;
        case State::PREPURGE:
            if (elapsed >= profile_.prepurge_ms) transition(State::PREHEAT, now);
            break;
        case State::PREHEAT:
            if (elapsed >= profile_.preheat_ms) transition(State::IGNITION, now);
            break;
        case State::IGNITION:
            if (elapsed >= profile_.ignition_ms) {
                if (input.sensors.exhaust.milli_c - ignition_baseline_milli_c_ >= profile_.minimum_ignition_rise_milli_c)
                    transition(State::VERIFY, now);
                else
                    latch_fault(FAULT_IGNITION_TIMEOUT, now);
            }
            break;
        case State::VERIFY:
            if (elapsed >= profile_.verify_ms) {
                if (input.sensors.exhaust.milli_c >= profile_.minimum_run_exhaust_milli_c)
                    transition(State::RUN, now);
                else
                    latch_fault(FAULT_IGNITION_TIMEOUT, now);
            }
            break;
        case State::RUN:
            if (input.sensors.exhaust.milli_c < profile_.minimum_run_exhaust_milli_c)
                latch_fault(FAULT_FLAME_LOST, now);
            else if (input.sensors.room.milli_c >= settings_.room_setpoint_milli_c)
                begin_stop(now);
            break;
        case State::STOP_PURGE:
            if (purge_ready(input.sensors, now)) {
                transition(State::IDLE, now);
                purge_id_ = 0;
            }
            break;
        case State::FAULT_PURGE:
            if (purge_ready(input.sensors, now)) {
                transition(State::FAULT, now);
                purge_id_ = 0;
            }
            break;
        default:
            break;
    }
    return snapshot(now);
}

Snapshot Controller::snapshot(uint64_t now_ms) const {
    Snapshot result{};
    result.state = state_;
    result.outputs = outputs();
    result.faults = faults_;
    result.state_version = state_version_;
    result.boot_id = boot_id_;
    result.run_id = run_id_;
    result.arm_id = arm_id_;
    result.duration_active = duration_active_;
    result.duration_remaining_ms = duration_active_ ? remaining(now_ms, duration_deadline_ms_) : 0;
    result.delayed_armed = delayed_armed_;
    result.delayed_remaining_ms = delayed_armed_ ? remaining(now_ms, delayed_due_ms_) : 0;
    result.purge_active = purging(state_);
    result.purge_id = result.purge_active ? purge_id_ : 0;
    const uint64_t purge_deadline = purge_started_ms_ > std::numeric_limits<uint64_t>::max() - purge_minimum_ms_
        ? std::numeric_limits<uint64_t>::max() : purge_started_ms_ + purge_minimum_ms_;
    result.purge_minimum_remaining_ms = result.purge_active ? remaining(now_ms, purge_deadline) : 0;
    result.purge_sensors_fresh = result.purge_active && sensors_fresh(last_sensors_, now_ms);
    result.purge_temperature_ready = result.purge_sensors_fresh && profile_.qualified &&
        last_sensors_.exhaust.milli_c <= profile_.maximum_purge_exit_exhaust_milli_c &&
        last_sensors_.air.milli_c <= profile_.maximum_purge_exit_air_milli_c;
    result.profile_qualified = profile_.qualified;
    if (!profile_.qualified) result.start_block = Block::PROFILE_NOT_QUALIFIED;
    else if (faults_) result.start_block = Block::FAULT_LATCHED;
    else if (state_ == State::RECOVERY_PURGE) result.start_block = Block::RECOVERY_REQUIRED;
    else if (state_ == State::MAINTENANCE) result.start_block = Block::MAINTENANCE;
    else if (state_ != State::IDLE) result.start_block = Block::BUSY;
    else result.start_block = Block::NONE;
    return result;
}

void Controller::update_settings(const Settings& settings) {
    if (valid_timer(settings.duration_ms) && settings.room_hysteresis_milli_c > 0) settings_ = settings;
}

int32_t decode_max31856_signed19(uint8_t msb, uint8_t mid, uint8_t lsb) {
    const uint32_t raw24 = (static_cast<uint32_t>(msb) << 16) |
                           (static_cast<uint32_t>(mid) << 8) | lsb;
    int32_t raw19 = static_cast<int32_t>(raw24 >> 5);
    if (raw19 & 0x40000) raw19 |= ~0x7FFFF;
    return raw19;
}

int32_t max31856_milli_c(uint8_t msb, uint8_t mid, uint8_t lsb) {
    return static_cast<int32_t>((static_cast<int64_t>(decode_max31856_signed19(msb, mid, lsb)) * 1000) / 128);
}

uint8_t onewire_crc8(const uint8_t* data, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t value = data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint8_t mix = (crc ^ value) & 1U;
            crc >>= 1;
            if (mix) crc ^= 0x8CU;
            value >>= 1;
        }
    }
    return crc;
}

bool decode_ds18b20_scratchpad(const uint8_t frame[9], unsigned requested_resolution_bits,
                               bool conversion_completed, int32_t* milli_c) {
    if (!frame || !milli_c || !conversion_completed || requested_resolution_bits < 9 ||
        requested_resolution_bits > 12 || onewire_crc8(frame, 8) != frame[8] ||
        (frame[4] & 0x9FU) != 0x1FU) return false;
    const unsigned resolution = (frame[4] >> 5) & 0x03U;
    if (requested_resolution_bits != resolution + 9) return false;
    constexpr std::array<uint8_t, 4> mask{{0x07, 0x03, 0x01, 0x00}};
    const uint8_t lsb = frame[0] & static_cast<uint8_t>(~mask[resolution]);
    const int16_t raw = static_cast<int16_t>((static_cast<uint16_t>(frame[1]) << 8) | lsb);
    *milli_c = static_cast<int32_t>((static_cast<int64_t>(raw) * 1000) / 16);
    return true;
}

bool max31856_sample_within_deadline(uint64_t started_ms, uint64_t ready_observed_ms,
                                     uint64_t processing_completed_ms) {
    return ready_observed_ms >= started_ms && processing_completed_ms >= ready_observed_ms &&
           ready_observed_ms - started_ms <= 210 && processing_completed_ms - started_ms <= 210;
}

bool FailClosedOutputDriver::apply(const Outputs& requested, void* context,
                                   DigitalWriter digital_write, PwmWriter pwm_write) {
    if (!digital_write || !pwm_write) return false;
    bool ok = requested.speed_code <= kPwmMaximum &&
              ((!requested.glow && !requested.fuel) || (requested.fan_request && requested.speed_code > 0));
    if (!requested.fuel) {
        ok = digital_write(context, OutputLine::FUEL, false) && ok;
        current_.fuel = false;
    }
    if (!requested.glow) {
        ok = digital_write(context, OutputLine::GLOW, false) && ok;
        current_.glow = false;
    }
    ok = digital_write(context, OutputLine::FAN_REQUEST, requested.fan_request) && ok;
    current_.fan_request = requested.fan_request;
    if (ok) ok = pwm_write(context, requested.speed_code);
    if (!ok) {
        digital_write(context, OutputLine::FUEL, false);
        digital_write(context, OutputLine::GLOW, false);
        current_.fuel = current_.glow = false;
        current_.speed_code = 0;
        return false;
    }
    current_.speed_code = requested.speed_code;
    if (requested.glow && !current_.glow) {
        ok = digital_write(context, OutputLine::GLOW, true);
        current_.glow = ok;
    }
    if (ok && requested.fuel && !current_.fuel) {
        ok = digital_write(context, OutputLine::FUEL, true);
        current_.fuel = ok;
    }
    if (!ok) {
        digital_write(context, OutputLine::FUEL, false);
        digital_write(context, OutputLine::GLOW, false);
        current_.fuel = current_.glow = false;
    }
    return ok;
}

const char* state_name(State state) {
    switch (state) {
        case State::RECOVERY_PURGE: return "RECOVERY_PURGE";
        case State::IDLE: return "IDLE";
        case State::PREPURGE: return "PREPURGE";
        case State::PREHEAT: return "PREHEAT";
        case State::IGNITION: return "IGNITION";
        case State::VERIFY: return "VERIFY";
        case State::RUN: return "RUN";
        case State::STOP_PURGE: return "STOP_PURGE";
        case State::FAULT_PURGE: return "FAULT_PURGE";
        case State::FAULT: return "FAULT";
        case State::MAINTENANCE: return "MAINTENANCE";
    }
    return "UNKNOWN";
}

const char* block_name(Block block) {
    switch (block) {
        case Block::NONE: return "NONE";
        case Block::PROFILE_NOT_QUALIFIED: return "PROFILE_NOT_QUALIFIED";
        case Block::RECOVERY_REQUIRED: return "RECOVERY_REQUIRED";
        case Block::SENSOR_INVALID: return "SENSOR_INVALID";
        case Block::TEMPERATURE_OUT_OF_RANGE: return "TEMPERATURE_OUT_OF_RANGE";
        case Block::FAULT_LATCHED: return "FAULT_LATCHED";
        case Block::BUSY: return "BUSY";
        case Block::MAINTENANCE: return "MAINTENANCE";
        case Block::STALE_COMMAND: return "STALE_COMMAND";
    }
    return "UNKNOWN";
}

}  // namespace sirokko
