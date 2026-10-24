#include "sirokko/control.hpp"

#include <algorithm>
#include <cstdint>

namespace sirokko {
namespace {

constexpr uint32_t kSchemaVersion = 1;
constexpr uint32_t kMaxDurationMs = 24u * 60u * 60u * 1000u;

bool is_timed_state(State state) {
    return state == State::PREPURGE || state == State::PREHEAT || state == State::SPINUP ||
           state == State::IGNITION_TRIAL || state == State::FLAME_CONFIRM ||
           state == State::RUN_HIGH || state == State::RUN_LOW || state == State::POST_PURGE;
}

bool is_fuel_state(State state) {
    return state == State::IGNITION_TRIAL || state == State::FLAME_CONFIRM ||
           state == State::RUN_HIGH || state == State::RUN_LOW;
}

bool is_glow_state(State state) {
    return state == State::PREHEAT || state == State::SPINUP || state == State::IGNITION_TRIAL;
}

}  // namespace

uint32_t validate_config(const Config& c, bool require_acceptance) {
    uint32_t errors = CONFIG_OK;
    if (c.schema_version != kSchemaVersion) errors |= CONFIG_SCHEMA;
    if (require_acceptance && !c.commissioning_accepted) errors |= CONFIG_NOT_ACCEPTED;

    const uint32_t durations[] = {
        c.boot_self_test_timeout_ms, c.prepurge_ms,      c.preheat_ms,
        c.spinup_ms,                 c.ignition_window_ms, c.flame_confirm_ms,
        c.postpurge_min_ms,          c.stage_min_ms,     c.restart_inhibit_ms,
        c.glow_max_ms,               c.sensor_stale_ms,  c.heartbeat_toggle_ms,
    };
    for (const uint32_t duration : durations) {
        if (duration == 0 || duration > kMaxDurationMs) errors |= CONFIG_DURATION;
    }
    const uint64_t worst_glow = static_cast<uint64_t>(c.preheat_ms) + c.spinup_ms + c.ignition_window_ms;
    if (c.glow_max_ms < worst_glow) errors |= CONFIG_GLOW_BUDGET;

    if (c.sensor_min_milli_c < -200000 || c.sensor_max_milli_c > 1800000 ||
        c.sensor_min_milli_c >= c.sensor_max_milli_c) {
        errors |= CONFIG_SENSOR_RANGE;
    }
    if (!(c.cool_exhaust_milli_c < c.hot_start_exhaust_milli_c &&
          c.hot_start_exhaust_milli_c < c.exhaust_overtemp_milli_c &&
          c.cool_air_milli_c < c.hot_start_air_milli_c &&
          c.hot_start_air_milli_c < c.air_overtemp_milli_c &&
          c.exhaust_overtemp_milli_c <= c.sensor_max_milli_c &&
          c.air_overtemp_milli_c <= c.sensor_max_milli_c)) {
        errors |= CONFIG_TEMPERATURE_ORDER;
    }
    if (c.flame_rise_milli_c <= 0 ||
        static_cast<int64_t>(c.flame_rise_milli_c) >
            static_cast<int64_t>(c.sensor_max_milli_c) - c.sensor_min_milli_c ||
        c.flame_min_exhaust_milli_c <= c.hot_start_exhaust_milli_c ||
        c.flame_hold_exhaust_milli_c <= c.hot_start_exhaust_milli_c ||
        c.flame_hold_exhaust_milli_c > c.flame_min_exhaust_milli_c ||
        c.flame_min_exhaust_milli_c >= c.exhaust_overtemp_milli_c) {
        errors |= CONFIG_FLAME_CRITERIA;
    }
    if (c.setpoint_min_milli_c < c.sensor_min_milli_c ||
        c.setpoint_max_milli_c > c.sensor_max_milli_c ||
        c.setpoint_min_milli_c >= c.setpoint_max_milli_c ||
        c.setpoint_default_milli_c < c.setpoint_min_milli_c ||
        c.setpoint_default_milli_c > c.setpoint_max_milli_c) {
        errors |= CONFIG_SETPOINT;
    }
    if (!(c.low_to_high_delta_milli_c < c.start_delta_milli_c &&
          c.start_delta_milli_c < c.high_to_low_delta_milli_c &&
          c.high_to_low_delta_milli_c < c.stop_delta_milli_c)) {
        errors |= CONFIG_REGULATOR_ORDER;
    }
    if (c.low_pwm_permille == 0 || c.low_pwm_permille >= 1000) errors |= CONFIG_PWM;
    return errors;
}

Machine::Machine(const Config& config, uint32_t startup_faults)
    : config_(config), faults_(startup_faults) {}

uint32_t Machine::elapsed(uint32_t now, uint32_t since) {
    return now - since;
}

bool Machine::sample_ok(const SensorSample& sample, uint32_t now) const {
    return sample.quality == SensorQuality::OK &&
           sample.milli_c >= config_.sensor_min_milli_c &&
           sample.milli_c <= config_.sensor_max_milli_c &&
           elapsed(now, sample.sample_ms) <= config_.sensor_stale_ms;
}

uint32_t Machine::current_input_faults(const Inputs& in) const {
    uint32_t faults = FAULT_NONE;
    if (validate_config(config_) != CONFIG_OK ||
        in.setpoint_milli_c < config_.setpoint_min_milli_c ||
        in.setpoint_milli_c > config_.setpoint_max_milli_c) {
        faults |= FAULT_CONFIG;
    }
    if (!in.safety_ok) faults |= FAULT_SAFETY_OPEN;
    if (!in.local_ui_ok) faults |= FAULT_LOCAL_UI;
    if (!sample_ok(in.exhaust, in.now_ms)) faults |= FAULT_EXHAUST_SENSOR;
    if (!sample_ok(in.hot_air, in.now_ms)) faults |= FAULT_AIR_SENSOR;
    if (!sample_ok(in.room, in.now_ms)) faults |= FAULT_ROOM_SENSOR;
    if (sample_ok(in.exhaust, in.now_ms) &&
        in.exhaust.milli_c >= config_.exhaust_overtemp_milli_c) {
        faults |= FAULT_EXHAUST_OVERTEMP;
    }
    if (sample_ok(in.hot_air, in.now_ms) && in.hot_air.milli_c >= config_.air_overtemp_milli_c) {
        faults |= FAULT_AIR_OVERTEMP;
    }
    return faults;
}

bool Machine::cold(const Inputs& in) const {
    return sample_ok(in.exhaust, in.now_ms) && sample_ok(in.hot_air, in.now_ms) &&
           in.exhaust.milli_c <= config_.cool_exhaust_milli_c &&
           in.hot_air.milli_c <= config_.cool_air_milli_c;
}

bool Machine::flame_rise_proven(const Inputs& in) const {
    if (!sample_ok(in.exhaust, in.now_ms)) return false;
    const int64_t rise_threshold = static_cast<int64_t>(ignition_baseline_milli_c_) +
                                   config_.flame_rise_milli_c;
    return static_cast<int64_t>(in.exhaust.milli_c) >=
           std::max(rise_threshold, static_cast<int64_t>(config_.flame_min_exhaust_milli_c));
}

bool Machine::state_may_need_purge(State state) const {
    return state == State::PREHEAT || state == State::SPINUP || is_fuel_state(state) ||
           state == State::POST_PURGE;
}

void Machine::transition(State state, uint32_t now) {
    state_ = state;
    state_entered_ms_ = now;
    if (state == State::PREHEAT) {
        glow_started_ms_ = now;
        glow_timer_started_ = true;
    }
    if (state == State::POST_PURGE) {
        purge_started_ms_ = now;
        purge_required_ = true;
    }
}

void Machine::trip(uint32_t fault, uint32_t now) {
    faults_ |= fault;
    if (state_ != State::LOCKOUT) {
        if (state_may_need_purge(state_)) {
            purge_required_ = true;
            purge_started_ms_ = now;
        }
        transition(State::LOCKOUT, now);
    }
}

void Machine::run_state(const Inputs& in) {
    if (is_timed_state(state_) && state_ != State::POST_PURGE &&
        (in.stop_active || !in.heat_enable)) {
        transition(State::POST_PURGE, in.now_ms);
        return;
    }

    switch (state_) {
        case State::BOOT_SELF_TEST:
            transition(State::WAIT_COOLDOWN, in.now_ms);
            break;

        case State::WAIT_COOLDOWN:
            if (restart_inhibit_ && elapsed(in.now_ms, restart_started_ms_) >= config_.restart_inhibit_ms) {
                restart_inhibit_ = false;
            }
            if (in.heat_enable && !in.stop_active && !restart_inhibit_ && cold(in) &&
                static_cast<int64_t>(in.room.milli_c) <=
                    static_cast<int64_t>(in.setpoint_milli_c) + config_.start_delta_milli_c) {
                transition(State::PREPURGE, in.now_ms);
            }
            break;

        case State::PREPURGE:
            if (elapsed(in.now_ms, state_entered_ms_) >= config_.prepurge_ms) {
                transition(State::PREHEAT, in.now_ms);
            }
            break;

        case State::PREHEAT:
            if (elapsed(in.now_ms, state_entered_ms_) >= config_.preheat_ms) {
                transition(State::SPINUP, in.now_ms);
            }
            break;

        case State::SPINUP:
            if (elapsed(in.now_ms, state_entered_ms_) >= config_.spinup_ms) {
                ignition_baseline_milli_c_ = in.exhaust.milli_c;
                transition(State::IGNITION_TRIAL, in.now_ms);
            }
            break;

        case State::IGNITION_TRIAL:
            if (flame_rise_proven(in)) {
                transition(State::FLAME_CONFIRM, in.now_ms);
            } else if (elapsed(in.now_ms, state_entered_ms_) >= config_.ignition_window_ms) {
                trip(FAULT_IGNITION_NOT_PROVEN, in.now_ms);
            }
            break;

        case State::FLAME_CONFIRM:
            if (!flame_rise_proven(in) ||
                in.exhaust.milli_c < config_.flame_hold_exhaust_milli_c) {
                trip(FAULT_IGNITION_NOT_PROVEN, in.now_ms);
            } else if (elapsed(in.now_ms, state_entered_ms_) >= config_.flame_confirm_ms) {
                transition(State::RUN_HIGH, in.now_ms);
            }
            break;

        case State::RUN_HIGH:
            if (in.exhaust.milli_c < config_.flame_hold_exhaust_milli_c) {
                trip(FAULT_FLAME_LOST, in.now_ms);
            } else if (static_cast<int64_t>(in.room.milli_c) >=
                       static_cast<int64_t>(in.setpoint_milli_c) + config_.stop_delta_milli_c) {
                transition(State::POST_PURGE, in.now_ms);
            } else if (elapsed(in.now_ms, state_entered_ms_) >= config_.stage_min_ms &&
                       static_cast<int64_t>(in.room.milli_c) >=
                           static_cast<int64_t>(in.setpoint_milli_c) +
                               config_.high_to_low_delta_milli_c) {
                transition(State::RUN_LOW, in.now_ms);
            }
            break;

        case State::RUN_LOW:
            if (in.exhaust.milli_c < config_.flame_hold_exhaust_milli_c) {
                trip(FAULT_FLAME_LOST, in.now_ms);
            } else if (static_cast<int64_t>(in.room.milli_c) >=
                       static_cast<int64_t>(in.setpoint_milli_c) + config_.stop_delta_milli_c) {
                transition(State::POST_PURGE, in.now_ms);
            } else if (elapsed(in.now_ms, state_entered_ms_) >= config_.stage_min_ms &&
                       static_cast<int64_t>(in.room.milli_c) <=
                           static_cast<int64_t>(in.setpoint_milli_c) +
                               config_.low_to_high_delta_milli_c) {
                transition(State::RUN_HIGH, in.now_ms);
            }
            break;

        case State::POST_PURGE:
            if (elapsed(in.now_ms, purge_started_ms_) >= config_.postpurge_min_ms && cold(in)) {
                purge_required_ = false;
                restart_inhibit_ = true;
                restart_started_ms_ = in.now_ms;
                transition(State::WAIT_COOLDOWN, in.now_ms);
            }
            break;

        case State::LOCKOUT:
            break;
    }
}

Outputs Machine::make_outputs(const Inputs& in) {
    Outputs out{};
    switch (state_) {
        case State::WAIT_COOLDOWN:
            out.fan_request = !cold(in);
            out.speed_permille = out.fan_request ? 1000 : 0;
            break;
        case State::PREPURGE:
            out.fan_request = true;
            out.speed_permille = 1000;
            break;
        case State::PREHEAT:
            out.glow_command = true;
            out.fan_request = true;
            // REV-A selects the fixed low winding; REV-B uses the commissioned low PWM.
            out.speed_permille = config_.revision == Revision::A ? 1 : config_.low_pwm_permille;
            break;
        case State::SPINUP:
            out.glow_command = true;
            out.fan_request = true;
            out.speed_permille = 1000;
            break;
        case State::IGNITION_TRIAL:
            out.glow_command = true;
            out.fan_request = true;
            out.fuel_command = true;
            out.fuel_permit = true;
            out.speed_permille = 1000;
            break;
        case State::FLAME_CONFIRM:
        case State::RUN_HIGH:
            out.fan_request = true;
            out.fuel_command = true;
            out.fuel_permit = true;
            out.speed_permille = 1000;
            break;
        case State::RUN_LOW:
            out.fan_request = true;
            out.fuel_command = true;
            out.fuel_permit = true;
            out.speed_permille = config_.revision == Revision::A ? 1 : config_.low_pwm_permille;
            break;
        case State::POST_PURGE:
            out.fan_request = true;
            out.speed_permille = 1000;
            break;
        case State::LOCKOUT:
            if (purge_required_ &&
                !(elapsed(in.now_ms, purge_started_ms_) >= config_.postpurge_min_ms && cold(in))) {
                out.fan_request = true;
                out.speed_permille = 1000;
            }
            out.sw_trip = true;
            break;
        case State::BOOT_SELF_TEST:
            break;
    }

    const uint32_t readiness_faults = current_input_faults(in) & ~FAULT_SAFETY_OPEN;
    const bool lockout_reset_ready = state_ == State::LOCKOUT && !in.heat_enable && !in.stop_active;
    out.arm_ready = readiness_faults == FAULT_NONE && state_ != State::BOOT_SELF_TEST &&
                    (state_ != State::LOCKOUT || lockout_reset_ready);
    if (state_ == State::LOCKOUT && reset_pending_) out.sw_trip = false;
    return out;
}

void Machine::update_heartbeat(const Inputs& in, bool healthy) {
    if (!healthy) {
        heartbeat_level_ = false;
        heartbeat_changed_ms_ = in.now_ms;
        return;
    }
    if (elapsed(in.now_ms, heartbeat_changed_ms_) >= config_.heartbeat_toggle_ms) {
        heartbeat_level_ = !heartbeat_level_;
        heartbeat_changed_ms_ = in.now_ms;
    }
}

Outputs Machine::tick(const Inputs& in) {
    if (!initialized_) {
        initialized_ = true;
        state_entered_ms_ = in.now_ms;
        heartbeat_changed_ms_ = in.now_ms;
    }

    const uint32_t input_faults = current_input_faults(in);
    const uint32_t immediate = input_faults &
        (FAULT_CONFIG | FAULT_SAFETY_OPEN | FAULT_LOCAL_UI | FAULT_EXHAUST_OVERTEMP | FAULT_AIR_OVERTEMP);
    if (immediate != FAULT_NONE) trip(immediate, in.now_ms);
    if (faults_ != FAULT_NONE && state_ != State::LOCKOUT) trip(faults_, in.now_ms);

    if (state_ == State::BOOT_SELF_TEST) {
        const uint32_t sensor_faults = input_faults &
            (FAULT_EXHAUST_SENSOR | FAULT_AIR_SENSOR | FAULT_ROOM_SENSOR);
        if (sensor_faults == FAULT_NONE) {
            run_state(in);
        } else if (elapsed(in.now_ms, state_entered_ms_) >= config_.boot_self_test_timeout_ms) {
            trip(sensor_faults | FAULT_SELF_TEST_TIMEOUT, in.now_ms);
        }
    } else if (state_ == State::LOCKOUT) {
        const uint32_t reset_blockers = input_faults & ~FAULT_SAFETY_OPEN;
        const bool reset_conditions = reset_blockers == FAULT_NONE && !in.heat_enable && !in.stop_active;
        if (!reset_pending_ && in.reset_request && reset_conditions) {
            reset_pending_ = true;
            reset_qualified_ = false;
        }
        if (reset_pending_) {
            if (!reset_conditions) {
                reset_pending_ = false;
                reset_qualified_ = false;
            } else if (in.safety_ok) {
                reset_qualified_ = true;
            }
            if (!in.reset_request) {
                if (reset_qualified_ && in.safety_ok && cold(in)) {
                    faults_ = FAULT_NONE;
                    purge_required_ = false;
                    glow_timer_started_ = false;
                    restart_inhibit_ = false;
                    reset_pending_ = false;
                    reset_qualified_ = false;
                    transition(State::WAIT_COOLDOWN, in.now_ms);
                } else {
                    reset_pending_ = false;
                    reset_qualified_ = false;
                }
            }
        }
    } else {
        const uint32_t sensor_faults = input_faults &
            (FAULT_EXHAUST_SENSOR | FAULT_AIR_SENSOR | FAULT_ROOM_SENSOR);
        if (sensor_faults != FAULT_NONE) {
            trip(sensor_faults, in.now_ms);
        } else {
            if (glow_timer_started_ && is_glow_state(state_) &&
                elapsed(in.now_ms, glow_started_ms_) >= config_.glow_max_ms) {
                trip(FAULT_GLOW_TIMEOUT, in.now_ms);
            }
            if (state_ != State::LOCKOUT) run_state(in);
        }
    }

    outputs_ = make_outputs(in);
    const bool invariant_bad =
        (outputs_.glow_command && !outputs_.fan_request) ||
        (outputs_.fuel_command &&
         (!outputs_.fuel_permit || !outputs_.fan_request || !in.safety_ok || faults_ != FAULT_NONE)) ||
        ((state_ == State::LOCKOUT || faults_ != FAULT_NONE) &&
         (outputs_.fuel_command || outputs_.fuel_permit || outputs_.glow_command));
    if (invariant_bad) {
        trip(FAULT_INTERNAL_INVARIANT, in.now_ms);
        outputs_ = make_outputs(in);
    }

    update_heartbeat(in, outputs_.arm_ready);
    outputs_.watchdog_heartbeat = heartbeat_level_;
    return outputs_;
}

Status Machine::status(uint32_t now_ms) const {
    Status result{};
    result.state = state_;
    result.faults = faults_;
    result.state_age_ms = initialized_ ? elapsed(now_ms, state_entered_ms_) : 0;
    result.ignition_baseline_milli_c = ignition_baseline_milli_c_;
    result.commissioning_ready = validate_config(config_) == CONFIG_OK;
    result.outputs = outputs_;
    return result;
}

const char* state_name(State state) {
    switch (state) {
        case State::BOOT_SELF_TEST: return "BOOT_SELF_TEST";
        case State::WAIT_COOLDOWN: return "WAIT_COOLDOWN";
        case State::PREPURGE: return "PREPURGE";
        case State::PREHEAT: return "PREHEAT";
        case State::SPINUP: return "SPINUP";
        case State::IGNITION_TRIAL: return "IGNITION_TRIAL";
        case State::FLAME_CONFIRM: return "FLAME_CONFIRM";
        case State::RUN_HIGH: return "RUN_HIGH";
        case State::RUN_LOW: return "RUN_LOW";
        case State::POST_PURGE: return "POST_PURGE";
        case State::LOCKOUT: return "LOCKOUT";
    }
    return "UNKNOWN";
}

}  // namespace sirokko
