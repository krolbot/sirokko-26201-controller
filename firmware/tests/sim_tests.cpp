#include "sirokko/control.hpp"
#include "sirokko/button.hpp"
#include "board.hpp"
#include "local_display.hpp"
#include "output_driver.hpp"
#include "commissioning.hpp"
#include "idf_stubs.hpp"
#include <algorithm>
#include <cstring>

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

Config test_config() {
    Config c{};
    c.schema_version = kConfigSchemaVersion;
    c.profile_revision = 1;
    c.run_low_qualified = true;
    c.commissioning_accepted = true;
    c.boot_self_test_timeout_ms = 50;
    c.recovery_purge_min_ms = 25;
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
    c.pwm_frequency_hz = 10000;
    c.recovery_purge_pwm_permille = 650;
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
        check(state() == State::RECOVERY_PURGE, "every boot must enter recovery purge");
        check(!out.fuel_command && !out.fuel_permit && !out.glow_command,
              "recovery purge must keep fuel and glow off");
        check(out.fan_request && out.speed_permille == config.recovery_purge_pwm_permille,
              "valid recovery profile must command configured PWM purge");
        advance(config.recovery_purge_min_ms);
        check(state() == State::WAIT_COOLDOWN, "cold completed recovery must reach wait");
        check(machine.status(in.now_ms).recovery_complete, "completed recovery must be explicit");
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

struct HardwareSafetyLatch {
    bool power_good{true};
    bool watchdog_ok{true};
    bool stop_ok{true};
    bool thermal_ok{true};
    bool allow_latch{};

    bool clear_ok(const Outputs& out) const {
        return power_good && watchdog_ok && stop_ok && thermal_ok && out.arm_ready && !out.sw_trip;
    }

    void settle(const Outputs& out) {
        if (!clear_ok(out)) allow_latch = false;
    }

    void reset_rising_edge(const Outputs& out, bool start_released = true) {
        settle(out);
        if (clear_ok(out)) {
            allow_latch = out.arm_ready && !out.fuel_command && !out.fuel_permit &&
                          !out.glow_command && start_released;
        }
    }

    bool safety_ok(const Outputs& out) {
        settle(out);
        return allow_latch && clear_ok(out);
    }
};

void coupled_tick(Sim& sim, HardwareSafetyLatch& latch) {
    sim.in.safety_ok = latch.safety_ok(sim.out);
    sim.tick();
    latch.settle(sim.out);
}

void expect_safe(const Outputs& out) {
    check(!out.fuel_command, "fuel command must be off");
    check(!out.fuel_permit, "fuel permit must be off");
    check(!out.glow_command, "glow must be off");
}

void drive_to_state(Sim& s, State target) {
    switch (target) {
        case State::BOOT_SELF_TEST:
            break;
        case State::RECOVERY_PURGE:
            s.tick();
            break;
        case State::WAIT_COOLDOWN:
            s.boot_wait();
            break;
        case State::PREPURGE:
        case State::PREHEAT:
        case State::SPINUP:
        case State::IGNITION_TRIAL:
            s.start_to(target);
            break;
        case State::FLAME_CONFIRM:
            s.start_to(State::IGNITION_TRIAL);
            s.in.exhaust.milli_c = 100000;
            s.until(State::FLAME_CONFIRM);
            break;
        case State::RUN_HIGH:
            s.reach_run_high();
            break;
        case State::RUN_LOW:
            s.reach_run_high();
            s.in.room.milli_c = 20000;
            s.advance(s.config.stage_min_ms);
            break;
        case State::POST_PURGE:
            s.reach_run_high();
            s.in.stop_active = true;
            s.tick();
            break;
        case State::LOCKOUT:
            s.boot_wait();
            s.in.exhaust.quality = SensorQuality::TIMEOUT;
            s.tick();
            break;
    }
    check(s.state() == target, "test setup must reach requested source state");
}

void test_config_validation() {
    Config c = test_config();
    check(validate_config(c) == CONFIG_OK, "known complete config must validate");
    c.schema_version = 1;
    check(validate_config(c) & CONFIG_SCHEMA,
          "legacy schema, including the removed two-speed profile, must be rejected");
    c = test_config();
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
    check(heartbeat_worst_case_falling_edge_ms(20) == 80,
          "heartbeat budget must include two toggles, 20 ms loop quantization and jitter");
    c = test_config();
    c.heartbeat_toggle_ms = 21;
    check(validate_config(c) & CONFIG_WATCHDOG_TIMING,
          "profile exceeding the 100 ms falling-edge budget must fail");
    c.heartbeat_toggle_ms = 100;
    check(validate_config(c) & CONFIG_WATCHDOG_TIMING,
          "profile slower than the 170 ms watchdog minimum must fail");
    c = test_config();
    c.recovery_purge_pwm_permille = 0;
    check(validate_config(c) & CONFIG_PWM, "recovery purge requires an explicit PWM duty");
    c = test_config();
    c.pwm_frequency_hz = kPwmMaximumFrequencyHz + 1;
    check(validate_config(c) & CONFIG_PWM, "unsupported PWM frequency must fail closed");
}

void test_uncommissioned_image_is_locked() {
    Config c = test_config();
    c.commissioning_accepted = false;
    Sim s(c);
    s.tick();
    check(s.state() == State::RECOVERY_PURGE,
          "uncommissioned image must remain explicitly blocked in recovery");
    check(s.faults() & FAULT_CONFIG, "config fault must be reported");
    expect_safe(s.out);
    check(!s.out.fan_request && !s.machine.status(s.in.now_ms).recovery_complete,
          "invalid config cannot fabricate a verified purge command or completion");
}

void test_recovery_purge_after_power_loss_from_every_state() {
    const State source_states[] = {
        State::BOOT_SELF_TEST, State::RECOVERY_PURGE, State::WAIT_COOLDOWN,
        State::PREPURGE,       State::PREHEAT,       State::SPINUP,
        State::IGNITION_TRIAL, State::FLAME_CONFIRM, State::RUN_HIGH,
        State::RUN_LOW,        State::POST_PURGE,    State::LOCKOUT,
    };

    for (const State source_state : source_states) {
        Sim source;
        drive_to_state(source, source_state);
        Sim restarted(source.config, source.faults());
        restarted.in.heat_enable = true;
        restarted.in.reset_request = true;
        restarted.tick();
        check(restarted.state() == State::RECOVERY_PURGE,
              "power loss from every source state must restart in recovery purge");
        expect_safe(restarted.out);
        check(restarted.out.fan_request &&
                  restarted.out.speed_permille == restarted.config.recovery_purge_pwm_permille,
              "recovery purge must use the commissioned PWM-only output");
        check(restarted.machine.status(restarted.in.now_ms).state_age_ms == 0 &&
                  !restarted.machine.status(restarted.in.now_ms).recovery_complete,
              "reboot must restart purge timing and completion state");
        restarted.advance(restarted.config.recovery_purge_min_ms - 1);
        check(restarted.state() == State::RECOVERY_PURGE,
              "START and RESET cannot bypass the minimum recovery time");
        restarted.advance(1);
        check(restarted.state() == (source.faults() == 0 ? State::WAIT_COOLDOWN : State::LOCKOUT),
              "completed cold purge must route clean boots to WAIT and latched faults to LOCKOUT");
        expect_safe(restarted.out);
    }
}

void test_repeated_power_loss_restarts_recovery_timer() {
    Sim first;
    first.tick();
    first.advance(first.config.recovery_purge_min_ms / 2);
    check(first.state() == State::RECOVERY_PURGE,
          "first partial recovery must still be in progress");

    Sim second(first.config, first.faults());
    second.tick();
    check(second.machine.status(second.in.now_ms).state_age_ms == 0,
          "a second power loss must not retain elapsed purge time");
    second.advance(second.config.recovery_purge_min_ms - 1);
    check(second.state() == State::RECOVERY_PURGE,
          "second boot must satisfy its own complete purge minimum");
    second.advance(1);
    check(second.state() == State::WAIT_COOLDOWN, "second complete cold purge may finish");
}

void test_recovery_blockers_and_request_edges() {
    Sim s;
    s.in.stop_active = true;
    s.in.heat_enable = true;
    s.in.reset_request = true;
    s.tick();
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::RECOVERY_PURGE && !s.machine.status(s.in.now_ms).recovery_complete,
          "STOP must block recovery completion without disabling fuel-safe purge");
    expect_safe(s.out);
    check(s.out.fan_request, "STOP during recovery must not suppress purge airflow");

    s.in.stop_active = false;
    s.in.exhaust.quality = SensorQuality::TIMEOUT;
    s.advance(s.config.boot_self_test_timeout_ms);
    check(s.state() == State::RECOVERY_PURGE && !s.machine.status(s.in.now_ms).recovery_complete,
          "sensor error must not be reported as completed cooling");
    s.in.exhaust.quality = SensorQuality::OK;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "restored sensing may finish purge but the sensor fault remains latched");
    expect_safe(s.out);
}

