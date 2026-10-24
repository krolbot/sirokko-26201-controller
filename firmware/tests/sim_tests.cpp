#include "sirokko/control.hpp"
#include "sirokko/button.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sirokko;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Config test_config(Revision revision = Revision::A) {
    Config c{};
    c.schema_version = 1;
    c.commissioning_accepted = true;
    c.revision = revision;
    c.boot_self_test_timeout_ms = 50;
    c.prepurge_ms = 30;
    c.preheat_ms = 40;
    c.spinup_ms = 20;
    c.ignition_window_ms = 100;
    c.flame_confirm_ms = 30;
    c.postpurge_min_ms = 40;
    c.stage_min_ms = 20;
    c.restart_inhibit_ms = 20;
    c.glow_max_ms = 200;
    c.sensor_stale_ms = 1000;
    c.heartbeat_toggle_ms = 10;
    c.sensor_min_milli_c = -50000;
    c.sensor_max_milli_c = 1000000;
    c.exhaust_overtemp_milli_c = 800000;
    c.air_overtemp_milli_c = 200000;
    c.cool_exhaust_milli_c = 50000;
    c.cool_air_milli_c = 40000;
    c.hot_start_exhaust_milli_c = 60000;
    c.hot_start_air_milli_c = 50000;
    c.flame_rise_milli_c = 20000;
    c.flame_min_exhaust_milli_c = 80000;
    c.flame_hold_exhaust_milli_c = 70000;
    c.setpoint_min_milli_c = 5000;
    c.setpoint_max_milli_c = 35000;
    c.setpoint_default_milli_c = 20000;
    c.start_delta_milli_c = -1000;
    c.high_to_low_delta_milli_c = -500;
    c.low_to_high_delta_milli_c = -1500;
    c.stop_delta_milli_c = 500;
    c.low_pwm_permille = 420;
    return c;
}

struct Sim {
    Config config;
    Machine machine;
    Inputs in{};
    Outputs out{};

    explicit Sim(Config c = test_config(), uint32_t startup_faults = 0,
                 uint32_t start_time = 0)
        : config(c), machine(config, startup_faults) {
        in.now_ms = start_time;
        in.safety_ok = true;
        in.setpoint_milli_c = config.setpoint_default_milli_c;
        in.exhaust = {20000, start_time, SensorQuality::OK};
        in.hot_air = {20000, start_time, SensorQuality::OK};
        in.room = {18000, start_time, SensorQuality::OK};
    }

    void tick(bool refresh = true) {
        if (refresh) {
            if (in.exhaust.quality == SensorQuality::OK) in.exhaust.sample_ms = in.now_ms;
            if (in.hot_air.quality == SensorQuality::OK) in.hot_air.sample_ms = in.now_ms;
            if (in.room.quality == SensorQuality::OK) in.room.sample_ms = in.now_ms;
        }
        out = machine.tick(in);
    }

    void advance(uint32_t milliseconds, bool refresh = true) {
        for (uint32_t i = 0; i < milliseconds; ++i) {
            ++in.now_ms;
            tick(refresh);
        }
    }

    State state() const { return machine.status(in.now_ms).state; }
    uint32_t faults() const { return machine.status(in.now_ms).faults; }

    void until(State target, uint32_t timeout = 1000) {
        for (uint32_t i = 0; i <= timeout; ++i) {
            tick();
            if (state() == target) return;
            ++in.now_ms;
        }
        throw std::runtime_error(std::string("state not reached: ") + state_name(target));
    }

    void boot_wait() {
        tick();
        check(state() == State::WAIT_COOLDOWN, "healthy boot must reach wait");
    }

    void start_to(State target) {
        boot_wait();
        in.heat_enable = true;
        until(target);
    }

    void reach_run_high() {
        start_to(State::IGNITION_TRIAL);
        in.exhaust.milli_c = 100000;
        until(State::FLAME_CONFIRM);
        until(State::RUN_HIGH);
        check(out.fuel_command && out.fuel_permit, "run must command both fuel channels");
        check(!out.glow_command, "glow must be off after confirmation");
    }
};

void expect_safe(const Outputs& out) {
    check(!out.fuel_command, "fuel command must be off");
    check(!out.fuel_permit, "fuel permit must be off");
    check(!out.glow_command, "glow must be off");
}

