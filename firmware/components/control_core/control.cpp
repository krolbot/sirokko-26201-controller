#include "sirokko/control.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>

namespace sirokko {
namespace {

bool active(State state) {
    return state == State::PREHEAT || state == State::IGNITION || state == State::RUN;
}

Block sensor_block(const Sensors &sensors, uint64_t now, bool room_required) {
    if (!thermocouple_valid(sensors.exhaust) || !thermocouple_valid(sensors.air) ||
        sensors.exhaust.sample_ms > now || sensors.air.sample_ms > now ||
        (room_required && (!room_temperature_valid(sensors.room) || sensors.room.sample_ms > now)))
        return Block::SENSOR_INVALID;
    if (!thermocouple_fresh(sensors.exhaust, now) || !thermocouple_fresh(sensors.air, now) ||
        (room_required && !room_temperature_fresh(sensors.room, now)))
        return Block::SENSOR_STALE;
    return Block::NONE;
}

bool purging(State state) {
    return state == State::RECOVERY_PURGE || state == State::STOP_PURGE || state == State::FAULT_PURGE;
}

uint64_t remaining(uint64_t now, uint64_t deadline) {
    return now >= deadline ? 0 : deadline - now;
}

int32_t trial_cooling_limit(const SafetyEnvelope &profile) {
    const auto configured = profile.cooling_exhaust_milli_c;
    return configured > 0 && configured < kProcessMeasurementCeilingMilliC ? configured : 40000;
}

} // namespace

Grant CommandGate::issue(Action action, uint64_t params_hash, uint64_t now_ms, uint64_t state_version,
                         uint64_t random_token) {
    if (!random_token || now_ms > std::numeric_limits<uint64_t>::max() - kCommandFreshnessMs)
        return {};
    for (auto &grant : grants_)
        if (grant.valid && now_ms >= grant.expires_ms)
            grant = {};
    for (const auto &grant : grants_)
        if (grant.valid && grant.token == random_token)
            return {};
    for (auto &grant : grants_) {
        if (grant.valid)
            continue;
        grant = {random_token, state_version, params_hash, now_ms + kCommandFreshnessMs, action, true};
        return grant;
    }
    return {};
}
bool CommandGate::consume(const Grant &candidate, uint64_t now_ms, uint64_t state_version) {
    if (!candidate.valid)
        return false;
    for (auto &grant : grants_) {
        if (!grant.valid)
            continue;
        if (now_ms >= grant.expires_ms || state_version != grant.state_version) {
            grant = {};
            continue;
        }
        if (candidate.token != grant.token || candidate.action != grant.action ||
            candidate.params_hash != grant.params_hash || candidate.state_version != grant.state_version ||
            candidate.expires_ms != grant.expires_ms)
            continue;
        grant = {};
        return true;
    }
    return false;
}
void CommandGate::invalidate() {
    grants_ = {};
}

Controller::Controller(SafetyEnvelope profile, Settings settings, uint64_t boot_id, Startup startup)
    : envelope_(profile), settings_(settings), boot_id_(boot_id ? boot_id : 1) {
    envelope_.provisioned = envelope_.provisioned && valid_safety_envelope(envelope_);
    if (!valid_settings(settings_))
        settings_ = {};
    if (!envelope_.provisioned && startup == Startup::RECOVER)
        envelope_.postpurge_ms = std::max(envelope_.postpurge_ms, kPostpurgeMinimumMs);
    settings_.manual_percent = std::max(settings_.manual_percent, minimum_manual_percent(envelope_));
    run_speed_code_ = envelope_.maximum_speed_code;
    state_ = startup == Startup::RECOVER ? State::RECOVERY_PURGE
             : envelope_.provisioned     ? State::IDLE
                                         : State::UNCONFIGURED;
    if (state_ == State::RECOVERY_PURGE) {
        purge_id_ = next_id_++;
    }
}

bool Controller::thermal_sensors_fresh(const Sensors &sensors, uint64_t now_ms) const {
    return thermocouple_fresh(sensors.exhaust, now_ms) && thermocouple_fresh(sensors.air, now_ms);
}

uint32_t Controller::sensor_faults(const Sensors &sensors, uint64_t now_ms) const {
    uint32_t result = FAULT_NONE;
    const bool exhaust_ok = thermocouple_fresh(sensors.exhaust, now_ms);
    if (!exhaust_ok)
        result |= FAULT_EXHAUST;
    if (!thermocouple_fresh(sensors.air, now_ms))
        result |= FAULT_AIR;
    if (!trial_active_ && settings_.mode == Mode::TEMPERATURE &&
        !room_temperature_fresh(sensors.room, now_ms))
        result |= FAULT_ROOM;
    if (exhaust_ok && envelope_.maximum_exhaust_milli_c &&
        sensors.exhaust.milli_c >= envelope_.maximum_exhaust_milli_c)
        result |= FAULT_OVER_TEMPERATURE;
    return result;
}

Block Controller::start_guard(const Sensors &sensors, uint64_t now_ms) const {
    if (!envelope_.provisioned)
        return Block::ENVELOPE_MISSING;
    if (faults_)
        return Block::FAULT_LATCHED;
    if (state_ != State::IDLE)
        return state_ == State::MAINTENANCE ? Block::MAINTENANCE : Block::BUSY;
    const auto data_block = sensor_block(sensors, now_ms, settings_.mode == Mode::TEMPERATURE);
    if (data_block != Block::NONE)
        return data_block;
    if (sensors.exhaust.milli_c < kThermocoupleMinimumMilliC ||
        sensors.exhaust.milli_c >= envelope_.maximum_exhaust_milli_c ||
        sensors.air.milli_c < kThermocoupleMinimumMilliC || !cooling_complete(envelope_, sensors, now_ms))
        return Block::INITIAL_THERMAL_STATE;
    if (settings_.duration_ms > std::numeric_limits<uint64_t>::max() - now_ms)
        return Block::STALE_COMMAND;
    return Block::NONE;
}

bool Controller::local_button_fresh(uint64_t now_ms) const {
    return local_button_valid_ && local_button_sample_ms_ <= now_ms &&
           now_ms - local_button_sample_ms_ <= kLocalButtonFreshMs;
}

bool Controller::cooling_ready(const Sensors &sensors, uint64_t now_ms) const {
    if (!trial_active_)
        return cooling_complete(envelope_, sensors, now_ms);
    return thermal_sensors_fresh(sensors, now_ms) && sensors.exhaust.milli_c < trial_cooling_limit(envelope_);
}

uint64_t Controller::purge_minimum_ms() const {
    return !trial_active_ ? envelope_.postpurge_ms : std::max(envelope_.postpurge_ms, kPostpurgeMinimumMs);
}

Block Controller::trial_start_guard(const Sensors &sensors, uint64_t now_ms) const {
    if (faults_)
        return Block::FAULT_LATCHED;
    if (trial_active_ || (state_ != State::IDLE && state_ != State::UNCONFIGURED))
        return Block::BUSY;
    if (!local_button_fresh(now_ms))
        return Block::LOCAL_CONTROLS_UNAVAILABLE;
    if (local_button_held_)
        return Block::BUTTON_HELD;
    const auto data = sensor_block(sensors, now_ms, false);
    if (data != Block::NONE)
        return data;
    if (sensors.exhaust.milli_c >= trial_cooling_limit(envelope_) ||
        (envelope_.maximum_exhaust_milli_c > 0 &&
         sensors.exhaust.milli_c >= envelope_.maximum_exhaust_milli_c))
        return Block::INITIAL_THERMAL_STATE;
    return Block::NONE;
}

void Controller::transition(State state, uint64_t now_ms) {
    if (state_ == state)
        return;
    state_ = state;
    if (state == State::IDLE || state == State::UNCONFIGURED || state == State::FAULT)
        trial_active_ = false;
    state_started_ms_ = now_ms;
    ++state_version_;
    if (state_version_ == 0)
        state_version_ = 1;
}

void Controller::begin_purge(State state, uint64_t now_ms) {
    if (!purging(state_)) {
        purge_started_ms_ = now_ms;
        purge_id_ = next_id_++;
        if (next_id_ == 0)
            next_id_ = 1;
    }
    transition(state, now_ms);
}

bool Controller::purge_ready(const Sensors &sensors, uint64_t now_ms) const {
    return now_ms >= purge_started_ms_ && now_ms - purge_started_ms_ >= purge_minimum_ms() &&
           cooling_ready(sensors, now_ms);
}

bool Controller::temperature_confirmed(bool condition, uint64_t sample_ms) {
    if (!condition) {
        condition_active_ = false;
        return false;
    }
    if (!condition_active_ || sample_ms < condition_sample_ms_ ||
        sample_ms - condition_sample_ms_ > kSensorStaleMs) {
        condition_active_ = true;
        condition_started_ms_ = sample_ms;
    }
    condition_sample_ms_ = sample_ms;
    return sample_ms - condition_started_ms_ >= kTemperatureConfirmationMs;
}

void Controller::begin_start(uint64_t now_ms) {
    trial_fault_ = false;
    combustion_monitor_.reset(envelope_.minimum_run_exhaust_milli_c, envelope_.maximum_speed_code);
    run_id_ = next_id_++;
    if (next_id_ == 0)
        next_id_ = 1;
    condition_active_ = false;
    run_speed_code_ = envelope_.maximum_speed_code;
    duration_active_ = settings_.duration_ms != 0;
    duration_deadline_ms_ = duration_active_ ? now_ms + settings_.duration_ms : 0;
    transition(State::PREHEAT, now_ms);
    record("START", now_ms);
}

void Controller::begin_stop(uint64_t now_ms) {
    if (combustion_monitor_.snapshot().phase != combustion::Phase::LOST)
        combustion_monitor_ = {};
    if (active(state_))
        record("STOP", now_ms);
    duration_active_ = false;
    duration_deadline_ms_ = 0;
    run_id_ = 0;
    if (active(state_))
        begin_purge(State::STOP_PURGE, now_ms);
    else if (state_ == State::TRIAL_READY)
        transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now_ms);
}