void test_latched_fault_survives_reboot_until_qualified_reset() {
    Sim failed;
    failed.boot_wait();
    failed.in.exhaust.quality = SensorQuality::TIMEOUT;
    failed.tick();
    const uint32_t persisted_faults = failed.faults();
    check(persisted_faults & FAULT_EXHAUST_SENSOR, "source fault must latch");

    Sim restarted(test_config(), persisted_faults);
    restarted.tick();
    restarted.advance(restarted.config.recovery_purge_min_ms);
    check(restarted.state() == State::LOCKOUT &&
              (restarted.faults() & FAULT_EXHAUST_SENSOR),
          "persisted fault must survive a power cycle");
    restarted.in.reset_request = true;
    restarted.tick();
    restarted.in.reset_request = false;
    restarted.tick();
    check(restarted.state() == State::WAIT_COOLDOWN && restarted.faults() == FAULT_NONE,
          "only a qualified RESET may clear the persisted logical fault");
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
    check(s.out.speed_permille == s.config.low_pwm_permille,
          "preheat must use the commissioned PWM duty");
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
    Sim s;
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

void test_pwm_low_speed() {
    Sim s;
    s.reach_run_high();
    s.in.room.milli_c = 20000;
    s.advance(s.config.stage_min_ms);
    check(s.state() == State::RUN_LOW, "PWM profile must enter low power");
    check(s.out.speed_permille == s.config.low_pwm_permille, "low power must use accepted PWM");
    check(s.out.fuel_command && !s.out.glow_command, "speed change keeps fuel and no re-glow");
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
    check(s.state() == State::RECOVERY_PURGE,
          "recovery may wait bounded time for the first complete sensor set");
    s.advance(s.config.boot_self_test_timeout_ms);
    check(s.state() == State::RECOVERY_PURGE,
          "invalid sensors must not be reported as a completed recovery purge");
    check(s.faults() & FAULT_SELF_TEST_TIMEOUT, "self-test reason expected");
    expect_safe(s.out);
    check(s.out.fan_request && !s.machine.status(s.in.now_ms).recovery_complete,
          "sensor failure keeps fuel blocked and purge requested");
    s.in.room.quality = SensorQuality::OK;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "restored sensors may finish cooling but the latched sensor fault still requires RESET");
}

