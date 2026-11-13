#include "sirokko/control.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>

namespace sirokko {
namespace {

bool active(State state) {
    return state == State::PREPURGE || state == State::PREHEAT || state == State::IGNITION ||
           state == State::VERIFY || state == State::RUN;
}

bool purging(State state) {
    return state == State::RECOVERY_PURGE || state == State::STOP_PURGE || state == State::FAULT_PURGE;
}

uint64_t remaining(uint64_t now, uint64_t deadline) {
    return now >= deadline ? 0 : deadline - now;
}

CommissioningPhase commissioning_phase(calibration::Phase phase) {
    switch (phase) {
    case calibration::Phase::IDLE:
        return CommissioningPhase::IDLE;
    case calibration::Phase::WAIT_REFERENCE_COMMAND:
        return CommissioningPhase::WAIT_REFERENCE_COMMAND;
    case calibration::Phase::SETTLE_REFERENCE:
        return CommissioningPhase::SETTLE_REFERENCE;
    case calibration::Phase::WAIT_DESCENT_COMMAND:
        return CommissioningPhase::WAIT_DESCENT_COMMAND;
    case calibration::Phase::SETTLE_POINT:
        return CommissioningPhase::SETTLE_POINT;
    case calibration::Phase::WAIT_RETURN_COMMAND:
        return CommissioningPhase::WAIT_RETURN_COMMAND;
    case calibration::Phase::SETTLE_RETURN_REFERENCE:
        return CommissioningPhase::SETTLE_RETURN_REFERENCE;
    case calibration::Phase::WAIT_REPEAT_COMMAND:
        return CommissioningPhase::WAIT_REPEAT_COMMAND;
    case calibration::Phase::SETTLE_REPEAT_POINT:
        return CommissioningPhase::SETTLE_REPEAT_POINT;
    case calibration::Phase::WAIT_POST_REPEAT_REFERENCE_COMMAND:
        return CommissioningPhase::WAIT_POST_REPEAT_REFERENCE_COMMAND;
    case calibration::Phase::SETTLE_POST_REPEAT_REFERENCE:
        return CommissioningPhase::SETTLE_POST_REPEAT_REFERENCE;
    case calibration::Phase::COMPLETE_PENDING_STOP:
        return CommissioningPhase::COMPLETE_PENDING_STOP;
    case calibration::Phase::FAILED:
        return CommissioningPhase::FAILED;
    case calibration::Phase::CANCELLED:
        return CommissioningPhase::CANCELLED;
    }
    return CommissioningPhase::FAILED;
}

CommissioningReason commissioning_reason(calibration::Reason reason) {
    return static_cast<CommissioningReason>(reason);
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
    if (!envelope_.provisioned && !(startup == Startup::RECOVER && debug_heat_configured(envelope_))) {
        state_ = State::UNCONFIGURED;
        return;
    }
    run_speed_code_ = envelope_.reference_speed_code;
    state_ = startup == Startup::CLEAN ? State::IDLE : State::RECOVERY_PURGE;
    if (state_ == State::RECOVERY_PURGE) {
        purge_id_ = next_id_++;
        purge_minimum_ms_ =
            envelope_.recovery_purge_ms ? envelope_.recovery_purge_ms : envelope_.postpurge_ms;
    }
}

bool Controller::thermal_sensors_fresh(const Sensors &sensors, uint64_t now_ms) const {
    const auto fresh = [now_ms](const SensorSample &sample) {
        return sample.valid && sample.status == 0 && sample.sample_ms <= now_ms &&
               now_ms - sample.sample_ms <= kSensorStaleMs && sample.milli_c >= kThermocoupleMinimumMilliC &&
               sample.milli_c < kProcessMeasurementCeilingMilliC;
    };
    return fresh(sensors.exhaust) && fresh(sensors.air);
}

bool Controller::sensors_fresh(const Sensors &sensors, uint64_t now_ms) const {
    const auto &r = sensors.room;
    const bool room_ok = r.valid && r.status == 0 && r.sample_ms <= now_ms &&
                         now_ms - r.sample_ms <= kRoomSensorStaleMs && r.milli_c >= -55000 &&
                         r.milli_c <= 125000;
    return thermal_sensors_fresh(sensors, now_ms) && (settings_.mode == Mode::MANUAL || room_ok);
}

uint32_t Controller::sensor_faults(const Sensors &sensors, uint64_t now_ms) const {
    const auto bad = [now_ms](const SensorSample &sample, uint64_t limit) {
        return !sample.valid || sample.status != 0 || sample.sample_ms > now_ms ||
               now_ms - sample.sample_ms > limit;
    };
    uint32_t result = FAULT_NONE;
    if (bad(sensors.exhaust, kSensorStaleMs) || sensors.exhaust.milli_c < kThermocoupleMinimumMilliC)
        result |= FAULT_EXHAUST;
    if (bad(sensors.air, kSensorStaleMs) || sensors.air.milli_c < kThermocoupleMinimumMilliC)
        result |= FAULT_AIR;
    if (settings_.mode == Mode::TEMPERATURE &&
        (bad(sensors.room, kRoomSensorStaleMs) || sensors.room.milli_c < -55000 ||
         sensors.room.milli_c > 125000))
        result |= FAULT_ROOM;
    if ((sensors.exhaust.valid && sensors.exhaust.milli_c >= kProcessMeasurementCeilingMilliC) ||
        (sensors.air.valid && sensors.air.milli_c >= kProcessMeasurementCeilingMilliC))
        result |= FAULT_OVER_TEMPERATURE;
    if (envelope_.maximum_exhaust_milli_c && sensors.exhaust.valid &&
        sensors.exhaust.milli_c >= envelope_.maximum_exhaust_milli_c)
        result |= FAULT_OVER_TEMPERATURE;
    if (envelope_.maximum_air_milli_c && sensors.air.valid &&
        sensors.air.milli_c >= envelope_.maximum_air_milli_c)
        result |= FAULT_OVER_TEMPERATURE;
    return result;
}

Block Controller::start_guard(const Sensors &sensors, uint64_t now_ms) const {
    if (!envelope_.provisioned)
        return Block::ENVELOPE_MISSING;
    if (faults_)
        return Block::FAULT_LATCHED;
    if (model_candidate_.valid)
        return Block::BUSY;
    if (state_ != State::IDLE)
        return state_ == State::MAINTENANCE ? Block::MAINTENANCE : Block::BUSY;
    if (!sensors_fresh(sensors, now_ms)) {
        const auto stale = [now_ms](const SensorSample &sample, uint64_t limit) {
            return sample.valid && sample.status == 0 && sample.sample_ms <= now_ms &&
                   now_ms - sample.sample_ms > limit;
        };
        if (stale(sensors.exhaust, kSensorStaleMs) || stale(sensors.air, kSensorStaleMs) ||
            (settings_.mode == Mode::TEMPERATURE && stale(sensors.room, kRoomSensorStaleMs)))
            return Block::SENSOR_STALE;
        return Block::SENSOR_INVALID;
    }
    if (sensors.exhaust.milli_c < kThermocoupleMinimumMilliC ||
        sensors.exhaust.milli_c >= envelope_.maximum_exhaust_milli_c ||
        sensors.air.milli_c < kThermocoupleMinimumMilliC ||
        sensors.air.milli_c >= envelope_.maximum_air_milli_c || !cooling_complete(envelope_, sensors, now_ms))
        return Block::INITIAL_THERMAL_STATE;
    if (settings_.duration_ms > std::numeric_limits<uint64_t>::max() - now_ms)
        return Block::STALE_COMMAND;
    return Block::NONE;
}

void Controller::transition(State state, uint64_t now_ms) {
    if (state_ == state)
        return;
    state_ = state;
    state_started_ms_ = now_ms;
    ++state_version_;
    if (state_version_ == 0)
        state_version_ = 1;
}

void Controller::begin_purge(State state, uint64_t now_ms) {
    if (!purging(state_)) {
        purge_started_ms_ = now_ms;
        purge_minimum_ms_ = state == State::RECOVERY_PURGE && envelope_.recovery_purge_ms
                                ? envelope_.recovery_purge_ms
                                : envelope_.postpurge_ms;
        purge_id_ = next_id_++;
        if (next_id_ == 0)
            next_id_ = 1;
    }
    transition(state, now_ms);
}

bool Controller::purge_ready(const Sensors &sensors, uint64_t now_ms) const {
    return debug_heat_configured(envelope_) && now_ms >= purge_started_ms_ &&
           now_ms - purge_started_ms_ >= purge_minimum_ms_ && thermal_sensors_fresh(sensors, now_ms) &&
           cooling_complete(envelope_, sensors, now_ms);
}

void Controller::begin_start(uint64_t now_ms, const Sensors &sensors) {
    combustion_monitor_.reset(envelope_.minimum_run_exhaust_milli_c, envelope_.reference_speed_code);
    run_id_ = next_id_++;
    if (next_id_ == 0)
        next_id_ = 1;
    ignition_baseline_milli_c_ = sensors.exhaust.milli_c;
    ignition_air_baseline_milli_c_ = sensors.air.milli_c;
    run_speed_code_ = envelope_.reference_speed_code;
    duration_active_ = !commissioning_requested_ && settings_.duration_ms != 0;
    duration_deadline_ms_ = duration_active_ ? now_ms + settings_.duration_ms : 0;
    transition(State::PREPURGE, now_ms);
    record("START", now_ms);
}

void Controller::begin_stop(uint64_t now_ms, bool preserve_calibration_candidate) {
    if (state_ == State::DEBUG) {
        record("DEBUG_STOP", now_ms);
        debug_outputs_ = {};
        debug_deadline_ms_ = 0;
        run_id_ = 0;
        if (debug_heated_)
            begin_purge(State::STOP_PURGE, now_ms);
        else
            transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now_ms);
        return;
    }
    if (combustion_monitor_.snapshot().phase != combustion::Phase::LOST)
        combustion_monitor_ = {};
    const bool cancel_presealed_calibration =
        commissioning_requested_ ||
        (model_candidate_.valid && !model_storage_sealed_ && !preserve_calibration_candidate);
    if (cancel_presealed_calibration)
        cancel_commissioning(calibration::Reason::EXTERNAL_CANCEL, now_ms);
    if (active(state_) || (model_candidate_.valid && model_storage_sealed_))
        record("STOP", now_ms);
    duration_active_ = false;
    duration_deadline_ms_ = 0;
    run_id_ = 0;
    if (active(state_))
        begin_purge(State::STOP_PURGE, now_ms);
}

void Controller::latch_fault(uint32_t faults, uint64_t now_ms) {
    if (combustion_monitor_.snapshot().phase != combustion::Phase::LOST)
        combustion_monitor_ = {};
    if (commissioning_requested_ || (model_candidate_.valid && !model_storage_sealed_))
        cancel_commissioning(calibration::Reason::RUN_CONTEXT_LOST, now_ms);
    faults_ |= faults;
    record("FAULT", now_ms);
    duration_active_ = false;
    duration_deadline_ms_ = 0;
    run_id_ = 0;
    if (active(state_) || purging(state_) || (state_ == State::DEBUG && debug_heated_) ||
        (faults & FAULT_OVER_TEMPERATURE))
        begin_purge(State::FAULT_PURGE, now_ms);
    else
        transition(State::FAULT, now_ms);
}

Outputs Controller::outputs() const {
    Outputs result{};
    if (state_ == State::DEBUG)
        return debug_outputs_;
    if (!envelope_.provisioned && !purging(state_))
        return result;
    const uint16_t purge = envelope_.purge_speed_code ? envelope_.purge_speed_code : kPwmMaximum;
    switch (state_) {
    case State::RECOVERY_PURGE:
    case State::PREPURGE:
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
        result = {true, envelope_.reference_speed_code, true, true};
        break;
    case State::VERIFY:
        result = {true, envelope_.reference_speed_code, false, true};
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
    return sensor_faults(sensors, now_ms) ? Block::SENSOR_INVALID : Block::NONE;
}

Block Controller::debug_heat_guard(const Sensors &sensors, uint64_t now_ms) const {
    if (!debug_heat_configured(envelope_))
        return Block::ENVELOPE_MISSING;
    if (!thermal_sensors_fresh(sensors, now_ms))
        return Block::SENSOR_INVALID;
    if (sensor_faults(sensors, now_ms) & ~static_cast<uint32_t>(FAULT_ROOM))
        return Block::SENSOR_INVALID;
    if (faults_)
        return Block::FAULT_LATCHED;
    return Block::NONE;
}

void Controller::debug_tick(const TickInput &input) {
    const auto now = input.now_ms;
    uint32_t faults = input.external_faults;
    if (have_tick_ && now < last_tick_ms_)
        faults |= FAULT_CONTROL_WATCHDOG;
    if (debug_heated_)
        faults |= sensor_faults(input.sensors, now) & ~static_cast<uint32_t>(FAULT_ROOM);
    if (faults) {
        latch_fault(faults, now);
        return;
    }
    if (input.stop || input.operator_combustion_problem || now >= debug_deadline_ms_) {
        if (now >= debug_deadline_ms_)
            record("DEBUG_LINK_LOST", now);
        ++state_version_;
        begin_stop(now);
        return;
    }
    if (input.set_debug_outputs && valid_debug_outputs(input.debug_outputs)) {
        const auto &requested = input.debug_outputs;
        const bool heat = requested.fuel || requested.glow;
        // A heat test requires durable recovery state and thermal limits, not a learned model.
        const bool permitted =
            !heat || (input.cycle_record_ready && debug_heat_guard(input.sensors, now) == Block::NONE);
        // After heating, fan-off ends the test through normal cooling.
        if (permitted && debug_heated_ && !requested.fan_request && !requested.glow) {
            begin_stop(now);
            return;
        }
        if (permitted) {
            if (requested.glow && !debug_outputs_.glow)
                debug_glow_started_ms_ = now;
            if (requested.fuel && !debug_outputs_.fuel) {
                debug_fuel_started_ms_ = now;
                debug_flame_seen_ = false;
                combustion_monitor_.reset(envelope_.minimum_run_exhaust_milli_c, requested.speed_code);
            }
            const bool changed = requested.speed_code != debug_outputs_.speed_code ||
                                 requested.glow != debug_outputs_.glow ||
                                 requested.fuel != debug_outputs_.fuel;
            debug_outputs_ = requested;
            debug_heated_ |= heat;
            debug_deadline_ms_ = now + 5000;
            ++state_version_;
            if (changed)
                record("DEBUG_OUTPUTS", now);
        }
    }
    if (debug_outputs_.glow && now - debug_glow_started_ms_ >= kPreheatMaximumMs) {
        debug_outputs_.glow = false;
        ++state_version_;
        record("DEBUG_GLOW_LIMIT", now);
    }
    if (debug_outputs_.fuel) {
        const auto threshold = envelope_.minimum_run_exhaust_milli_c;
        if (threshold > 0 && input.sensors.exhaust.milli_c >= threshold)
            debug_flame_seen_ = true;
        if (debug_flame_seen_ && input.sensors.exhaust.milli_c < threshold) {
            latch_fault(FAULT_FLAME_LOST, now);
            return;
        }
        if (!debug_flame_seen_ && now - debug_fuel_started_ms_ >= kFiringStartupMaximumMs) {
            latch_fault(FAULT_IGNITION_TIMEOUT, now);
            return;
        }
        if (debug_flame_seen_ && !debug_outputs_.glow &&
            combustion_monitor_.observe(now, input.sensors, debug_outputs_.speed_code).phase ==
                combustion::Phase::LOST)
            latch_fault(FAULT_COMBUSTION_DROP, now);
    }
}

Snapshot Controller::tick(const TickInput &input) {
    const uint64_t now = input.now_ms;
    last_sensors_ = input.sensors;
    if (input.enter_debug && !input.stop && !input.external_faults && !faults_ &&
        (state_ == State::IDLE || state_ == State::UNCONFIGURED) && !commissioning_requested_ &&
        !model_candidate_.valid) {
        debug_outputs_ = {};
        debug_heated_ = debug_flame_seen_ = false;
        debug_deadline_ms_ = now + 5000;
        run_id_ = next_id_++;
        transition(State::DEBUG, now);
        record("DEBUG_ENTER", now);
    }
    if (state_ == State::DEBUG) {
        debug_tick(input);
        last_tick_ms_ = now;
        have_tick_ = true;
        return snapshot(now);
    }
    if (!envelope_.provisioned && !purging(state_) && state_ != State::FAULT) {
        last_tick_ms_ = now;
        faults_ = input.external_faults;
        if (input.stop || input.exit_maintenance) {
            transition(State::UNCONFIGURED, now);
            if (input.stop)
                ++state_version_;
        } else if (input.enter_maintenance && !faults_ &&
                   (!debug_heat_configured(envelope_) || cooling_complete(envelope_, input.sensors, now))) {
            transition(State::MAINTENANCE, now);
        }
        return snapshot(now);
    }
    last_sensors_ = input.sensors;
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
    if (active(state_) || state_ == State::MAINTENANCE)
        new_faults |= current_sensor_faults;
    if (model_candidate_.valid && purging(state_))
        new_faults |= current_sensor_faults & ~static_cast<uint32_t>(FAULT_ROOM);

    // Preserve the operator report with the pre-fault state/output context. A
    // simultaneous sensor or external fault still owns the state transition below.
    if (input.operator_combustion_problem && (commissioning_requested_ || model_candidate_.valid)) {
        record("OPERATOR_COMBUSTION_PROBLEM", now);
        cancel_commissioning(calibration::Reason::OPERATOR_NEGATIVE, now);
    }
    if (new_faults)
        latch_fault(new_faults, now);

    // A pending/staged range is no longer trustworthy once either thermal
    // channel becomes invalid, even while IDLE has not escalated it to a fault.
    if (model_candidate_.valid && !model_storage_sealed_ && state_ == State::IDLE &&
        !thermal_sensors_fresh(input.sensors, now))
        cancel_commissioning(calibration::Reason::SENSOR_INVALID, now);

    // STOP is an independent priority barrier. It cancels queued starts and never writes GPIO directly.
    const bool stop_barrier = input.stop || input.operator_combustion_problem;
    if (stop_barrier) {
        // Revoke queued commands even when STOP leaves an idle controller in IDLE.
        if (++state_version_ == 0)
            state_version_ = 1;
        begin_stop(now);
        if (state_ == State::MAINTENANCE)
            transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now);
    }

    if (input.enter_maintenance && !stop_barrier && state_ == State::IDLE && faults_ == 0 &&
        !model_candidate_.valid && thermal_sensors_fresh(input.sensors, now) &&
        cooling_complete(envelope_, input.sensors, now)) {
        transition(State::MAINTENANCE, now);
    } else if (input.exit_maintenance && state_ == State::MAINTENANCE) {
        transition(faults_ ? State::FAULT : State::IDLE, now);
    }

    bool reset_barrier = false;
    if (input.reset_fault && reset_fault_guard(input.sensors, now, input.external_faults) == Block::NONE) {
        faults_ = FAULT_NONE;
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
    if (!stop_barrier && !expired && input.set_shutdown_timer && active(state_) &&
        input.shutdown_after_ms <= kMaximumTimerMs &&
        input.shutdown_after_ms <= std::numeric_limits<uint64_t>::max() - now) {
        duration_active_ = input.shutdown_after_ms != 0;
        duration_deadline_ms_ = duration_active_ ? now + input.shutdown_after_ms : 0;
        ++state_version_;
        record(duration_active_ ? "TIMER_ARMED" : "TIMER_CANCELLED", now);
    }

    const Block requested_start_block = start_guard(input.sensors, now);
    const Block requested_commissioning_block =
        input.start_commissioning ? commissioning_configuration_block(envelope_, settings_) : Block::NONE;
    if (!stop_barrier && !reset_barrier && !expired && input.cycle_record_ready &&
        (input.start || input.start_commissioning) && requested_start_block == Block::NONE &&
        requested_commissioning_block == Block::NONE && (input.start_commissioning || model_.valid)) {
        model_saved_ = false;
        model_storage_id_ = 0;
        model_storage_sealed_ = false;
        if (input.start_commissioning)
            model_storage_unconfirmed_ = false;
        calibrator_ = {};
        commissioning_requested_ = input.start_commissioning;
        commissioning_started_ms_ = now;
        begin_start(now, input.sensors);
        if (commissioning_requested_)
            record("CALIBRATION_START", now);
    }

    const uint64_t elapsed = now >= state_started_ms_ ? now - state_started_ms_ : 0;
    switch (state_) {
    case State::RECOVERY_PURGE:
        if (purge_ready(input.sensors, now)) {
            transition(envelope_.provisioned ? State::IDLE : State::UNCONFIGURED, now);
            purge_id_ = 0;
        }
        break;
    case State::PREPURGE:
        if (elapsed >= envelope_.prepurge_ms)
            transition(State::PREHEAT, now);
        break;
    case State::PREHEAT:
        if (elapsed >= envelope_.preheat_ms) {
            // Sample immediately before fuel admission, not before a potentially hot glow preparation.
            ignition_baseline_milli_c_ = input.sensors.exhaust.milli_c;
            ignition_air_baseline_milli_c_ = input.sensors.air.milli_c;
            transition(State::IGNITION, now);
        }
        break;
    case State::IGNITION:
        if (elapsed >= envelope_.ignition_ms) {
            if (static_cast<int64_t>(input.sensors.exhaust.milli_c) - ignition_baseline_milli_c_ >=
                    envelope_.minimum_ignition_rise_milli_c &&
                static_cast<int64_t>(input.sensors.air.milli_c) - ignition_air_baseline_milli_c_ >=
                    envelope_.minimum_ignition_air_rise_milli_c)
                transition(State::VERIFY, now);
            else
                latch_fault(FAULT_IGNITION_TIMEOUT, now);
        }
        break;
    case State::VERIFY:
        if (elapsed >= envelope_.verify_ms) {
            if (input.sensors.exhaust.milli_c >= envelope_.minimum_run_exhaust_milli_c &&
                static_cast<int64_t>(input.sensors.air.milli_c) - ignition_air_baseline_milli_c_ >=
                    envelope_.minimum_ignition_air_rise_milli_c) {
                transition(State::RUN, now);
                regulator_.reset(now, input.sensors.room.milli_c / 1000.0, 1.0);
                if (commissioning_requested_)
                    start_commissioning_run(now);
            } else
                latch_fault(FAULT_IGNITION_TIMEOUT, now);
        }
        break;
    case State::RUN: {
        if (input.sensors.exhaust.milli_c < envelope_.minimum_run_exhaust_milli_c)
            latch_fault(FAULT_FLAME_LOST, now);
        else {
            const auto previous = combustion_monitor_.snapshot().phase;
            const auto observed = combustion_monitor_.observe(now, input.sensors, outputs().speed_code);
            if (observed.phase != previous && observed.phase == combustion::Phase::SUSPECT)
                record("COMBUSTION_SUSPECT", now);
            if (observed.phase == combustion::Phase::LOST)
                latch_fault(FAULT_COMBUSTION_DROP, now);
            else if (observed.phase != combustion::Phase::SUSPECT) {
                // Do not change the command or learn a new calibration point while a drop is being checked.
                if (commissioning_requested_)
                    run_commissioning(now, input.sensors);
                else
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
    result.debug_remaining_ms = state_ == State::DEBUG ? remaining(now_ms, debug_deadline_ms_) : 0;
    result.debug_heat_block = debug_heat_guard(last_sensors_, now_ms);
    result.duration_active = duration_active_;
    result.duration_remaining_ms = duration_active_ ? remaining(now_ms, duration_deadline_ms_) : 0;
    result.purge_active = purging(state_);
    result.purge_id = result.purge_active ? purge_id_ : 0;
    const uint64_t purge_deadline =
        purge_started_ms_ > std::numeric_limits<uint64_t>::max() - purge_minimum_ms_
            ? std::numeric_limits<uint64_t>::max()
            : purge_started_ms_ + purge_minimum_ms_;
    result.purge_minimum_remaining_ms = result.purge_active ? remaining(now_ms, purge_deadline) : 0;
    result.purge_sensors_fresh = result.purge_active && thermal_sensors_fresh(last_sensors_, now_ms);
    result.purge_temperature_ready =
        result.purge_sensors_fresh && cooling_complete(envelope_, last_sensors_, now_ms);
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
    const Block common_start_block = result.start_block;
    if (result.start_block == Block::NONE && !model_.valid)
        result.start_block = Block::MODEL_UNAVAILABLE;
    result.combustion = combustion_monitor_.snapshot();
    if (state_ != State::RUN && state_ != State::DEBUG && result.combustion.phase != combustion::Phase::LOST)
        result.combustion = {};
    result.learned_model = model_;
    result.command_percent = envelope_.reference_speed_code
                                 ? 100.0 * result.outputs.speed_code / envelope_.reference_speed_code
                                 : 0;
    result.working_span_fraction = regulator_.value().fraction;
    const auto cal = calibrator_.snapshot(now_ms);
    auto &commissioning = result.commissioning;
    commissioning.active = commissioning_requested_ || model_candidate_.valid;
    commissioning.phase =
        model_storage_unconfirmed_
            ? CommissioningPhase::PERSIST_FAILED
            : (model_saved_ ? CommissioningPhase::SAVED
                            : (model_candidate_.valid && model_storage_sealed_
                                   ? CommissioningPhase::PERSIST_COMMITTING
                                   : (model_candidate_.valid && model_storage_id_ != 0
                                          ? CommissioningPhase::PERSIST_STAGING
                                          : (commissioning_requested_ && state_ != State::RUN
                                                 ? CommissioningPhase::STARTING
                                                 : commissioning_phase(cal.phase)))));
    commissioning.reason = model_storage_unconfirmed_ ? CommissioningReason::STORAGE_UNCONFIRMED
                                                      : commissioning_reason(cal.reason);
    commissioning.target_speed_code = cal.target_code;
    commissioning.accepted_point_count = cal.accepted_point_count;
    commissioning.elapsed_ms =
        now_ms >= commissioning_started_ms_ && (commissioning.active || cal.phase != calibration::Phase::IDLE)
            ? now_ms - commissioning_started_ms_
            : 0;
    commissioning.source_envelope_fingerprint = envelope_fingerprint(envelope_);
    commissioning.installed_model_fingerprint = model_.valid ? model_.fingerprint : 0;
    commissioning.installed_model_revision = model_.valid ? model_.provenance.revision : 0;
    commissioning.block = common_start_block == Block::NONE
                              ? commissioning_configuration_block(envelope_, settings_)
                              : common_start_block;
    result.room_excess = !commissioning.active && state_ == State::RUN &&
                         settings_.mode == Mode::TEMPERATURE && last_sensors_.room.valid &&
                         last_sensors_.room.milli_c > settings_.room_setpoint_milli_c &&
                         (!model_.valid || result.working_span_fraction <= 0.001);
    return result;
}

bool Controller::install_safety_envelope(const SafetyEnvelope &profile, bool recover, uint64_t now_ms) {
    if (!valid_profile_draft(profile) ||
        (state_ != State::UNCONFIGURED && state_ != State::IDLE && state_ != State::FAULT) ||
        commissioning_requested_ || model_candidate_.valid)
        return false;
    const bool was_unconfigured = !envelope_.provisioned;
    envelope_ = profile;
    envelope_.provisioned = valid_safety_envelope(envelope_);
    model_ = {};
    model_saved_ = false;
    model_storage_id_ = 0;
    model_storage_sealed_ = model_storage_unconfirmed_ = false;
    calibrator_ = {};
    commissioning_config_ = {};
    commissioning_started_ms_ = commissioning_control_ms_ = 0;
    commissioning_logged_phase_ = calibration::Phase::IDLE;
    combustion_monitor_ = {};
    regulator_ = Regulator{};
    run_speed_code_ = profile.reference_speed_code;
    if (++state_version_ == 0)
        state_version_ = 1;
    if (was_unconfigured || !envelope_.provisioned) {
        if (recover && debug_heat_configured(envelope_))
            begin_purge(faults_ ? State::FAULT_PURGE : State::RECOVERY_PURGE, now_ms);
        else
            transition(faults_ ? State::FAULT : (envelope_.provisioned ? State::IDLE : State::UNCONFIGURED),
                       now_ms);
    }
    record("PROFILE_SAVED", now_ms);
    return true;
}

bool Controller::update_settings(const Settings &settings) {
    if (!valid_settings(settings) || state_ == State::DEBUG || state_ == State::MAINTENANCE ||
        commissioning_requested_ || combustion_monitor_.snapshot().phase == combustion::Phase::SUSPECT ||
        model_candidate_.valid)
        return false;
    Settings accepted = settings;
    if (active(state_) && settings_.mode == Mode::TEMPERATURE && settings.mode == Mode::MANUAL &&
        envelope_.reference_speed_code) {
        const long step = std::lround(20.0 * run_speed_code_ / envelope_.reference_speed_code);
        accepted.manual_percent = static_cast<uint8_t>(std::clamp(step * 5, 5L, 100L));
    }
    accepted.manual_percent = std::max(accepted.manual_percent, minimum_manual_percent(model_));
    settings_ = accepted;
    ++state_version_;
    return true;
}

bool Controller::install_learned_model(const LearnedOperatingModel &model) {
    if (active(state_) || state_ == State::DEBUG || state_ == State::MAINTENANCE || !envelope_.provisioned ||
        !model.valid || model.provenance.revision == 0 ||
        model.provenance.safety_envelope_fingerprint != envelope_fingerprint(envelope_) ||
        model.fingerprint != learned_model_fingerprint(model) || !envelope_.exploration_floor_code ||
        model.minimum_speed_code < envelope_.exploration_floor_code ||
        model.minimum_speed_code >= model.maximum_speed_code ||
        model.maximum_speed_code > envelope_.reference_speed_code || !std::isfinite(model.slew_up_per_s) ||
        !std::isfinite(model.slew_down_per_s) || model.slew_up_per_s < kMinimumSlewPerS ||
        model.slew_up_per_s > kMaximumSlewPerS || model.slew_down_per_s < kMinimumSlewPerS ||
        model.slew_down_per_s > kMaximumSlewPerS || model.measurements.tested_point_count == 0 ||
        model.measurements.elapsed_ms == 0 ||
        model.provenance.commissioning_completed_ms < model.provenance.commissioning_started_ms ||
        model.measurements.elapsed_ms !=
            model.provenance.commissioning_completed_ms - model.provenance.commissioning_started_ms)
        return false;
    model_ = model;
    run_speed_code_ = envelope_.reference_speed_code;
    ++state_version_;
    return true;
}

bool Controller::prepare_model_revision(const LearnedModelRevision &revision,
                                        LearnedOperatingModel *candidate) const {
    if (!candidate || state_ != State::IDLE || commissioning_requested_ || model_candidate_.valid ||
        !valid_model_revision(model_, revision))
        return false;
    *candidate = model_;
    candidate->minimum_speed_code = revision.minimum_speed_code;
    candidate->maximum_speed_code = revision.maximum_speed_code;
    candidate->slew_up_per_s = revision.slew_up_per_s;
    candidate->slew_down_per_s = revision.slew_down_per_s;
    ++candidate->provenance.revision;
    candidate->fingerprint = learned_model_fingerprint(*candidate);
    return true;
}

void Controller::regulate(uint64_t now_ms, const Sensors &sensors) {
    if (!model_.valid) {
        run_speed_code_ = envelope_.reference_speed_code;
        return;
    }
    const double span = model_.maximum_speed_code - model_.minimum_speed_code;
    const double manual = std::clamp(
        (settings_.manual_percent * model_.maximum_speed_code / 100.0 - model_.minimum_speed_code) / span,
        0.0, 1.0);
    const double room =
        sensors.room.valid ? sensors.room.milli_c / 1000.0 : settings_.room_setpoint_milli_c / 1000.0;
    const auto regulated = regulator_.tick(now_ms, room, settings_.room_setpoint_milli_c / 1000.0,
                                           settings_.mode == Mode::TEMPERATURE, manual,
                                           std::min(settings_.slew_up_per_s, model_.slew_up_per_s),
                                           std::min(settings_.slew_down_per_s, model_.slew_down_per_s));
    run_speed_code_ = model_.minimum_speed_code + regulated.fraction * span;
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
    case State::PREPURGE:
        return "PREPURGE";
    case State::PREHEAT:
        return "PREHEAT";
    case State::IGNITION:
        return "IGNITION";
    case State::VERIFY:
        return "VERIFY";
    case State::RUN:
        return "RUN";
    case State::STOP_PURGE:
        return "STOP_PURGE";
    case State::FAULT_PURGE:
        return "FAULT_PURGE";
    case State::FAULT:
        return "FAULT";
    case State::DEBUG:
        return "DEBUG";
    case State::MAINTENANCE:
        return "MAINTENANCE";
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
    case Block::MODEL_UNAVAILABLE:
        return "MODEL_UNAVAILABLE";
    }
    return "UNKNOWN";
}

} // namespace sirokko
