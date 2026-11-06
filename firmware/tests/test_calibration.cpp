#include "sirokko/calibration.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using namespace sirokko::calibration;

namespace {

Config synthetic_config() {
    Config config{};
    config.floor_code = 400;
    config.ceiling_code = 1000;
    config.step_code = 200;
    config.samples_per_window = 8;
    config.sample_period_ms = 1000;
    config.maximum_sample_gap_ms = 1500;
    config.maximum_sample_age_ms = 5000;
    config.maximum_no_new_sample_ms = 3000;
    config.maximum_channel_skew_ms = 0;
    config.command_arrival_timeout_ms = 250000;
    config.reference_timeout_ms = 120000;
    config.point_timeout_ms = 180000;
    config.maximum_total_duration_ms = 2500000;
    config.maximum_transition_settle_difference_ms = 15000;
    config.reference_noise_floor_milli_c = 300;
    config.reference_noise_ceiling_milli_c = 3000;
    config.reference_drift_floor_milli_c = 1000;
    config.reference_drift_ceiling_milli_c = 6000;
    config.minimum_command_response_milli_c = 1500;
    config.reference_noise_multiplier = 3;
    config.reference_drift_multiplier = 3;
    config.repeat_mean_multiplier = 3;
    config.response_multiplier = 3;
    config.maximum_slew_up_codes_per_s = 3.0;
    config.maximum_slew_down_codes_per_s = 3.0;
    return config;
}

enum class PlantKind : uint8_t { NORMAL, CONSTANT, SLOW_RESIDUAL, INCONSISTENT_REPEAT, NOISY };

class SyntheticPlant {
public:
    SyntheticPlant(const Config& config, PlantKind kind)
        : config_(config), kind_(kind), code_(config.ceiling_code), last_target_(config.ceiling_code),
          exhaust_(160000.0 + 110.0 * config.ceiling_code), hot_air_(70000.0) {}

    void advance(uint16_t target) {
        now_ms_ += 1000;
        if (target != last_target_) {
            if (target < config_.ceiling_code) ++lower_target_visits_;
            last_target_ = target;
        }
        if (code_ < target) {
            const uint16_t difference = static_cast<uint16_t>(target - code_);
            code_ = static_cast<uint16_t>(code_ + (difference > 3 ? 3 : difference));
        } else if (code_ > target) {
            const uint16_t difference = static_cast<uint16_t>(code_ - target);
            code_ = static_cast<uint16_t>(code_ - (difference > 3 ? 3 : difference));
        }

        const double previous_exhaust = exhaust_;
        if (kind_ == PlantKind::CONSTANT) {
            // Fresh conversions whose values are frozen: deliberately indistinguishable by freshness alone.
        } else if (kind_ == PlantKind::SLOW_RESIDUAL) {
            // Deliberately smooth residual heat, unrelated to commanded code.
            exhaust_ -= 25.0;
            hot_air_ -= 15.0;
        } else {
            double exhaust_target = 160000.0 + 110.0 * code_;
            double air_target = 70000.0 + 15.0 * (config_.ceiling_code - code_);
            if (kind_ == PlantKind::INCONSISTENT_REPEAT && last_target_ == 800 &&
                lower_target_visits_ >= 2) {
                exhaust_target -= 10000.0;
                air_target += 6000.0;
            }
            exhaust_ += 0.25 * (exhaust_target - exhaust_);
            hot_air_ += 0.20 * (air_target - hot_air_);
            if (kind_ == PlantKind::NOISY && last_target_ < config_.ceiling_code) {
                const double noise = ((now_ms_ / 1000) % 2U == 0U) ? 6000.0 : -6000.0;
                exhaust_ += noise;
                hot_air_ -= noise;
            }
        }
        if (last_target_ < config_.ceiling_code && exhaust_ < previous_exhaust) saw_exhaust_cooling_ = true;
        if (last_target_ < config_.ceiling_code && hot_air_ > initial_hot_air_) saw_hot_air_rise_ = true;
    }