void test_config_validation() {
    Config c = test_config();
    check(validate_config(c) == CONFIG_OK, "known complete config must validate");
    c.commissioning_accepted = false;
    check(validate_config(c) & CONFIG_NOT_ACCEPTED, "unaccepted config must fail closed");
    c = test_config();
    c.preheat_ms = 0;
    check(validate_config(c) & CONFIG_DURATION, "zero duration must fail");
    c = test_config();
    c.glow_max_ms = 10;
    check(validate_config(c) & CONFIG_GLOW_BUDGET, "glow budget must cover sequence");
    c = test_config();
    c.cool_air_milli_c = c.air_overtemp_milli_c;
    check(validate_config(c) & CONFIG_TEMPERATURE_ORDER, "unsafe temperature order must fail");
    c = test_config();
    c.flame_rise_milli_c = std::numeric_limits<int32_t>::max();
    check(validate_config(c) & CONFIG_FLAME_CRITERIA,
          "unbounded flame-rise arithmetic must fail validation");
}

void test_uncommissioned_image_is_locked() {
    Config c = test_config();
    c.commissioning_accepted = false;
    Sim s(c);
    s.tick();
    check(s.state() == State::LOCKOUT, "uncommissioned image must lock out");
    check(s.faults() & FAULT_CONFIG, "config fault must be reported");
    expect_safe(s.out);
}

void test_cold_start_sequence_and_fuel_gate() {
    Sim s;
    s.boot_wait();
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::PREPURGE && s.out.fan_request, "start begins with purge");
    expect_safe(s.out);
    s.until(State::PREHEAT);
    check(s.out.glow_command && s.out.fan_request, "preheat must keep the fan running");
    check(s.out.speed_permille == 1, "Rev A preheat must select the low-speed winding");
    check(!s.out.fuel_command && !s.out.fuel_permit, "preheat has no fuel");
    for (uint32_t elapsed = 0; elapsed + 1 < s.config.preheat_ms; ++elapsed) {
        s.advance(1);
        check(s.state() == State::PREHEAT, "preheat duration must not shorten");
        check(s.out.glow_command && s.out.fan_request,
              "the forbidden startup vector G=1,M=0 must never occur during preheat");
        check(!s.out.fuel_command && !s.out.fuel_permit,
              "fuel must remain blocked for the whole preheat interval");
    }
    s.until(State::SPINUP);
    check(s.out.glow_command && s.out.fan_request && !s.out.fuel_command,
          "spinup retains glow but no fuel");
    s.until(State::IGNITION_TRIAL);
    check(s.out.glow_command && s.out.fan_request && s.out.fuel_command && s.out.fuel_permit,
          "fuel only begins after purge, preheat, and spinup");
}

void test_glow_always_implies_fan_during_startup() {
    for (Revision revision : {Revision::A, Revision::B}) {
        Sim s(test_config(revision));
        s.boot_wait();
        s.in.heat_enable = true;
        const uint32_t startup_budget = s.config.prepurge_ms + s.config.preheat_ms +
                                        s.config.spinup_ms + s.config.ignition_window_ms;
        for (uint32_t i = 0; i < startup_budget; ++i) {
            s.tick();
            check(!(s.out.glow_command && !s.out.fan_request),
                  "startup must reject forbidden G=1,M=0 output vector");
            ++s.in.now_ms;
            if (s.state() == State::IGNITION_TRIAL) break;
        }
    }
}

void test_ignition_timeout_latches() {
    Sim s;
    s.start_to(State::IGNITION_TRIAL);
    s.advance(s.config.ignition_window_ms);
    check(s.state() == State::LOCKOUT, "missing rise must lock out");
    check(s.faults() & FAULT_IGNITION_NOT_PROVEN, "ignition fault expected");
    expect_safe(s.out);
    check(s.out.fan_request, "failed ignition requires purge");
}

void test_flame_confirmation_after_glow_off() {
    Sim s;
    s.start_to(State::IGNITION_TRIAL);
    s.in.exhaust.milli_c = 100000;
    s.tick();
    check(s.state() == State::FLAME_CONFIRM, "rise must enter no-glow confirmation");
    check(!s.out.glow_command && s.out.fuel_command, "confirmation runs without glow");
    s.advance(s.config.flame_confirm_ms);
    check(s.state() == State::RUN_HIGH, "sustained rise must enter run");
}

void test_confirmation_drop_latches() {
    Sim s;
    s.start_to(State::IGNITION_TRIAL);
    s.in.exhaust.milli_c = 100000;
    s.tick();
    s.in.exhaust.milli_c = 65000;
    s.tick();
    check(s.state() == State::LOCKOUT, "drop after glow-off must lock out");
    expect_safe(s.out);
}