void test_safety_input_fail_closed() {
    Sim s;
    s.in.safety_ok = false;
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::RECOVERY_PURGE && s.out.fan_request,
          "open fuel latch must not prevent recovery purge");
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::LOCKOUT, "missing safety must require manual reset after purge");
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

void test_detected_display_init_failure_preserves_recovery_purge() {
    Sim s(test_config(), local_display_startup_fault(false));
    s.tick();
    check(s.state() == State::RECOVERY_PURGE,
          "detected display init failure must not skip boot recovery purge");
    check(s.faults() & FAULT_LOCAL_UI, "display init failure must latch the local UI fault");
    expect_safe(s.out);
    check(s.out.fan_request && s.out.speed_permille == s.config.recovery_purge_pwm_permille,
          "display init failure must not suppress the required fuel-safe purge");
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::LOCKOUT,
          "detected display init failure must lock out after recovery completes");
    expect_safe(s.out);
}

void test_display_transfer_failure_during_recovery_preserves_purge() {
    Sim s;
    s.tick();
    s.in.local_ui_ok = false;
    s.tick();
    check(s.state() == State::RECOVERY_PURGE,
          "display transfer failure during recovery must not interrupt purge");
    check(s.faults() & FAULT_LOCAL_UI, "display transfer failure must latch the local UI fault");
    expect_safe(s.out);
    check(s.out.fan_request && s.out.speed_permille == s.config.recovery_purge_pwm_permille,
          "display transfer failure must leave the commissioned purge airflow active");
}

void test_shared_spi_display_contract() {
    check(board::kSpiMosi == 11 && board::kSpiSclk == 12 && board::kSpiMiso == 13,
          "shared SPI pins must remain on the released sensor bus");
    check(board::kTftCs == 47 && board::kTftDc == 48 && board::kTftReset == -1,
          "TFT control pins and EN-bound reset contract must remain exact");
    check(board::shared_spi_chip_selects_are_distinct(),
          "TFT and both MAX31856 devices require independent chip selects");
    check(board::kMaxSpiMode == 1 && board::kTftSpiMode == 0,
          "each shared-bus device must retain its own SPI mode");
    check(board::kMaxSpiClockHz == 5'000'000 && board::kTftSpiClockHz == 20'000'000,
          "shared-bus clocks must remain at the selected conservative values");
    check(board::kTftBandTransferBudgetUs < kControlLoopPeriodMs * 1000u,
          "one bounded TFT band must fit inside a control-loop period");
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
    check(s.state() == State::RECOVERY_PURGE, "hot unit must remain in recovery purge");
    check(s.out.fan_request && !s.out.fuel_command && !s.out.glow_command,
          "hot recovery may cool but never fuel or glow");
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::RECOVERY_PURGE, "minimum time alone cannot finish hot recovery");
    s.in.exhaust.milli_c = 20000;
    s.in.hot_air.milli_c = 20000;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "cold plus minimum time completes recovery");
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN,
          "a request held through recovery must not auto-start");
    s.in.heat_enable = false;
    s.tick();
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::PREPURGE, "release and new START may begin a new cycle");
}

void test_manual_reset_no_autostart() {
    Sim s(test_config(), FAULT_BROWNOUT_RESET);
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::RECOVERY_PURGE, "brownout must purge before lockout handling");
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::LOCKOUT, "latched brownout requires reset after purge");
    s.in.reset_request = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "reset while start held must be refused");
    s.in.heat_enable = false;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "releasing START during a held RESET must not synthesize a reset press");
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "release after a RESET press blocked by START must not clear lockout");
    s.in.reset_request = true;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "a separate RESET press after START release may qualify");
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN,
          "release after the separate qualified RESET press may clear corrected fault");
    expect_safe(s.out);
    s.advance(5);
    check(s.state() == State::WAIT_COOLDOWN, "reset must not autostart");
}