    Observation observation() const {
        const auto exhaust = static_cast<int32_t>(std::lround(exhaust_));
        const auto hot_air = static_cast<int32_t>(std::lround(hot_air_));
        return {now_ms_, {exhaust, now_ms_, 0, true}, {hot_air, now_ms_, 0, true},
                code_, true, true, false, false};
    }

    uint64_t now_ms() const { return now_ms_; }
    bool saw_exhaust_cooling() const { return saw_exhaust_cooling_; }
    bool saw_hot_air_rise() const { return saw_hot_air_rise_; }

private:
    Config config_;
    PlantKind kind_;
    uint64_t now_ms_{};
    uint16_t code_{};
    uint16_t last_target_{};
    unsigned lower_target_visits_{};
    double exhaust_{};
    double hot_air_{};
    const double initial_hot_air_{70000.0};
    bool saw_exhaust_cooling_{};
    bool saw_hot_air_rise_{};
};

Snapshot run_to_terminal(Calibrator& calibrator, SyntheticPlant& plant, uint64_t maximum_steps = 4000) {
    Snapshot state = calibrator.snapshot(plant.now_ms());
    for (uint64_t step = 0; step < maximum_steps && state.active; ++step) {
        plant.advance(state.target_code);
        state = calibrator.tick(plant.observation());
    }
    assert(!state.active);
    return state;
}

Observation good_observation(uint64_t now_ms, uint16_t code) {
    return {now_ms, {250000, now_ms, 0, true}, {75000, now_ms, 0, true},
            code, true, true, false, false};
}

void test_full_autonomous_cycle_with_opposite_channel_signs() {
    const Config config = synthetic_config();
    Calibrator calibrator;
    assert(calibrator.start(config, 0));  // The only start command in this test.
    SyntheticPlant plant(config, PlantKind::NORMAL);
    const Snapshot state = run_to_terminal(calibrator, plant);

    assert(state.phase == Phase::COMPLETE_PENDING_STOP);
    assert(state.reason == Reason::COMPLETE);
    assert(state.stop_requested && state.completed_pending_stop);
    assert(state.result.completed && state.result.profile.valid);
    const Profile& profile = state.result.profile;
    assert(profile.min_code == 600 && profile.max_code == 1000);
    assert(profile.tested_point_count == 3);
    assert(profile.tested_points[0].code == 800 && profile.tested_points[1].code == 600 &&
           profile.tested_points[2].code == 400);
    assert(profile.tested_slew_up_codes_per_s > 0.0 &&
           profile.tested_slew_up_codes_per_s <= config.maximum_slew_up_codes_per_s);
    assert(profile.tested_slew_down_codes_per_s > 0.0 &&
           profile.tested_slew_down_codes_per_s <= config.maximum_slew_down_codes_per_s);
    // Lower command cools exhaust while the synthetic hot-air channel rises.
    assert(profile.tested_points[0].first.exhaust_mean_milli_c < profile.reference.exhaust_mean_milli_c);
    assert(profile.tested_points[0].first.hot_air_mean_milli_c > profile.reference.hot_air_mean_milli_c);
    assert(plant.saw_exhaust_cooling());
    assert(plant.saw_hot_air_rise());
}

void test_short_operator_cancel_at_every_active_phase() {
    constexpr std::array<Phase, 10> phases{{
        Phase::WAIT_REFERENCE_COMMAND,
        Phase::SETTLE_REFERENCE,
        Phase::WAIT_DESCENT_COMMAND,
        Phase::SETTLE_POINT,
        Phase::WAIT_RETURN_COMMAND,
        Phase::SETTLE_RETURN_REFERENCE,
        Phase::WAIT_REPEAT_COMMAND,
        Phase::SETTLE_REPEAT_POINT,
        Phase::WAIT_POST_REPEAT_REFERENCE_COMMAND,
        Phase::SETTLE_POST_REPEAT_REFERENCE,
    }};

    for (const Phase wanted : phases) {
        const Config config = synthetic_config();
        Calibrator calibrator;
        assert(calibrator.start(config, 0));
        SyntheticPlant plant(config, PlantKind::NORMAL);
        Snapshot state = calibrator.snapshot(0);
        bool reached = false;
        for (uint64_t step = 0; step < 4000; ++step) {
            if (state.phase == wanted) {
                Observation negative = plant.observation();
                negative.operator_negative = true;
                state = calibrator.tick(negative);
                assert(state.phase == Phase::CANCELLED && state.reason == Reason::OPERATOR_NEGATIVE);
                assert(state.stop_requested && !state.result.completed && !state.result.profile.valid);
                reached = true;
                break;
            }
            assert(state.active);
            plant.advance(state.target_code);
            state = calibrator.tick(plant.observation());
        }
        assert(reached);
    }
}

void test_invalid_stale_repeated_and_backdated_inputs() {
    Config invalid = synthetic_config();
    invalid.floor_code = 0;
    Calibrator calibrator;
    assert(!calibrator.start(invalid, 0));
    Snapshot state = calibrator.snapshot(0);
    assert(state.phase == Phase::FAILED && state.reason == Reason::INVALID_CONFIG && state.stop_requested);

    const Config config = synthetic_config();
    assert(calibrator.start(config, 0));
    Observation stale = good_observation(config.maximum_sample_age_ms + 1, config.ceiling_code);
    stale.exhaust.sample_ms = 0;
    stale.hot_air.sample_ms = 0;
    state = calibrator.tick(stale);
    assert(state.phase == Phase::FAILED && state.reason == Reason::STALE_SAMPLE && !state.result.profile.valid);

    assert(calibrator.start(config, 0));
    state = calibrator.tick(good_observation(0, config.ceiling_code));
    assert(state.active);
    Observation repeated = good_observation(config.maximum_no_new_sample_ms, config.ceiling_code);
    repeated.exhaust.sample_ms = 0;
    repeated.hot_air.sample_ms = 0;
    state = calibrator.tick(repeated);
    assert(state.phase == Phase::FAILED && state.reason == Reason::REPEATED_SAMPLE);

    assert(calibrator.start(config, 10));
    state = calibrator.tick(good_observation(10, config.ceiling_code));
    assert(state.active);
    state = calibrator.tick(good_observation(9, config.ceiling_code));
    assert(state.phase == Phase::FAILED && state.reason == Reason::BACKDATED_TIME);

    assert(calibrator.start(config, 10));
    state = calibrator.tick(good_observation(10, config.ceiling_code));
    assert(state.active);
    Observation timestamp_regression = good_observation(11, config.ceiling_code);
    timestamp_regression.exhaust.sample_ms = 9;
    timestamp_regression.hot_air.sample_ms = 9;
    state = calibrator.tick(timestamp_regression);
    assert(state.phase == Phase::FAILED && state.reason == Reason::SAMPLE_TIMESTAMP_REGRESSION);

    assert(calibrator.start(config, 0));
    Observation invalid_sensor = good_observation(1, config.ceiling_code);
    invalid_sensor.hot_air.status = 1;
    state = calibrator.tick(invalid_sensor);
    assert(state.phase == Phase::FAILED && state.reason == Reason::SENSOR_INVALID);

    assert(calibrator.start(config, 0));
    state = calibrator.tick(good_observation(0, config.ceiling_code));
    assert(state.active);
    state = calibrator.tick(good_observation(1, config.floor_code));
    assert(state.phase == Phase::FAILED && state.reason == Reason::COMMAND_SLEW_VIOLATION);

    const uint64_t near_maximum = std::numeric_limits<uint64_t>::max() - 10U;
    assert(calibrator.start(config, near_maximum));
    state = calibrator.tick(good_observation(near_maximum, config.ceiling_code));
    assert(state.active);
    state = calibrator.tick(good_observation(near_maximum + 1U, config.ceiling_code));
    assert(state.active);
    calibrator.cancel();
}

void test_fresh_constant_and_slow_residual_data_do_not_complete() {
    const Config config = synthetic_config();

    Calibrator constant;
    assert(constant.start(config, 0));
    SyntheticPlant frozen(config, PlantKind::CONSTANT);
    Snapshot state = run_to_terminal(constant, frozen);
    assert(state.phase == Phase::FAILED && state.reason == Reason::NO_COMMAND_RESPONSE);
    assert(!state.result.completed && !state.result.profile.valid);

    Calibrator residual;
    assert(residual.start(config, 0));
    SyntheticPlant slow(config, PlantKind::SLOW_RESIDUAL);
    state = run_to_terminal(residual, slow);
    assert(state.phase == Phase::FAILED);
    assert(state.reason == Reason::REFERENCE_NOT_REPRODUCIBLE ||
           state.reason == Reason::DYNAMIC_NOT_REPRODUCIBLE ||
           state.reason == Reason::NO_COMMAND_RESPONSE);
    assert(!state.result.completed && !state.result.profile.valid);
}

void test_noise_and_inconsistent_repeat_reject() {
    const Config config = synthetic_config();

    Calibrator noisy;
    assert(noisy.start(config, 0));
    SyntheticPlant noise(config, PlantKind::NOISY);
    Snapshot state = run_to_terminal(noisy, noise);
    assert(state.phase == Phase::FAILED && state.reason == Reason::POINT_TIMEOUT);
    assert(!state.result.profile.valid);

    Calibrator inconsistent;
    assert(inconsistent.start(config, 0));
    SyntheticPlant changed_repeat(config, PlantKind::INCONSISTENT_REPEAT);
    state = run_to_terminal(inconsistent, changed_repeat);
    assert(state.phase == Phase::FAILED && state.reason == Reason::POINT_NOT_REPRODUCIBLE);
    assert(!state.result.profile.valid);
}

void test_inclusive_deadline_and_explicit_cancel() {
    Config config = synthetic_config();
    config.maximum_total_duration_ms = 100;
    Calibrator deadline;
    assert(deadline.start(config, 0));
    Snapshot state = deadline.tick(good_observation(100, config.ceiling_code));
    assert(state.phase == Phase::FAILED && state.reason == Reason::OVERALL_TIMEOUT);

    config = synthetic_config();
    Calibrator cancelled;
    assert(cancelled.start(config, 0));
    cancelled.cancel();
    state = cancelled.snapshot(0);
    assert(state.phase == Phase::CANCELLED && state.reason == Reason::EXTERNAL_CANCEL &&
           state.stop_requested && !state.result.profile.valid);
}

void test_quantized_slew_budget() {
    auto config=synthetic_config();
    Calibrator smooth;
    assert(smooth.start(config,0));
    Snapshot state{};
    uint64_t moving_ms=0;
    bool moved=false;
    for(uint64_t now=20;now<60000;now+=20) {
        if(state.phase==Phase::WAIT_DESCENT_COMMAND) moving_ms+=20;
        const auto code=static_cast<uint16_t>(std::lround(config.ceiling_code-3.0*moving_ms/1000.0));
        state=smooth.tick(good_observation(now,code));
        assert(state.active);
        moved|=code<config.ceiling_code;
    }
    assert(moved);
    Calibrator too_fast;
    assert(too_fast.start(config,0));
    assert(too_fast.tick(good_observation(20,1000)).active);
    assert(too_fast.tick(good_observation(40,999)).active); // one-code quantization allowance
    state=too_fast.tick(good_observation(60,998));
    assert(state.reason==Reason::COMMAND_SLEW_VIOLATION && state.stop_requested);
    std::cout<<"PASS: 20ms integer PWM quantization accepted, sustained overspeed rejected\n";
}
}  // namespace

int main() {
    test_quantized_slew_budget();
    test_full_autonomous_cycle_with_opposite_channel_signs();
    test_short_operator_cancel_at_every_active_phase();
    test_invalid_stale_repeated_and_backdated_inputs();
    test_fresh_constant_and_slow_residual_data_do_not_complete();
    test_noise_and_inconsistent_repeat_reject();
    test_inclusive_deadline_and_explicit_cancel();
    std::cout << "PASS: synthetic autonomous temperature-calibration cycle; command-conditioned "
                 "opposite-sign dynamics; per-phase short cancel; invalid/stale/repeated/backdated "
                 "data; fresh constants, residual heat, noise and inconsistent repeats reject; "
                 "inclusive deadline and pending-stop profile contract\n";
}