void test_flame_loss_latches() {
    Sim s;
    s.reach_run_high();
    s.in.exhaust.milli_c = s.config.flame_hold_exhaust_milli_c - 1;
    s.tick();
    check(s.state() == State::LOCKOUT, "flame loss must lock out");
    check(s.faults() & FAULT_FLAME_LOST, "flame loss reason expected");
    expect_safe(s.out);
}

void test_revision_a_low_speed() {
    Sim s(test_config(Revision::A));
    s.reach_run_high();
    s.in.room.milli_c = 20000;
    s.advance(s.config.stage_min_ms);
    check(s.state() == State::RUN_LOW, "room regulation must select low");
    check(s.out.speed_permille == 1, "Rev A low selector must energize");
    check(s.out.fuel_command && !s.out.glow_command, "speed change keeps fuel and no re-glow");
}

void test_revision_b_pwm() {
    Sim s(test_config(Revision::B));
    s.reach_run_high();
    s.in.room.milli_c = 20000;
    s.advance(s.config.stage_min_ms);
    check(s.state() == State::RUN_LOW, "Rev B must enter low power");
    check(s.out.speed_permille == s.config.low_pwm_permille, "Rev B must use accepted PWM");
}

void test_room_stop_and_purge() {
    Sim s;
    s.reach_run_high();
    s.in.room.milli_c = s.in.setpoint_milli_c + s.config.stop_delta_milli_c;
    s.tick();
    check(s.state() == State::POST_PURGE, "upper room boundary must stop fuel");
    expect_safe(s.out);
    check(s.out.fan_request && s.out.speed_permille == 1000, "post-purge must be full speed");
}

void test_stop_from_every_active_state() {
    const std::vector<State> targets = {
        State::PREPURGE, State::PREHEAT, State::SPINUP, State::IGNITION_TRIAL,
        State::FLAME_CONFIRM, State::RUN_HIGH, State::RUN_LOW,
    };
    for (State target : targets) {
        Sim s;
        if (target == State::FLAME_CONFIRM || target == State::RUN_HIGH || target == State::RUN_LOW) {
            s.start_to(State::IGNITION_TRIAL);
            s.in.exhaust.milli_c = 100000;
            s.until(State::FLAME_CONFIRM);
            if (target == State::RUN_HIGH || target == State::RUN_LOW) s.until(State::RUN_HIGH);
            if (target == State::RUN_LOW) {
                s.in.room.milli_c = 20000;
                s.until(State::RUN_LOW);
            }
        } else {
            s.start_to(target);
        }
        s.in.stop_active = true;
        s.tick();
        check(s.state() == State::POST_PURGE, "STOP must enter post-purge from active state");
        expect_safe(s.out);
        check(s.out.fan_request, "STOP must retain fan");
    }
}

void test_sensor_failure_matrix() {
    const std::vector<SensorQuality> failures = {
        SensorQuality::MISSING, SensorQuality::HARDWARE_FAULT, SensorQuality::CRC_ERROR,
        SensorQuality::TIMEOUT, SensorQuality::IMPLAUSIBLE, SensorQuality::POWER_ON_DEFAULT,
    };
    for (SensorQuality failure : failures) {
        for (int channel = 0; channel < 3; ++channel) {
            Sim s;
            s.reach_run_high();
            SensorSample* sample = channel == 0 ? &s.in.exhaust : channel == 1 ? &s.in.hot_air : &s.in.room;
            sample->quality = failure;
            s.tick();
            check(s.state() == State::LOCKOUT, "any sensor diagnostic must lock out");
            const uint32_t expected = channel == 0 ? FAULT_EXHAUST_SENSOR :
                                      channel == 1 ? FAULT_AIR_SENSOR : FAULT_ROOM_SENSOR;
            check(s.faults() & expected, "sensor-specific fault expected");
            expect_safe(s.out);
        }
    }
}

void test_stale_data_latches() {
    Sim s;
    s.reach_run_high();
    s.in.exhaust.sample_ms = s.in.now_ms - s.config.sensor_stale_ms - 1;
    s.tick(false);
    check(s.state() == State::LOCKOUT, "stale thermocouple data must lock out");
    check(s.faults() & FAULT_EXHAUST_SENSOR, "stale channel fault expected");
}

void test_self_test_wait_and_timeout() {
    Sim s;
    s.in.room.quality = SensorQuality::MISSING;
    s.tick();
    check(s.state() == State::BOOT_SELF_TEST, "boot may wait bounded time for first conversion");
    s.advance(s.config.boot_self_test_timeout_ms);
    check(s.state() == State::LOCKOUT, "self-test must time out");
    check(s.faults() & FAULT_SELF_TEST_TIMEOUT, "self-test reason expected");
    expect_safe(s.out);
}