void test_set_dominant_latch_reset_handshake() {
    Sim s;
    s.in.safety_ok = false;
    s.tick();
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::LOCKOUT && s.out.arm_ready && !s.out.sw_trip,
          "healthy MCU side must pre-arm the physical RESET interface while latch remains tripped");
    s.in.reset_request = true;
    s.tick();
    check(!s.out.sw_trip && s.state() == State::LOCKOUT,
          "held qualified reset keeps software SET released but does not clear state");
    s.in.safety_ok = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "safety restoration must be observed while reset is held");
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN, "release after SAFETY_OK completes reset");
    expect_safe(s.out);
}

void test_reset_release_cannot_qualify_safety() {
    Sim s(test_config(), FAULT_BROWNOUT_RESET);
    s.in.safety_ok = false;
    s.tick();
    s.advance(s.config.recovery_purge_min_ms);

    s.in.reset_request = true;
    s.tick();
    check(s.state() == State::LOCKOUT, "RESET press with open safety starts but cannot qualify reset");

    s.in.reset_request = false;
    s.in.safety_ok = true;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "SAFETY_OK first observed on RESET release must not clear lockout");

    s.in.reset_request = true;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "new RESET hold must observe SAFETY_OK before release");
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN,
          "release after SAFETY_OK was observed while held must clear lockout");
}

void test_reset_blocker_cancels_qualification() {
    Sim s(test_config(), FAULT_BROWNOUT_RESET);
    s.tick();
    s.advance(s.config.recovery_purge_min_ms);

    s.in.reset_request = true;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "healthy held RESET may qualify but must remain locked until release");

    s.in.local_ui_ok = false;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "a current blocker must cancel the qualified RESET cycle");
    s.in.local_ui_ok = true;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "removing a blocker while RESET remains held must not requalify without a new press");

    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::LOCKOUT,
          "release of the cancelled RESET cycle must not clear lockout");
    s.in.reset_request = true;
    s.tick();
    s.in.reset_request = false;
    s.tick();
    check(s.state() == State::WAIT_COOLDOWN,
          "a new qualified RESET cycle after blocker recovery must clear lockout");
}

void test_coupled_hardware_latch_reset_protocol() {
    Sim s;
    HardwareSafetyLatch latch;
    coupled_tick(s, latch);
    check(s.state() == State::RECOVERY_PURGE && s.out.fan_request,
          "open hardware latch must not suppress the reset-recovery purge");
    for (uint32_t i = 0; i < s.config.recovery_purge_min_ms; ++i) {
        ++s.in.now_ms;
        coupled_tick(s, latch);
    }
    check(s.state() == State::LOCKOUT, "open hardware latch must lock after recovery purge");
    expect_safe(s.out);
    check(s.out.arm_ready && !s.out.sw_trip,
          "software trip must release before the asynchronous physical RESET edge");

    latch.reset_rising_edge(s.out);
    coupled_tick(s, latch);
    check(latch.allow_latch && s.in.safety_ok,
          "qualified hardware RESET must restore SAFETY_OK before firmware RESET");
    s.in.reset_request = true;
    coupled_tick(s, latch);
    check(s.state() == State::LOCKOUT,
          "firmware RESET must remain locked while the request is held");
    s.in.reset_request = false;
    coupled_tick(s, latch);
    check(s.state() == State::WAIT_COOLDOWN, "RESET release must complete the reset handshake");
    expect_safe(s.out);

    latch.thermal_ok = false;
    latch.reset_rising_edge(s.out);
    coupled_tick(s, latch);
    check(!latch.allow_latch && s.state() == State::LOCKOUT,
          "a simultaneous fault must dominate a RESET edge");
    expect_safe(s.out);

    Sim held;
    HardwareSafetyLatch held_latch;
    held.in.reset_request = true;
    coupled_tick(held, held_latch);
    for (uint32_t i = 0; i < held.config.recovery_purge_min_ms; ++i) {
        ++held.in.now_ms;
        coupled_tick(held, held_latch);
    }
    check(held.state() == State::LOCKOUT, "boot-held RESET still reaches post-recovery lockout");
    check(held.out.arm_ready && !held.out.sw_trip,
          "firmware may pre-arm only its half of the edge-qualified reset path");
    check(!held_latch.allow_latch, "boot-held RESET must not synthesize a hardware reset edge");
    held.in.reset_request = false;
    coupled_tick(held, held_latch);
    check(held.state() == State::LOCKOUT,
          "releasing a boot-held RESET must not synthesize a reset cycle");
    held_latch.reset_rising_edge(held.out);
    coupled_tick(held, held_latch);
    check(held.in.safety_ok, "new hardware RESET after release may restore SAFETY_OK");
    held.in.reset_request = true;
    coupled_tick(held, held_latch);
    held.in.reset_request = false;
    coupled_tick(held, held_latch);
    check(held.state() == State::WAIT_COOLDOWN,
          "a new firmware RESET press after release must complete exactly one reset cycle");
}