void Controller::latch_fault(uint32_t faults, uint64_t now_ms) {
    trial_fault_ = trial_fault_ || trial_active_;
    if (combustion_monitor_.snapshot().phase != combustion::Phase::LOST)
        combustion_monitor_ = {};
    faults_ |= faults;
    record("FAULT", now_ms);
    duration_active_ = false;
    duration_deadline_ms_ = 0;
    run_id_ = 0;
    if (active(state_) || purging(state_) || (faults & FAULT_OVER_TEMPERATURE))
        begin_purge(State::FAULT_PURGE, now_ms);
    else
        transition(State::FAULT, now_ms);
}

Outputs Controller::outputs() const {
    Outputs result{};
    if (trial_active_) {
        if (purging(state_))
            return {true, kPwmMaximum, false, false};
        const bool held = local_button_held_ && local_button_fresh(last_tick_ms_);
        if (state_ == State::PREHEAT)
            return {false, 0, held, false};
        if (state_ == State::IGNITION) {
            const uint64_t elapsed =
                last_tick_ms_ >= state_started_ms_ ? last_tick_ms_ - state_started_ms_ : 0;
            return {true, kPwmMaximum, held && elapsed < kIgnitionGlowMs,
                    held && elapsed < kTrialMeasurementMs};
        }
        return result;
    }
    if (!envelope_.provisioned && !purging(state_))
        return result;
    const uint16_t purge =
        envelope_.provisioned && envelope_.purge_speed_code ? envelope_.purge_speed_code : kPwmMaximum;
    switch (state_) {
    case State::RECOVERY_PURGE:
    case State::STOP_PURGE:
    case State::FAULT_PURGE:
        result.fan_request = true;
        result.speed_code = purge;
        break;
    case State::PREHEAT:
        // The manual preheat button energizes the glow plug before motor/fuel start.
        result = {false, 0, true, false};
        break;
    case State::IGNITION:
        result = {true, envelope_.maximum_speed_code,
                  last_tick_ms_ >= state_started_ms_ && last_tick_ms_ - state_started_ms_ < kIgnitionGlowMs,
                  true};
        break;
    case State::RUN:
        result = {true, static_cast<uint16_t>(std::lround(run_speed_code_)), false, true};
        break;
    default:
        break;
    }
    result.speed_code = std::min<uint16_t>(result.speed_code, kPwmMaximum);
    return result;
}