void test_safety_input_fail_closed() {
    Sim s;
    s.in.safety_ok = false;
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "missing safety must lock out immediately");
    check(s.faults() & FAULT_SAFETY_OPEN, "safety reason expected");
    expect_safe(s.out);
}

void test_local_ui_failure_fails_closed() {
    Sim s;
    s.boot_wait();
    s.in.local_ui_ok = false;
    s.tick();
    check(s.state() == State::LOCKOUT, "loss of mandatory local display must lock out");
    check(s.faults() & FAULT_LOCAL_UI, "local UI fault reason expected");
    expect_safe(s.out);
}

void test_overtemperature_boundaries() {
    Sim exhaust;
    exhaust.boot_wait();
    exhaust.in.exhaust.milli_c = exhaust.config.exhaust_overtemp_milli_c;
    exhaust.tick();
    check(exhaust.state() == State::LOCKOUT, "exhaust equality is overtemperature");
    check(exhaust.faults() & FAULT_EXHAUST_OVERTEMP, "exhaust reason expected");

    Sim air;
    air.boot_wait();
    air.in.hot_air.milli_c = air.config.air_overtemp_milli_c;
    air.tick();
    check(air.state() == State::LOCKOUT, "air equality is overtemperature");
    check(air.faults() & FAULT_AIR_OVERTEMP, "air reason expected");
}

void test_hot_start_is_inhibited() {
    Sim s;
    s.in.exhaust.milli_c = 100000;
    s.in.hot_air.milli_c = 70000;
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "hot unit must not start");
    check(s.out.fan_request && !s.out.fuel_command && !s.out.glow_command,
          "hot request may cool but never fuel or glow");
}

void test_manual_reset_no_autostart() {
    Sim s(test_config(), FAULT_BROWNOUT_RESET);
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "brownout starts locked");
    s.in.reset_request = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "reset while start held must be refused");
    s.in.heat_enable = false;
    s.tick();
    check(s.state() == State::LOCKOUT && !s.out.sw_trip,
          "qualified held reset must release software trip but stay locked");
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "release after qualified reset may clear corrected fault");
    expect_safe(s.out);
    s.advance(5);
    check(s.state() == State::WAIT_COOLDOWN, "reset must not autostart");
}

void test_set_dominant_latch_reset_handshake() {
    Sim s;
    s.in.safety_ok = false;
    s.tick();
    check(s.state() == State::LOCKOUT && s.out.arm_ready && s.out.sw_trip,
          "healthy MCU side must advertise reset readiness while latch remains tripped");
    s.in.reset_request = true;
    s.tick();
    check(!s.out.sw_trip && s.state() == State::LOCKOUT,
          "held qualified reset deasserts software SET but does not clear state");
    s.in.safety_ok = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "safety restoration must be observed while reset is held");
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "release after SAFETY_OK completes reset");
    expect_safe(s.out);
}

void test_watchdog_only_when_healthy() {
    Sim s;
    s.boot_wait();
    const bool first = s.out.watchdog_heartbeat;
    s.advance(s.config.heartbeat_toggle_ms);
    check(s.out.watchdog_heartbeat != first, "healthy core must toggle heartbeat");
    s.in.exhaust.quality = SensorQuality::TIMEOUT;
    s.tick();
    check(!s.out.watchdog_heartbeat, "unhealthy core must stop heartbeat");
}

void test_purge_needs_cold_and_minimum_time() {
    Sim s;
    s.reach_run_high();
    s.in.stop_active = true;
    s.tick();
    s.in.stop_active = false;
    s.in.heat_enable = false;
    s.in.exhaust.milli_c = 100000;
    s.advance(s.config.postpurge_min_ms + 10);
    check(s.state() == State::POST_PURGE, "time alone must not finish hot purge");
    s.in.exhaust.milli_c = s.config.cool_exhaust_milli_c;
    s.in.hot_air.milli_c = s.config.cool_air_milli_c;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "cold plus minimum time finishes purge");
}

void test_restart_inhibit() {
    Sim s;
    s.reach_run_high();
    s.in.stop_active = true;
    s.tick();
    s.in.stop_active = false;
    s.in.heat_enable = false;
    s.in.exhaust.milli_c = 20000;
    s.in.hot_air.milli_c = 20000;
    s.advance(s.config.postpurge_min_ms);
    check(s.state() == State::WAIT_COOLDOWN, "purge should finish");
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "restart inhibit must block immediate start");
    s.advance(s.config.restart_inhibit_ms);
    check(s.state() == State::PREPURGE, "accepted request may start after inhibit");
}