void test_coupled_hardware_latch_debounced_reset_protocol() {
    Sim s;
    HardwareSafetyLatch latch;
    DebouncedActiveLowButton reset_button(5);
    reset_button.update(true, s.in.now_ms);
    coupled_tick(s, latch);
    for (uint32_t i = 0; i < s.config.recovery_purge_min_ms; ++i) {
        ++s.in.now_ms;
        coupled_tick(s, latch);
    }
    check(s.state() == State::LOCKOUT && s.out.arm_ready && !s.out.sw_trip,
          "completed recovery must pre-arm only the MCU half of physical RESET");
    const uint32_t reset_start = s.in.now_ms;
    s.in.now_ms = reset_start + 1;
    latch.reset_rising_edge(s.out);
    reset_button.update(false, s.in.now_ms);
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    check(latch.allow_latch && !s.in.reset_request,
          "hardware RESET must precede the debounced firmware request");
    s.in.now_ms = reset_start + 2;
    reset_button.update(true, s.in.now_ms);
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    s.in.now_ms = reset_start + 3;
    latch.reset_rising_edge(s.out);
    reset_button.update(false, s.in.now_ms);
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    s.in.now_ms = reset_start + 8;
    check(reset_button.update(false, s.in.now_ms), "debounced RESET press must be observed once");
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    check(s.state() == State::LOCKOUT && s.in.safety_ok,
          "firmware must observe restored SAFETY_OK while RESET is held");
    s.in.now_ms = reset_start + 12;
    reset_button.update(false, s.in.now_ms);
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    s.in.now_ms = reset_start + 13;
    reset_button.update(true, s.in.now_ms);
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    s.in.now_ms = reset_start + 18;
    reset_button.update(true, s.in.now_ms);
    s.in.reset_request = reset_button.pressed();
    coupled_tick(s, latch);
    check(s.state() == State::WAIT_COOLDOWN, "debounced RESET release must complete the handshake");
    expect_safe(s.out);
    latch.watchdog_ok = false;
    coupled_tick(s, latch);
    check(s.state() == State::LOCKOUT && !latch.allow_latch,
          "watchdog clear must dominate the armed latch");
    latch.reset_rising_edge(s.out);
    check(!latch.allow_latch, "RESET must not override an active watchdog clear");

    Sim held;
    HardwareSafetyLatch held_latch;
    DebouncedActiveLowButton held_button(5);
    held_button.update(false, held.in.now_ms);
    held.in.reset_request = held_button.pressed();
    coupled_tick(held, held_latch);
    for (uint32_t i = 0; i < held.config.recovery_purge_min_ms; ++i) {
        ++held.in.now_ms;
        held_button.update(false, held.in.now_ms);
        held.in.reset_request = held_button.pressed();
        coupled_tick(held, held_latch);
    }
    check(held.state() == State::LOCKOUT && !held_latch.allow_latch && !held.in.reset_request,
          "RESET held through boot/recovery must not arm either reset path");
    const uint32_t held_start = held.in.now_ms;
    for (const uint32_t offset : {1u, 6u, 7u, 12u, 13u, 18u}) {
        held.in.now_ms = held_start + offset;
        const bool released = offset <= 6 || offset >= 13;
        if (offset == 7) held_latch.reset_rising_edge(held.out);
        held_button.update(released, held.in.now_ms);
        held.in.reset_request = held_button.pressed();
        coupled_tick(held, held_latch);
        if (offset == 6) {
            check(held.state() == State::LOCKOUT && !held_latch.allow_latch,
                  "releasing a boot-held RESET must not synthesize a hardware edge");
        }
    }
    check(held.state() == State::WAIT_COOLDOWN,
          "a new debounced press after release must complete exactly one reset cycle");
    expect_safe(held.out);
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

void test_watchdog_falling_edges_with_loop_jitter() {
    Config config = test_config();
    config.heartbeat_toggle_ms = 20;
    Sim s(config);
    s.boot_wait();

    bool previous = s.out.watchdog_heartbeat;
    uint32_t previous_falling_ms = s.in.now_ms;
    uint32_t maximum_interval_ms = 0;
    uint32_t falling_edges = 0;
    for (int i = 0; i < 8; ++i) {
        s.in.now_ms += kControlLoopPeriodMs + kControlLoopJitterBudgetMs;
        s.tick();
        if (previous && !s.out.watchdog_heartbeat) {
            const uint32_t interval = s.in.now_ms - previous_falling_ms;
            if (falling_edges > 0 && interval > maximum_interval_ms) maximum_interval_ms = interval;
            previous_falling_ms = s.in.now_ms;
            ++falling_edges;
        }
        previous = s.out.watchdog_heartbeat;
    }

    check(falling_edges >= 2, "jitter trace must contain repeated watchdog falling edges");
    check(maximum_interval_ms == heartbeat_worst_case_falling_edge_ms(config.heartbeat_toggle_ms),
          "trace must reach the calculated worst-case falling-edge interval");
    check(maximum_interval_ms <= kHeartbeatMaximumFallingEdgeMs &&
              maximum_interval_ms < kWatchdogMinimumTimeoutMs,
          "worst-case heartbeat trace must remain inside both watchdog limits");
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
    check(s.state() == State::WAIT_COOLDOWN, "blocked START must not become a delayed automatic restart");
    s.in.heat_enable = false;
    s.tick();
    s.in.heat_enable = true;
    s.tick();
    check(s.state() == State::PREPURGE, "new manual START after inhibit may start");
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
    check(s.state() == State::RECOVERY_PURGE,
          "invalid startup setpoint must remain fuel-safe during recovery");
    s.advance(s.config.recovery_purge_min_ms);
    check(s.state() == State::LOCKOUT, "invalid runtime setpoint must fail closed after purge");
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

void test_pwm_hardware_boundaries() {
    for (uint32_t hz : {0u, 1u, 76u, 77u, 20000u, 20001u, 78125u}) {
        Config c = test_config();
        c.pwm_frequency_hz = hz;
        const bool expected = hz == 77 || hz == 20000;
        check(((validate_config(c) & CONFIG_PWM) == 0) == expected, "frequency boundary");
    }
    for (uint16_t duty : {0, 1, 800, 801, 999, 1000, 1001}) {
        Config c = test_config();
        c.low_pwm_permille = duty;
        check(((validate_config(c) & CONFIG_PWM) == 0) == (duty == 1 || duty == 800), "low boundary");
        c = test_config();
        c.recovery_purge_pwm_permille = duty;
        check(((validate_config(c) & CONFIG_PWM) == 0) == (duty == 1 || duty == 800 || duty == 1000), "recovery boundary");
        check(speed_supported(duty) == (duty <= 800 || duty == 1000), "output boundary incl OFF");
    }
}

void check_physical_off() {
    check(!fake_idf::connected || (!fake_idf::running && fake_idf::idle == 0), "GPIO17 must not retain PWM/HIGH");
    for (int pin : {14, 15, 16, 18, 21, 41}) check(fake_idf::levels[pin] == 0, "dangerous outputs off");
    check(fake_idf::levels[42] == 1, "software trip asserted");
}

Outputs commanded_speed(uint16_t duty) {
    Outputs out{};
    out.speed_permille = duty;
    out.fan_request = duty != 0;
    return out;
}

void test_real_adapter_off_pwm_dc_pwm_off() {
    fake_idf::reset();
    OutputDriver driver;
    check(driver.initialize(test_config()), "adapter init");
    for (uint16_t duty : {0, 1, 800, 1000, 1000, 1, 800, 0}) {
        fake_idf::trace.clear();
        check(driver.apply(commanded_speed(duty)), "transition must succeed");
        check(driver.applied_speed() == duty, "requested versus applied speed");
        if (duty == 0 || duty == 1000) {
            check(!fake_idf::running && fake_idf::idle == (duty == 1000 ? 1u : 0u), "static is not a PWM counter maximum");
            check(std::find(fake_idf::trace.begin(), fake_idf::trace.end(), "update") == fake_idf::trace.end(), "no periodic restart in DC");
        } else {
            const uint32_t expected = (duty * 1024u + 500u) / 1000u;
            check(fake_idf::running && fake_idf::active == expected, "1024 physical ticks per period, rounded duty");
            check(fake_idf::active < 1024, "no counter overflow");
        }
    }
}

void test_real_adapter_rejects_unqualified_commands() {
    for (uint16_t duty : {801, 999, 1001}) {
        fake_idf::reset();
        OutputDriver driver;
        check(driver.initialize(test_config()), "init");
        check(driver.apply(commanded_speed(1000)), "precondition DC");
        check(!driver.apply(commanded_speed(duty)), "no output-layer bypass");
        check_physical_off();
        check(!driver.apply(commanded_speed(1000)), "failure latched, no stale retry");
        check_physical_off();
    }
}

void test_real_adapter_initialization_errors() {
    for (const char* call : {"gpio_config", "gpio_level:14:0", "timer", "channel", "frequency", "stop:0"}) {
        fake_idf::reset();
        fake_idf::failing_call = call;
        OutputDriver driver;
        check(!driver.initialize(test_config()), "injected initialization must fail");
        check(!driver.healthy(), "failure latched");
        check_physical_off();
        check(!driver.initialize(test_config()), "no implicit reinitialization after fault");
        check(!driver.apply(commanded_speed(1000)), "no stale command after init failure");
        check_physical_off();
    }
}

void test_real_adapter_runtime_errors() {
    for (uint16_t source : {420, 1000}) {
        for (const char* call : {"stop:1", "stop:0", "duty:819", "update", "gpio_level:14:1", "gpio_level:21:1"}) {
            fake_idf::reset();
            OutputDriver driver;
            check(driver.initialize(test_config()), "init");
            check(driver.apply(commanded_speed(source)), "source");
            fake_idf::failing_call = call;
            uint16_t target = std::string(call) == "stop:1" ? 1000 : std::string(call) == "stop:0" ? 0 : 800;
            // Ensure stop:1 is actually issued when starting in DC.
            if (source == 1000 && target == 1000) {
                fake_idf::failing_call.clear();
                check(driver.apply(commanded_speed(420)), "transition setup");
                fake_idf::failing_call = call;
            }
            Outputs out = commanded_speed(target);
            out.glow_command = out.fuel_command = out.fuel_permit = out.arm_ready = true;
            check(!driver.apply(out), "injected runtime failure must propagate");
            check_physical_off();
            check(!driver.healthy() && driver.applied_speed() == 0, "fault status versus requested command");
            check(!driver.apply(out), "repeated command cannot restore HIGH or PWM");
            check_physical_off();
        }
    }
}

void test_real_adapter_stop_failure_detaches_gpio() {
    fake_idf::reset();
    OutputDriver driver;
    check(driver.initialize(test_config()), "init");
    check(driver.apply(commanded_speed(1000)), "source DC");
    fake_idf::failing_call = "stop:0";
    fake_idf::persistent_failure = true;
    check(!driver.apply(commanded_speed(0)), "stop failure");
    check(!fake_idf::connected && fake_idf::levels[17] == 0, "GPIO disconnect overrides persistent LEDC stop failure");
    check(!driver.apply(commanded_speed(800)), "repeated failure stays inhibited");
    check_physical_off();
}

void test_cli_store_validation_and_token_bypass() {
    fake_idf::reset();
    CommissioningStore store;
    Config c = test_config();
    for (const char* value : {"0", "1", "76", "20001", "78125", "4294967296"}) {
        check(!store.set_field(c, "pwm_frequency_hz", value), "CLI rejects unsupported frequency");
    }
    for (const char* value : {"0", "801", "999", "1001", "65537", "-1"}) {
        check(!store.set_field(c, "recovery_purge_pwm_permille", value), "CLI rejects recovery bypass");
        check(!store.set_field(c, "low_pwm_permille", value), "CLI rejects low bypass");
    }
    check(!store.set_field(c, "low_pwm_permille", "1000"), "low cannot be static ON");
    check(store.set_field(c, "recovery_purge_pwm_permille", "1000"), "recovery may be true DC");
    check(!c.commissioning_accepted, "edit revokes acceptance");
    check(store.accept_config(c, store.candidate_token(c)), "valid candidate accepted");
    const uint32_t stale = store.candidate_token(c);
    check(store.set_field(c, "pwm_frequency_hz", "20000"), "upper boundary");
    check(!store.accept_config(c, stale), "old token cannot commit changed profile");
    c.low_pwm_permille = 999;
    check(!store.accept_config(c, store.candidate_token(c)), "matching token cannot bypass validator");
    c.commissioning_accepted = true;
    check(!store.save_config(c), "direct accepted NVS write cannot bypass validator");
}

void test_legacy_accepted_nvs_out_of_range() {
    for (int field : {0, 1, 2, 3}) {
        fake_idf::reset();
        CommissioningStore store;
        // Construct the actual old schema-2 blob with a valid legacy digest, not a core mock.
        struct Stored { uint32_t magic; Config config; uint32_t digest; } stored{};
        stored.magic = 0x5349524f;
        stored.config = test_config();
        if (field == 0) stored.config.pwm_frequency_hz = 78125;
        if (field == 1) stored.config.low_pwm_permille = 801;
        if (field == 2) stored.config.recovery_purge_pwm_permille = 999;
        if (field == 3) stored.config.schema_version = 1;
        uint32_t hash = 2166136261u;
        auto feed = [&](const void* ptr, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(ptr);
            for (size_t i=0; i<size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
        };
        feed(&stored.config.schema_version, sizeof(stored.config.schema_version));
        feed(&stored.config.commissioning_accepted, sizeof(stored.config.commissioning_accepted));
        feed(&stored.config.boot_self_test_timeout_ms,
             offsetof(Config, low_pwm_permille) + sizeof(stored.config.low_pwm_permille) - offsetof(Config, boot_self_test_timeout_ms));
        feed(&stored.config.profile_revision, sizeof(stored.config.profile_revision));
        feed(&stored.config.run_low_qualified, sizeof(stored.config.run_low_qualified));
        stored.digest = hash;
        nvs_set_blob(1, "config", &stored, sizeof(stored));
        bool integrity{};
        Config loaded = store.load_config(&integrity);
        check(integrity, "old checksum must actually verify");
        check(!loaded.commissioning_accepted, "invalid old acceptance revoked");
        check(loaded.pwm_frequency_hz == stored.config.pwm_frequency_hz &&
              loaded.low_pwm_permille == stored.config.low_pwm_permille &&
              loaded.recovery_purge_pwm_permille == stored.config.recovery_purge_pwm_permille, "no hidden clamping");
        check(!store.accept_config(loaded, store.candidate_token(loaded)), "cannot reaccept invalid NVS");
        Sim s(loaded);
        s.in.heat_enable = true;
        s.advance(200);
        expect_safe(s.out);
        check(!s.out.arm_ready && !s.out.fan_request, "old accepted profile cannot START or purge");
    }
}

void test_output_fault_and_config_edit_latch() {
    for (bool pwm : {false, true}) {
        Sim s;
        drive_to_state(s, pwm ? State::RUN_LOW : State::RUN_HIGH);
        s.in.output_ok = false;
        s.tick();
        check(s.faults() & FAULT_OUTPUT, "runtime output fault latched");
        expect_safe(s.out);
        check(!s.out.arm_ready && !s.out.watchdog_heartbeat, "no permit or heartbeat after output failure");
    }
    Sim s;
    s.boot_wait();
    s.in.configuration_ok = false;
    s.in.heat_enable = true;
    s.tick();
    check(s.faults() & FAULT_CONFIG, "edited candidate inhibits START until reboot");
    expect_safe(s.out);
}

void test_static_recovery_and_unchanged_purge() {
    Config c = test_config();
    c.recovery_purge_pwm_permille = 1000;
    Sim s(c);
    s.boot_wait();
    for (State state : {State::PREPURGE, State::SPINUP, State::IGNITION_TRIAL, State::FLAME_CONFIRM,
                        State::RUN_HIGH, State::POST_PURGE}) {
        Sim active(c);
        drive_to_state(active, state);
        check(active.out.speed_permille == 1000, "high states not clamped to 800");
    }
    for (State state : {State::RUN_LOW, State::RUN_HIGH}) {
        Sim active(c);
        drive_to_state(active, state);
        active.in.hot_air.milli_c = c.air_overtemp_milli_c;
        active.tick();
        expect_safe(active.out);
        check(active.out.fan_request && active.out.speed_permille == 1000, "thermal fault retains full post-purge, not universal fan-OFF");
    }
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"pwm_hardware_boundaries", test_pwm_hardware_boundaries},
        {"real_adapter_off_pwm_dc_pwm_off", test_real_adapter_off_pwm_dc_pwm_off},
        {"real_adapter_rejects_unqualified_commands", test_real_adapter_rejects_unqualified_commands},
        {"real_adapter_initialization_errors", test_real_adapter_initialization_errors},
        {"real_adapter_runtime_errors", test_real_adapter_runtime_errors},
        {"real_adapter_stop_failure_detaches_gpio", test_real_adapter_stop_failure_detaches_gpio},
        {"cli_store_validation_and_token_bypass", test_cli_store_validation_and_token_bypass},
        {"legacy_accepted_nvs_out_of_range", test_legacy_accepted_nvs_out_of_range},
        {"output_fault_and_config_edit_latch", test_output_fault_and_config_edit_latch},
        {"static_recovery_and_unchanged_purge", test_static_recovery_and_unchanged_purge},
        {"config_validation", test_config_validation},
        {"uncommissioned_image_is_locked", test_uncommissioned_image_is_locked},
        {"recovery_purge_after_power_loss_from_every_state",
         test_recovery_purge_after_power_loss_from_every_state},
        {"repeated_power_loss_restarts_recovery_timer",
         test_repeated_power_loss_restarts_recovery_timer},
        {"recovery_blockers_and_request_edges", test_recovery_blockers_and_request_edges},
        {"latched_fault_survives_reboot_until_qualified_reset",
         test_latched_fault_survives_reboot_until_qualified_reset},
        {"cold_start_sequence_and_fuel_gate", test_cold_start_sequence_and_fuel_gate},
        {"glow_always_implies_fan_during_startup", test_glow_always_implies_fan_during_startup},
        {"ignition_timeout_latches", test_ignition_timeout_latches},
        {"flame_confirmation_after_glow_off", test_flame_confirmation_after_glow_off},
        {"confirmation_drop_latches", test_confirmation_drop_latches},
        {"flame_loss_latches", test_flame_loss_latches},
        {"pwm_low_speed", test_pwm_low_speed},
        {"room_stop_and_purge", test_room_stop_and_purge},
        {"stop_from_every_active_state", test_stop_from_every_active_state},
        {"sensor_failure_matrix", test_sensor_failure_matrix},
        {"stale_data_latches", test_stale_data_latches},
        {"self_test_wait_and_timeout", test_self_test_wait_and_timeout},
        {"safety_input_fail_closed", test_safety_input_fail_closed},
        {"local_ui_failure_fails_closed", test_local_ui_failure_fails_closed},
        {"detected_display_init_failure_preserves_recovery_purge",
         test_detected_display_init_failure_preserves_recovery_purge},
        {"display_transfer_failure_during_recovery_preserves_purge",
         test_display_transfer_failure_during_recovery_preserves_purge},
        {"shared_spi_display_contract", test_shared_spi_display_contract},
        {"overtemperature_boundaries", test_overtemperature_boundaries},
        {"hot_start_is_inhibited", test_hot_start_is_inhibited},
        {"manual_reset_no_autostart", test_manual_reset_no_autostart},
        {"set_dominant_latch_reset_handshake", test_set_dominant_latch_reset_handshake},
        {"reset_release_cannot_qualify_safety", test_reset_release_cannot_qualify_safety},
        {"reset_blocker_cancels_qualification", test_reset_blocker_cancels_qualification},
        {"coupled_hardware_latch_reset_protocol", test_coupled_hardware_latch_reset_protocol},
        {"coupled_hardware_latch_debounced_reset_protocol", test_coupled_hardware_latch_debounced_reset_protocol},
        {"watchdog_only_when_healthy", test_watchdog_only_when_healthy},
        {"watchdog_falling_edges_with_loop_jitter", test_watchdog_falling_edges_with_loop_jitter},
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
