#include "sirokko/combustion.hpp"
#include "sirokko/control.hpp"
#include <algorithm>
#include <cmath>

namespace sirokko::combustion {
void Monitor::reset(int32_t minimum_exhaust, uint16_t reference_speed) {
    *this = {};
    minimum_exhaust_milli_c_ = minimum_exhaust;
    command_tolerance_ = std::max<uint16_t>(1, reference_speed / 100);
    snapshot_.phase = Phase::LEARNING;
}
void Monitor::clear_window() {
    count_ = next_ = 0;
    suspect_samples_ = 0;
    suspect_started_ms_ = 0;
    snapshot_ = {};
    snapshot_.phase = Phase::LEARNING;
}
const Monitor::Point &Monitor::point(size_t index) const {
    return window_[(next_ + kWindowSamples - count_ + index) % kWindowSamples];
}
void Monitor::learn() {
    if (count_ != kWindowSamples || snapshot_.phase != Phase::LEARNING)
        return;
    auto stable = [&](bool exhaust, double *mean, double *sigma) {
        std::array<int32_t, kWindowSamples - 1> differences{};
        double blocks[3]{};
        int32_t low = INT32_MAX, high = INT32_MIN;
        for (size_t i = 0; i < count_; ++i) {
            const auto value = exhaust ? point(i).exhaust : point(i).air;
            blocks[i / 20] += value / 20.0;
            low = std::min(low, value);
            high = std::max(high, value);
            if (i)
                differences[i - 1] = std::abs(value - (exhaust ? point(i - 1).exhaust : point(i - 1).air));
        }
        std::sort(differences.begin(), differences.end());
        const int32_t step_noise = differences[differences.size() / 2];
        const double drift = *std::max_element(blocks, blocks + 3) - *std::min_element(blocks, blocks + 3);
        if (drift > std::max<int32_t>(1500, step_noise * 6) ||
            high - low > std::max<int32_t>(4000, step_noise * 12))
            return false;
        *mean = (blocks[0] + blocks[1] + blocks[2]) / 3;
        double variance = 0;
        for (size_t i = 0; i < count_; ++i) {
            const double delta = (exhaust ? point(i).exhaust : point(i).air) - *mean;
            variance += delta * delta;
        }
        *sigma = std::sqrt(variance / count_);
        return true;
    };
    double exhaust{}, air{}, exhaust_noise{}, air_noise{};
    if (!stable(true, &exhaust, &exhaust_noise) || !stable(false, &air, &air_noise) ||
        exhaust <= minimum_exhaust_milli_c_ ||
        exhaust_noise > std::max(2000.0, (exhaust - minimum_exhaust_milli_c_) * .02) || air_noise > 2000)
        return;
    snapshot_.reference_exhaust_milli_c = static_cast<int32_t>(std::lround(exhaust));
    snapshot_.reference_air_milli_c = static_cast<int32_t>(std::lround(air));
    snapshot_.drop_limit_milli_c = static_cast<int32_t>(
        std::ceil(std::max({4000.0, 6 * exhaust_noise, (exhaust - minimum_exhaust_milli_c_) * .20})));
    snapshot_.phase = Phase::WATCHING;
}
Snapshot Monitor::observe(uint64_t now, const Sensors &sensors, uint16_t command) {
    if (snapshot_.phase == Phase::LOST || snapshot_.phase == Phase::OFF)
        return snapshot_;
    const auto &exhaust = sensors.exhaust;
    const auto &air = sensors.air;
    const auto skew = exhaust.sample_ms > air.sample_ms ? exhaust.sample_ms - air.sample_ms
                                                        : air.sample_ms - exhaust.sample_ms;
    if (!thermocouple_fresh(exhaust, now) || !thermocouple_fresh(air, now) || skew > 250 ||
        (have_timestamps_ &&
         (now < last_now_ms_ || exhaust.sample_ms < last_exhaust_ms_ || air.sample_ms < last_air_ms_))) {
        clear_window();
        have_timestamps_ = false;
        return snapshot_;
    }
    last_now_ms_ = now;
    if (have_timestamps_ && (exhaust.sample_ms == last_exhaust_ms_ || air.sample_ms == last_air_ms_))
        return snapshot_;
    const uint64_t at = std::min(exhaust.sample_ms, air.sample_ms);
    if (count_ && at - point(count_ - 1).at_ms < kSamplePeriodMs)
        return snapshot_;
    if (count_ && at - point(count_ - 1).at_ms > 2 * kSamplePeriodMs)
        clear_window(); // Gaps cannot count as continuous confirmation.
    last_exhaust_ms_ = exhaust.sample_ms;
    last_air_ms_ = air.sample_ms;
    have_timestamps_ = true;
    if (!have_command_ || std::abs(static_cast<int>(command) - command_) > command_tolerance_) {
        clear_window();
        command_ = command;
        have_command_ = true;
    }
    window_[next_] = {exhaust.milli_c, air.milli_c, at};
    next_ = (next_ + 1) % kWindowSamples;
    count_ = std::min(count_ + 1, kWindowSamples);
    snapshot_.samples = static_cast<uint8_t>(count_);
    if (count_ > 1) {
        const auto &before = point(count_ > 10 ? count_ - 10 : 0);
        const double dt = static_cast<double>(at - before.at_ms) / 1000;
        snapshot_.exhaust_slope_milli_c_per_s =
            static_cast<int32_t>(std::lround((exhaust.milli_c - before.exhaust) / dt));
        snapshot_.air_slope_milli_c_per_s =
            static_cast<int32_t>(std::lround((air.milli_c - before.air) / dt));
    }
    learn();
    if (snapshot_.phase == Phase::WATCHING || snapshot_.phase == Phase::SUSPECT) {
        const bool dropped =
            snapshot_.reference_exhaust_milli_c - exhaust.milli_c > snapshot_.drop_limit_milli_c;
        if (dropped) {
            if (!suspect_samples_)
                suspect_started_ms_ = at;
            if (suspect_samples_ < 255)
                ++suspect_samples_;
            snapshot_.suspect_ms = at - suspect_started_ms_;
            snapshot_.phase = suspect_samples_ >= 3 && snapshot_.suspect_ms >= kConfirmationMs
                                  ? Phase::LOST
                                  : Phase::SUSPECT;
        } else {
            suspect_samples_ = 0;
            snapshot_.suspect_ms = 0;
            snapshot_.phase = Phase::WATCHING;
        }
    }
    return snapshot_;
}
} // namespace sirokko::combustion