Block Controller::reset_fault_guard(const Sensors &sensors, uint64_t now_ms, uint32_t external_faults) const {
    if (state_ != State::FAULT)
        return Block::BUSY;
    if (external_faults)
        return Block::FAULT_LATCHED;
    const auto data_block =
        sensor_block(sensors, now_ms, !trial_fault_ && settings_.mode == Mode::TEMPERATURE);
    if (data_block != Block::NONE)
        return data_block;
    return sensor_faults(sensors, now_ms) & FAULT_OVER_TEMPERATURE ? Block::INITIAL_THERMAL_STATE
                                                                   : Block::NONE;
}

Snapshot Controller::tick(const TickInput &input) {
    const uint64_t now = input.now_ms;
    last_sensors_ = input.sensors;
    local_button_held_ = input.local_button_held;
    local_button_valid_ = input.local_button_valid;
    local_button_sample_ms_ = input.local_button_sample_ms;
    if (!envelope_.provisioned && !trial_active_ && !input.trial_request && !purging(state_) &&
        state_ != State::FAULT) {
        last_tick_ms_ = now;
        faults_ = input.external_faults;
        if (input.stop || input.exit_maintenance) {
            transition(State::UNCONFIGURED, now);
            if (input.stop)
                ++state_version_;
        } else if (input.enter_maintenance && !faults_ &&
                   (!envelope_.provisioned || cooling_complete(envelope_, input.sensors, now))) {
            transition(State::MAINTENANCE, now);
        }
        return snapshot(now);
    }
    if (have_tick_ && now < last_tick_ms_) {
        latch_fault(FAULT_CONTROL_WATCHDOG, now);
        last_tick_ms_ = now;
        return snapshot(now);
    }
    have_tick_ = true;
    last_tick_ms_ = now;

    const uint32_t current_sensor_faults = sensor_faults(input.sensors, now);
    const uint32_t new_external_faults = input.external_faults & ~previous_external_faults_;
    previous_external_faults_ = input.external_faults;
    uint32_t new_faults = new_external_faults;
    if (active(state_) || state_ == State::MAINTENANCE || state_ == State::TRIAL_READY)
        new_faults |= current_sensor_faults;
    const bool button_sample_fresh =
        local_button_sample_ms_ <= now && now - local_button_sample_ms_ <= kLocalButtonFreshMs;
    if (trial_active_ &&
        ((active(state_) && (!button_sample_fresh || (!local_button_valid_ && local_button_held_))) ||
         (state_ == State::TRIAL_READY && !button_sample_fresh)))
        new_faults |= FAULT_LOCAL_CONTROLS;
    if (new_faults)
        latch_fault(new_faults, now);

    // STOP is an independent priority barrier. It cancels queued starts and never writes GPIO directly.
    const bool stop_barrier =
        input.stop || (trial_active_ && active(state_) && (!local_button_held_ || !local_button_valid_));
    if (stop_barrier) {
        // Revoke queued commands even when STOP leaves an idle controller in IDLE.
        if (++state_version_ == 0)
            state_version_ = 1;
        begin_stop(now);
        if (state_ == State::MAINTENANCE)
            transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now);
    }

    if (input.enter_maintenance && !stop_barrier && state_ == State::IDLE && faults_ == 0 &&
        thermal_sensors_fresh(input.sensors, now) && cooling_complete(envelope_, input.sensors, now)) {
        transition(State::MAINTENANCE, now);
    } else if (input.exit_maintenance && state_ == State::MAINTENANCE) {
        transition(faults_ ? State::FAULT : State::IDLE, now);
    }

    bool reset_barrier = false;
    if (input.reset_fault && reset_fault_guard(input.sensors, now, input.external_faults) == Block::NONE) {
        faults_ = FAULT_NONE;
        trial_fault_ = false;
        combustion_monitor_ = {};
        transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now);
        reset_barrier = true; // RESET_FAULT never consumes a START from the same control pass.
    }

    // Existing expiry wins over a simultaneous timer extension or cancellation.
    const bool expired = duration_active_ && now >= duration_deadline_ms_;
    if (expired) {
        record("TIMER_EXPIRED", now);
        begin_stop(now);
    }
    if (!trial_active_ && !stop_barrier && !expired && input.set_shutdown_timer && active(state_) &&
        input.shutdown_after_ms <= kMaximumTimerMs &&
        input.shutdown_after_ms <= std::numeric_limits<uint64_t>::max() - now) {
        duration_active_ = input.shutdown_after_ms != 0;
        duration_deadline_ms_ = duration_active_ ? now + input.shutdown_after_ms : 0;
        ++state_version_;
        record(duration_active_ ? "TIMER_ARMED" : "TIMER_CANCELLED", now);
    }

    const Block requested_start_block = start_guard(input.sensors, now);
    if (!input.trial_request && !stop_barrier && !reset_barrier && !expired && input.cycle_record_ready &&
        input.start && requested_start_block == Block::NONE)
        begin_start(now);

    if (!stop_barrier && !reset_barrier && !expired && input.cycle_record_ready && input.trial_request &&
        trial_start_guard(input.sensors, now) == Block::NONE) {
        trial_active_ = true;
        trial_fault_ = false;
        duration_active_ = false;
        duration_deadline_ms_ = 0;
        condition_active_ = false;
        combustion_monitor_ = {};
        transition(State::TRIAL_READY, now);
        record("TRIAL_ARMED", now);
    }

    const uint64_t elapsed = now >= state_started_ms_ ? now - state_started_ms_ : 0;
    switch (state_) {
    case State::RECOVERY_PURGE:
        if (purge_ready(input.sensors, now)) {
            transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now);
            purge_id_ = 0;
        }
        break;
    case State::TRIAL_READY:
        if (elapsed >= kTrialArmMs) {
            record("TRIAL_EXPIRED", now);
            begin_stop(now);
        } else if (local_button_held_ && local_button_fresh(now) && input.cycle_record_ready) {
            run_id_ = next_id_++;
            if (!next_id_)
                next_id_ = 1;
            transition(State::PREHEAT, now);
            record("TRIAL_START", now);
        }
        break;
    case State::PREHEAT:
        if (elapsed >= (!trial_active_ ? envelope_.preheat_ms : kPreheatMinimumMs)) {
            condition_active_ = false;
            transition(State::IGNITION, now);
        }
        break;
    case State::IGNITION:
        if (trial_active_) {
            if (elapsed >= kTrialMeasurementMs) {
                record("TRIAL_FINISHED", now);
                begin_stop(now);
            }
            break;
        }
        if (elapsed >= envelope_.ignition_timeout_ms) {
            latch_fault(FAULT_IGNITION_TIMEOUT, now);
        } else if (elapsed >= kIgnitionGlowMs &&
                   temperature_confirmed(
                       input.sensors.exhaust.sample_ms >= state_started_ms_ &&
                           input.sensors.exhaust.sample_ms - state_started_ms_ >= kIgnitionGlowMs &&
                           input.sensors.exhaust.milli_c >= envelope_.minimum_run_exhaust_milli_c,
                       input.sensors.exhaust.sample_ms)) {
            transition(State::RUN, now);
            condition_active_ = false;
            run_speed_code_ = envelope_.maximum_speed_code;
            regulator_.reset(now, input.sensors.room.milli_c / 1000.0, 1.0);
        }
        break;
    case State::RUN: {
        if (temperature_confirmed(input.sensors.exhaust.milli_c < envelope_.minimum_run_exhaust_milli_c,
                                  input.sensors.exhaust.sample_ms))
            latch_fault(FAULT_FLAME_LOST, now);
        else {
            const auto previous = combustion_monitor_.snapshot().phase;
            const auto observed = combustion_monitor_.observe(now, input.sensors, outputs().speed_code);
            if (observed.phase != previous && observed.phase == combustion::Phase::SUSPECT)
                record("COMBUSTION_SUSPECT", now);
            if (observed.phase == combustion::Phase::LOST)
                latch_fault(FAULT_COMBUSTION_DROP, now);
            else if (!condition_active_ && observed.phase != combustion::Phase::SUSPECT) {
                regulate(now, input.sensors);
            }
        }
        break;
    }
    case State::STOP_PURGE:
        if (purge_ready(input.sensors, now)) {
            transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now);
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
    result.reset_fault_block = reset_fault_guard(last_sensors_, now_ms, previous_external_faults_);
    result.trial_active = trial_active_;
    result.trial_start_block = trial_start_guard(last_sensors_, now_ms);
    if (trial_active_) {
        const uint64_t elapsed = now_ms >= state_started_ms_ ? now_ms - state_started_ms_ : 0;
        const uint64_t period = state_ == State::TRIAL_READY ? kTrialArmMs
                                : state_ == State::PREHEAT   ? kPreheatMinimumMs
                                : state_ == State::IGNITION  ? kTrialMeasurementMs
                                                             : 0;
        result.trial_remaining_ms = elapsed < period ? period - elapsed : 0;
    }
    result.duration_active = duration_active_;
    result.duration_remaining_ms = duration_active_ ? remaining(now_ms, duration_deadline_ms_) : 0;
    result.purge_active = purging(state_);
    result.purge_id = result.purge_active ? purge_id_ : 0;
    const uint64_t purge_duration = purge_minimum_ms();
    const uint64_t purge_deadline = purge_started_ms_ > std::numeric_limits<uint64_t>::max() - purge_duration
                                        ? std::numeric_limits<uint64_t>::max()
                                        : purge_started_ms_ + purge_duration;
    result.purge_minimum_remaining_ms = result.purge_active ? remaining(now_ms, purge_deadline) : 0;
    result.purge_sensors_fresh = result.purge_active && thermal_sensors_fresh(last_sensors_, now_ms);
    result.purge_temperature_ready = result.purge_sensors_fresh && cooling_ready(last_sensors_, now_ms);
    result.envelope_provisioned = envelope_.provisioned;
    if (!envelope_.provisioned)
        result.start_block = Block::ENVELOPE_MISSING;
    else if (faults_)
        result.start_block = Block::FAULT_LATCHED;
    else if (state_ == State::RECOVERY_PURGE)
        result.start_block = Block::RECOVERY_PURGE;
    else if (state_ == State::MAINTENANCE)
        result.start_block = Block::MAINTENANCE;
    else if (state_ != State::IDLE)
        result.start_block = Block::BUSY;
    else
        result.start_block = start_guard(last_sensors_, now_ms);
    result.combustion = combustion_monitor_.snapshot();
    if (state_ != State::RUN && result.combustion.phase != combustion::Phase::LOST)
        result.combustion = {};
    const auto command_maximum = !trial_active_ ? envelope_.maximum_speed_code : kPwmMaximum;
    result.command_percent = command_maximum ? 100.0 * result.outputs.speed_code / command_maximum : 0;
    result.working_span_fraction = regulator_.value().fraction;
    result.room_excess = state_ == State::RUN && settings_.mode == Mode::TEMPERATURE &&
                         room_temperature_fresh(last_sensors_.room, now_ms) &&
                         last_sensors_.room.milli_c > settings_.room_setpoint_milli_c &&
                         result.outputs.speed_code <= envelope_.minimum_speed_code;
    return result;
}