void test_timer_wraparound() {
    Sim s(test_config(), 0, std::numeric_limits<uint32_t>::max() - 20u);
    s.boot_wait();
    s.in.heat_enable = true;
    s.tick();
    s.advance(s.config.prepurge_ms);
    check(s.state() == State::PREHEAT, "unsigned elapsed must survive wraparound");
}

void test_exact_timer_boundaries() {
    Sim s;
    s.start_to(State::PREPURGE);
    s.advance(s.config.prepurge_ms - 1);
    check(s.state() == State::PREPURGE, "must not transition before exact boundary");
    s.advance(1);
    check(s.state() == State::PREHEAT, "must transition at exact boundary");
}

void test_setpoint_out_of_range_locks() {
    Sim s;
    s.in.setpoint_milli_c = s.config.setpoint_max_milli_c + 1;
    s.tick();
    check(s.state() == State::LOCKOUT, "invalid runtime setpoint must fail closed");
    check(s.faults() & FAULT_CONFIG, "setpoint config reason expected");
}

void test_active_low_button_held_at_boot_is_ignored() {
    DebouncedActiveLowButton button(5);
    check(!button.update(false, 0), "held button at boot must not emit press");
    check(!button.pressed(), "held button is not qualified before a release");
    check(!button.update(false, 20), "continued hold must remain ignored");
    check(!button.update(true, 21), "release begins debounce");
    check(!button.update(true, 26), "release never emits press");
    check(button.release_seen(), "release after boot must arm button");
    check(!button.update(false, 27), "new press begins debounce");
    check(button.update(false, 32), "new debounced press must emit one event");
    check(button.pressed(), "qualified held press must be visible for reset handshake");
    check(!button.update(false, 40), "holding must not repeat event");
}

void test_active_low_button_rejects_bounce_and_wraps() {
    DebouncedActiveLowButton button(5);
    const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 2u;
    check(!button.update(true, near_wrap), "released boot init");
    check(!button.update(false, near_wrap + 1u), "press candidate");
    check(!button.update(true, near_wrap + 2u), "bounce cancels candidate");
    check(!button.update(false, near_wrap + 3u), "second candidate across wrap");
    check(button.update(false, 5u), "debounce elapsed must survive wrap");
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"config_validation", test_config_validation},
        {"uncommissioned_image_is_locked", test_uncommissioned_image_is_locked},
        {"cold_start_sequence_and_fuel_gate", test_cold_start_sequence_and_fuel_gate},
        {"glow_always_implies_fan_during_startup", test_glow_always_implies_fan_during_startup},
        {"ignition_timeout_latches", test_ignition_timeout_latches},
        {"flame_confirmation_after_glow_off", test_flame_confirmation_after_glow_off},
        {"confirmation_drop_latches", test_confirmation_drop_latches},
        {"flame_loss_latches", test_flame_loss_latches},
        {"revision_a_low_speed", test_revision_a_low_speed},
        {"revision_b_pwm", test_revision_b_pwm},
        {"room_stop_and_purge", test_room_stop_and_purge},
        {"stop_from_every_active_state", test_stop_from_every_active_state},
        {"sensor_failure_matrix", test_sensor_failure_matrix},
        {"stale_data_latches", test_stale_data_latches},
        {"self_test_wait_and_timeout", test_self_test_wait_and_timeout},
        {"safety_input_fail_closed", test_safety_input_fail_closed},
        {"local_ui_failure_fails_closed", test_local_ui_failure_fails_closed},
        {"overtemperature_boundaries", test_overtemperature_boundaries},
        {"hot_start_is_inhibited", test_hot_start_is_inhibited},
        {"manual_reset_no_autostart", test_manual_reset_no_autostart},
        {"set_dominant_latch_reset_handshake", test_set_dominant_latch_reset_handshake},
        {"watchdog_only_when_healthy", test_watchdog_only_when_healthy},
        {"purge_needs_cold_and_minimum_time", test_purge_needs_cold_and_minimum_time},
        {"restart_inhibit", test_restart_inhibit},
        {"timer_wraparound", test_timer_wraparound},
        {"exact_timer_boundaries", test_exact_timer_boundaries},
        {"setpoint_out_of_range_locks", test_setpoint_out_of_range_locks},
        {"active_low_button_held_at_boot_is_ignored", test_active_low_button_held_at_boot_is_ignored},
        {"active_low_button_rejects_bounce_and_wraps", test_active_low_button_rejects_bounce_and_wraps},
    };

    size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << "RESULT " << passed << "/" << tests.size() << " passed\n";
    return passed == tests.size() ? 0 : 1;
}
