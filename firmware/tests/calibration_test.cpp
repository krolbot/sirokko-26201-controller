#include "sirokko/calibration.hpp"
#include "sirokko/regulator.hpp"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace sirokko::calibration;
Config config() {
    Config c{};
    c.floor_code = 100;
    c.ceiling_code = 120;
    c.step_code = 10;
    c.samples_per_window = 2;
    c.sample_period_ms = 100;
    c.maximum_sample_gap_ms = 200;
    c.maximum_sample_age_ms = 200;
    c.maximum_no_new_sample_ms = 300;
    c.maximum_channel_skew_ms = 20;
    c.command_arrival_timeout_ms = 1000;
    c.reference_timeout_ms = c.point_timeout_ms = 10000;
    c.maximum_total_duration_ms = 100000;
    c.maximum_transition_settle_difference_ms = 200;
    c.reference_noise_floor_milli_c = c.reference_drift_floor_milli_c = 1;
    c.reference_noise_ceiling_milli_c = c.reference_drift_ceiling_milli_c = 100;
    c.minimum_command_response_milli_c = 100;
    c.reference_noise_multiplier = c.reference_drift_multiplier = c.repeat_mean_multiplier =
        c.response_multiplier = 2;
    c.maximum_slew_up_codes_per_s = c.maximum_slew_down_codes_per_s = 100;
    return c;
}
Observation observation(uint64_t time, uint16_t code = 120) {
    return {time,
            {static_cast<int32_t>(code) * 100, time, 0, true},
            {static_cast<int32_t>(code) * 50, time, 0, true},
            code,
            true,
            true,
            false,
            false};
}
int main() {
    Calibrator invalid;
    assert(!invalid.start({}, 0));
    assert(invalid.snapshot(0).stop_requested);
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        Calibrator c;
        assert(c.start(config(), 100));
        auto o = observation(100);
        c.tick(o);
        o = observation(200);
        switch (scenario) {
        case 0:
            o.operator_negative = true;
            break;
        case 1:
            o.exhaust.valid = false;
            break;
        case 2:
            o.glow_commanded = true;
            break;
        case 3:
            o.exhaust.sample_ms = 0;
            o.now_ms = 500;
            break;
        case 4:
            o.hot_air.sample_ms = 170;
            break;
        case 5:
            o.now_ms = 99;
            break;
        }
        auto s = c.tick(o);
        assert(s.stop_requested && !s.active && !s.result.profile.valid);
    }
    Calibrator c;
    assert(c.start(config(), 0));
    Snapshot s{};
    int achieved = 120;
    for (uint64_t t = 100; t < 100000 && c.active(); t += 100) {
        achieved += std::clamp(static_cast<int>(c.snapshot(t).target_code) - achieved, -10, 10);
        s = c.tick(observation(t, achieved));
    }
    if (!s.result.completed)
        std::cerr << "Calibration failed: " << reason_name(s.reason) << '\n';
    assert(s.completed_pending_stop && s.result.completed && s.result.profile.valid);
    assert(s.result.profile.min_code == 110 && s.result.profile.max_code == 120 &&
           s.result.profile.tested_point_count == 2);
    // No response to a changed command must not manufacture a learned model.
    Calibrator flat;
    assert(flat.start(config(), 0));
    achieved = 120;
    for (uint64_t t = 100; t < 100000 && flat.active(); t += 100) {
        achieved += std::clamp(static_cast<int>(flat.snapshot(t).target_code) - achieved, -10, 10);
        auto o = observation(t, achieved);
        o.exhaust.milli_c = 12000;
        o.hot_air.milli_c = 6000;
        s = flat.tick(o);
    }
    assert(s.stop_requested && !s.result.profile.valid && s.reason == Reason::NO_COMMAND_RESPONSE);
    sirokko::Regulator reg;
    reg.reset(0, 20, 0.5);
    const auto start = reg.value();
    auto value = reg.tick(1000, 20, 30, false, 1, .005, .005);
    assert(value.fraction >= start.fraction && value.fraction <= start.fraction + .005 + 1e-12);
    const auto before = value;
    value = reg.tick(100000, 20, 30, false, 1, .005, .005);
    assert(value.fraction <= before.fraction + .005 + 1e-12); // No catch-up jump after delayed call.
    const auto finite = value;
    value = reg.tick(100020, std::numeric_limits<double>::quiet_NaN(), 30, true, 0, .005, .005);
    assert(value.fraction == finite.fraction && std::isfinite(value.fraction));
    for (uint64_t t = 101000; t < 1000000; t += 1000) {
        value = reg.tick(t, 30, 15, true, 0, .005, .005);
        assert(value.fraction >= 0 && value.fraction <= 1);
    }
    std::cout << "PASS calibration/regulator: completed trace, rejected traces, bounded motion\n";
}