bool Controller::install_safety_envelope(const SafetyEnvelope &profile, bool recover, uint64_t now_ms) {
    if (!valid_profile_draft(profile) ||
        (state_ != State::UNCONFIGURED && state_ != State::IDLE && state_ != State::FAULT))
        return false;
    const bool was_unconfigured = !envelope_.provisioned;
    envelope_ = profile;
    envelope_.provisioned = valid_safety_envelope(envelope_);
    combustion_monitor_ = {};
    regulator_ = Regulator{};
    settings_.manual_percent = std::max(settings_.manual_percent, minimum_manual_percent(envelope_));
    run_speed_code_ = profile.maximum_speed_code;
    if (++state_version_ == 0)
        state_version_ = 1;
    if (was_unconfigured || !envelope_.provisioned) {
        if (recover && envelope_.provisioned)
            begin_purge(faults_ ? State::FAULT_PURGE : State::RECOVERY_PURGE, now_ms);
        else
            transition(faults_ ? State::FAULT : (envelope_.provisioned ? State::IDLE : State::UNCONFIGURED),
                       now_ms);
    }
    record("PROFILE_SAVED", now_ms);
    return true;
}

bool Controller::update_settings(const Settings &settings) {
    if (!valid_settings(settings) || trial_active_ || state_ == State::MAINTENANCE ||
        combustion_monitor_.snapshot().phase == combustion::Phase::SUSPECT)
        return false;
    Settings accepted = settings;
    if (active(state_) && settings_.mode == Mode::TEMPERATURE && settings.mode == Mode::MANUAL &&
        envelope_.maximum_speed_code) {
        const long step = std::lround(20.0 * run_speed_code_ / envelope_.maximum_speed_code);
        accepted.manual_percent = static_cast<uint8_t>(std::clamp(step * 5, 5L, 100L));
    }
    accepted.manual_percent = std::max(accepted.manual_percent, minimum_manual_percent(envelope_));
    settings_ = accepted;
    ++state_version_;
    return true;
}

void Controller::regulate(uint64_t now_ms, const Sensors &sensors) {
    const double span = envelope_.maximum_speed_code - envelope_.minimum_speed_code;
    if (span <= 0) {
        run_speed_code_ = envelope_.maximum_speed_code;
        return;
    }
    const double manual = std::clamp(
        (settings_.manual_percent * envelope_.maximum_speed_code / 100.0 - envelope_.minimum_speed_code) /
            span,
        0.0, 1.0);
    const double room =
        sensors.room.valid ? sensors.room.milli_c / 1000.0 : settings_.room_setpoint_milli_c / 1000.0;
    const auto regulated = regulator_.tick(now_ms, room, settings_.room_setpoint_milli_c / 1000.0,
                                           settings_.mode == Mode::TEMPERATURE, manual,
                                           settings_.slew_up_per_s, settings_.slew_down_per_s);
    run_speed_code_ = envelope_.minimum_speed_code + regulated.fraction * span;
}

void Controller::record(const char *kind, uint64_t now_ms) {
    if (event_count_ == events_.size()) {
        std::move(events_.begin() + 1, events_.end(), events_.begin());
        --event_count_;
    }
    events_[event_count_++] = {
        now_ms, kind, state_, outputs().speed_code, last_sensors_.exhaust.milli_c, last_sensors_.air.milli_c,
        faults_};
}

int32_t decode_max31856_signed19(uint8_t msb, uint8_t mid, uint8_t lsb) {
    const uint32_t raw24 = (static_cast<uint32_t>(msb) << 16) | (static_cast<uint32_t>(mid) << 8) | lsb;
    int32_t raw19 = static_cast<int32_t>(raw24 >> 5);
    if (raw19 & 0x40000)
        raw19 |= ~0x7FFFF;
    return raw19;
}

int32_t max31856_milli_c(uint8_t msb, uint8_t mid, uint8_t lsb) {
    return static_cast<int32_t>((static_cast<int64_t>(decode_max31856_signed19(msb, mid, lsb)) * 1000) / 128);
}

uint8_t onewire_crc8(const uint8_t *data, size_t size) {
    uint8_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t value = data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint8_t mix = (crc ^ value) & 1U;
            crc >>= 1;
            if (mix)
                crc ^= 0x8CU;
            value >>= 1;
        }
    }
    return crc;
}

bool decode_ds18b20_scratchpad(const uint8_t frame[9], unsigned requested_resolution_bits,
                               bool conversion_completed, int32_t *milli_c) {
    if (!frame || !milli_c || !conversion_completed || requested_resolution_bits < 9 ||
        requested_resolution_bits > 12 || onewire_crc8(frame, 8) != frame[8] || (frame[4] & 0x9FU) != 0x1FU)
        return false;
    const unsigned resolution = (frame[4] >> 5) & 0x03U;
    if (requested_resolution_bits != resolution + 9)
        return false;
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

bool FailClosedOutputDriver::apply(const Outputs &requested, void *context, DigitalWriter digital_write,
                                   PwmWriter pwm_write) {
    if (!digital_write || !pwm_write)
        return false;
    const auto write_off = [&](OutputLine line, bool *current) {
        const bool off = digital_write(context, line, false);
        if (off)
            *current = false;
        return off;
    };
    const auto force_combustion_outputs_off = [&] {
        const bool fuel_off = write_off(OutputLine::FUEL, &current_.fuel);
        const bool glow_off = write_off(OutputLine::GLOW, &current_.glow);
        return fuel_off && glow_off;
    };

    bool ok = requested.speed_code <= kPwmMaximum &&
              (!requested.fuel || (requested.fan_request && requested.speed_code > 0));
    if (!requested.fuel)
        ok = write_off(OutputLine::FUEL, &current_.fuel) && ok;
    if (!requested.glow)
        ok = write_off(OutputLine::GLOW, &current_.glow) && ok;
    const bool fan_written = digital_write(context, OutputLine::FAN_REQUEST, requested.fan_request);
    if (fan_written)
        current_.fan_request = requested.fan_request;
    ok = fan_written && ok;
    if (ok) {
        const bool pwm_written = pwm_write(context, requested.speed_code);
        if (pwm_written)
            current_.speed_code = requested.speed_code;
        ok = pwm_written;
    }
    if (!ok) {
        // Never claim PWM=0 if its writer did not confirm it. A normal purge
        // deliberately keeps a non-zero fan command, so its error path does not
        // inject a conflicting PWM-off request.
        force_combustion_outputs_off();
        if (!requested.fan_request && pwm_write(context, 0))
            current_.speed_code = 0;
        return false;
    }
    if (requested.glow && !current_.glow) {
        const bool glow_written = digital_write(context, OutputLine::GLOW, true);
        if (glow_written)
            current_.glow = true;
        ok = glow_written;
    }
    if (ok && requested.fuel && !current_.fuel) {
        const bool fuel_written = digital_write(context, OutputLine::FUEL, true);
        if (fuel_written)
            current_.fuel = true;
        ok = fuel_written;
    }
    if (!ok) {
        force_combustion_outputs_off();
        if (!requested.fan_request && pwm_write(context, 0))
            current_.speed_code = 0;
    }
    return ok;
}

const char *state_name(State state) {
    switch (state) {
    case State::UNCONFIGURED:
        return "UNCONFIGURED";
    case State::RECOVERY_PURGE:
        return "RECOVERY_PURGE";
    case State::IDLE:
        return "IDLE";
    case State::PREHEAT:
        return "PREHEAT";
    case State::IGNITION:
        return "IGNITION";
    case State::RUN:
        return "RUN";
    case State::STOP_PURGE:
        return "STOP_PURGE";
    case State::FAULT_PURGE:
        return "FAULT_PURGE";
    case State::FAULT:
        return "FAULT";
    case State::MAINTENANCE:
        return "MAINTENANCE";
    case State::TRIAL_READY:
        return "TRIAL_READY";
    }
    return "UNKNOWN";
}

const char *block_name(Block block) {
    switch (block) {
    case Block::NONE:
        return "NONE";
    case Block::INITIALIZING:
        return "INITIALIZING";
    case Block::STORAGE_UNAVAILABLE:
        return "STORAGE_UNAVAILABLE";
    case Block::ENVELOPE_MISSING:
        return "ENVELOPE_MISSING";
    case Block::RECOVERY_PURGE:
        return "RECOVERY_PURGE";
    case Block::SENSOR_INVALID:
        return "SENSOR_INVALID";
    case Block::INITIAL_THERMAL_STATE:
        return "INITIAL_THERMAL_STATE";
    case Block::FAULT_LATCHED:
        return "FAULT_LATCHED";
    case Block::BUSY:
        return "BUSY";
    case Block::MAINTENANCE:
        return "MAINTENANCE";
    case Block::LOCAL_CONTROLS_UNAVAILABLE:
        return "LOCAL_CONTROLS_UNAVAILABLE";
    case Block::STALE_COMMAND:
        return "STALE_COMMAND";
    case Block::ENVELOPE_INVALID:
        return "ENVELOPE_INVALID";
    case Block::SENSOR_STALE:
        return "SENSOR_STALE";
    case Block::BUTTON_HELD:
        return "BUTTON_HELD";
    }
    return "UNKNOWN";
}

} // namespace sirokko
