#include "sirokko/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sirokko::calibration {
namespace {

bool is_terminal(Phase phase) {
    return phase == Phase::IDLE || phase == Phase::COMPLETE_PENDING_STOP || phase == Phase::FAILED ||
           phase == Phase::CANCELLED;
}

uint64_t absolute_difference(int32_t left, int32_t right) {
    const int64_t difference = static_cast<int64_t>(left) - static_cast<int64_t>(right);
    return difference < 0 ? static_cast<uint64_t>(-difference) : static_cast<uint64_t>(difference);
}

uint64_t absolute_difference_u64(uint64_t left, uint64_t right) {
    return left >= right ? left - right : right - left;
}

uint64_t absolute_difference_i64(int64_t left, int64_t right) {
    const int64_t difference = left - right;
    return difference < 0 ? static_cast<uint64_t>(-difference) : static_cast<uint64_t>(difference);
}

int32_t rounded_int32(double value) {
    const double upper = static_cast<double>(std::numeric_limits<int32_t>::max());
    const double lower = static_cast<double>(std::numeric_limits<int32_t>::min());
    if (value >= upper)
        return std::numeric_limits<int32_t>::max();
    if (value <= lower)
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

uint32_t ceil_uint32(double value) {
    if (!(value > 0.0))
        return 0;
    const double maximum = static_cast<double>(std::numeric_limits<uint32_t>::max());
    if (value >= maximum)
        return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(std::ceil(value));
}

uint64_t multiplied(uint32_t value, uint8_t multiplier) {
    return static_cast<uint64_t>(value) * static_cast<uint64_t>(multiplier);
}

} // namespace

bool Calibrator::valid_config(const Config &config) const {
    if (config.floor_code == 0 || config.ceiling_code == 0 || config.floor_code >= config.ceiling_code ||
        config.step_code == 0)
        return false;

    const uint32_t span = static_cast<uint32_t>(config.ceiling_code) - config.floor_code;
    if (span <= config.step_code)
        return false; // At least two lower points are required for a margin.
    const uint32_t planned_points = (span + config.step_code - 1U) / config.step_code;
    if (planned_points < 2U || planned_points > kMaximumTestedPoints)
        return false;

    if (config.samples_per_window < 2 || config.samples_per_window > kMaximumSamplesPerWindow ||
        config.sample_period_ms == 0 || config.maximum_sample_gap_ms < config.sample_period_ms ||
        config.maximum_sample_age_ms == 0 || config.maximum_no_new_sample_ms == 0 ||
        config.command_arrival_timeout_ms == 0 || config.reference_timeout_ms == 0 ||
        config.point_timeout_ms == 0 || config.maximum_total_duration_ms == 0 ||
        config.maximum_transition_settle_difference_ms == 0)
        return false;

    const uint64_t required_samples =
        static_cast<uint64_t>(config.samples_per_window) * kRequiredAnalysisWindows;
    if (config.sample_period_ms > std::numeric_limits<uint64_t>::max() / required_samples)
        return false;
    const uint64_t minimum_settle_span = config.sample_period_ms * required_samples;
    // Deadlines are inclusive: a usable point must have time strictly before them.
    if (config.reference_timeout_ms <= minimum_settle_span || config.point_timeout_ms <= minimum_settle_span)
        return false;

    if (config.reference_noise_floor_milli_c == 0 ||
        config.reference_noise_floor_milli_c > config.reference_noise_ceiling_milli_c ||
        config.reference_drift_floor_milli_c == 0 ||
        config.reference_drift_floor_milli_c > config.reference_drift_ceiling_milli_c ||
        config.minimum_command_response_milli_c == 0 || config.reference_noise_multiplier == 0 ||
        config.reference_noise_multiplier > 16 || config.reference_drift_multiplier == 0 ||
        config.reference_drift_multiplier > 16 || config.repeat_mean_multiplier == 0 ||
        config.repeat_mean_multiplier > 16 || config.response_multiplier == 0 ||
        config.response_multiplier > 16)
        return false;

    return std::isfinite(config.maximum_slew_up_codes_per_s) &&
           std::isfinite(config.maximum_slew_down_codes_per_s) && config.maximum_slew_up_codes_per_s > 0.0 &&
           config.maximum_slew_down_codes_per_s > 0.0;
}

bool Calibrator::start(const Config &config, uint64_t now_ms) {
    if (active())
        return false;

    config_ = config;
    slew_up_credit_codes_ = slew_down_credit_codes_ = 1.0;
    started_ms_ = now_ms;
    phase_started_ms_ = now_ms;
    last_now_ms_ = now_ms;
    phase_ = Phase::WAIT_REFERENCE_COMMAND;
    reason_ = Reason::NONE;
    result_ = {};
    target_code = config.ceiling_code;
    current_point_code_ = 0;
    accepted_point_count_ = 0;
    have_sample_timestamps_ = false;
    have_command_observation_ = false;
    observed_slew_up_codes_per_s_ = 0.0;
    observed_slew_down_codes_per_s_ = 0.0;
    exhaust_noise_band_milli_c_ = 0;
    hot_air_noise_band_milli_c_ = 0;
    exhaust_drift_band_milli_c_ = 0;
    hot_air_drift_band_milli_c_ = 0;
    reference_ = {};
    pre_point_reference_ = {};
    first_point_ = {};
    returned_reference_ = {};
    accepted_points_.fill({});
    clear_history();

    if (!valid_config(config)) {
        fail(Phase::FAILED, Reason::INVALID_CONFIG);
        return false;
    }
    return true;
}

bool Calibrator::active() const {
    return !is_terminal(phase_);
}

uint64_t Calibrator::phase_elapsed(uint64_t now_ms) const {
    return now_ms >= phase_started_ms_ ? now_ms - phase_started_ms_ : 0;
}

bool Calibrator::validate_context(const Observation &observation) {
    if (!observation.run_active || !observation.fuel_commanded || observation.glow_commanded) {
        fail(Phase::FAILED, Reason::RUN_CONTEXT_LOST);
        return false;
    }
    if (observation.commanded_code < config_.floor_code ||
        observation.commanded_code > config_.ceiling_code) {
        fail(Phase::FAILED, Reason::COMMAND_OUT_OF_RANGE);
        return false;
    }
    return true;
}

bool Calibrator::validate_samples(const Observation &observation) {
    const auto invalid = [](const TemperatureSample &sample) { return !sample.valid || sample.status != 0; };
    if (invalid(observation.exhaust) || invalid(observation.hot_air)) {
        fail(Phase::FAILED, Reason::SENSOR_INVALID);
        return false;
    }
    if (observation.exhaust.sample_ms > observation.now_ms ||
        observation.hot_air.sample_ms > observation.now_ms) {
        fail(Phase::FAILED, Reason::SAMPLE_TIMESTAMP_REGRESSION);
        return false;
    }
    if (observation.now_ms - observation.exhaust.sample_ms > config_.maximum_sample_age_ms ||
        observation.now_ms - observation.hot_air.sample_ms > config_.maximum_sample_age_ms) {
        fail(Phase::FAILED, Reason::STALE_SAMPLE);
        return false;
    }
    if (absolute_difference_u64(observation.exhaust.sample_ms, observation.hot_air.sample_ms) >
        config_.maximum_channel_skew_ms) {
        fail(Phase::FAILED, Reason::SAMPLE_SKEW);
        return false;
    }

    if (!have_sample_timestamps_) {
        have_sample_timestamps_ = true;
        last_exhaust_sample_ms_ = observation.exhaust.sample_ms;
        last_hot_air_sample_ms_ = observation.hot_air.sample_ms;
        last_new_sample_seen_at_ms_ = observation.now_ms;
        return true;
    }

    if (observation.exhaust.sample_ms < last_exhaust_sample_ms_ ||
        observation.hot_air.sample_ms < last_hot_air_sample_ms_) {
        fail(Phase::FAILED, Reason::SAMPLE_TIMESTAMP_REGRESSION);
        return false;
    }
    if (observation.exhaust.sample_ms > last_exhaust_sample_ms_ &&
        observation.hot_air.sample_ms > last_hot_air_sample_ms_) {
        last_exhaust_sample_ms_ = observation.exhaust.sample_ms;
        last_hot_air_sample_ms_ = observation.hot_air.sample_ms;
        last_new_sample_seen_at_ms_ = observation.now_ms;
    } else if (observation.now_ms - last_new_sample_seen_at_ms_ >= config_.maximum_no_new_sample_ms) {
        fail(Phase::FAILED, Reason::REPEATED_SAMPLE);
        return false;
    }
    return true;
}

bool Calibrator::observe_command_slew(const Observation &observation) {
    if (!have_command_observation_) {
        have_command_observation_ = true;
        last_command_code_ = observation.commanded_code;
        last_command_observed_ms_ = observation.now_ms;
        return true;
    }

    const double dt = static_cast<double>(observation.now_ms - last_command_observed_ms_) / 1000.0;
    const double up_budget = std::min(1.0, slew_up_credit_codes_) + config_.maximum_slew_up_codes_per_s * dt;
    const double down_budget =
        std::min(1.0, slew_down_credit_codes_) + config_.maximum_slew_down_codes_per_s * dt;
    double up_spent = 0.0, down_spent = 0.0;
    if (observation.commanded_code != last_command_code_) {
        if (observation.now_ms == last_command_observed_ms_) {
            fail(Phase::FAILED, Reason::COMMAND_SLEW_VIOLATION);
            return false;
        }
        const uint16_t difference =
            observation.commanded_code > last_command_code_
                ? static_cast<uint16_t>(observation.commanded_code - last_command_code_)
                : static_cast<uint16_t>(last_command_code_ - observation.commanded_code);
        const double elapsed_ms = static_cast<double>(observation.now_ms - last_command_observed_ms_);
        const double rate = static_cast<double>(difference) * 1000.0 / elapsed_ms;
        const bool upwards = observation.commanded_code > last_command_code_;
        // Integer PWM observations have one-code rounding uncertainty. A bounded
        // token budget permits that quantization, but cannot renew a one-code
        // allowance on every fast tick and thereby hide sustained overspeed.
        const double budget = upwards ? up_budget : down_budget;
        if (difference > budget + 1e-9) {
            fail(Phase::FAILED, Reason::COMMAND_SLEW_VIOLATION);
            return false;
        }
        if (upwards) {
            up_spent = difference;
            observed_slew_up_codes_per_s_ = std::max(observed_slew_up_codes_per_s_, rate);
        } else {
            down_spent = difference;
            observed_slew_down_codes_per_s_ = std::max(observed_slew_down_codes_per_s_, rate);
        }
    }
    slew_up_credit_codes_ = std::min(1.0, up_budget - up_spent);
    slew_down_credit_codes_ = std::min(1.0, down_budget - down_spent);
    last_command_code_ = observation.commanded_code;
    last_command_observed_ms_ = observation.now_ms;
    return true;
}

void Calibrator::clear_history() {
    history_count_ = 0;
}

void Calibrator::add_sample(const RecordedSample &sample) {
    const uint16_t capacity = static_cast<uint16_t>(config_.samples_per_window) * kRequiredAnalysisWindows;
    if (history_count_ < capacity) {
        history_[history_count_++] = sample;
        return;
    }
    for (uint16_t index = 1; index < capacity; ++index)
        history_[index - 1] = history_[index];
    history_[capacity - 1] = sample;
}

bool Calibrator::record_sample_if_due(const Observation &observation) {
    const uint64_t sample_ms = std::max(observation.exhaust.sample_ms, observation.hot_air.sample_ms);
    // A conversion timestamp equal to the command-arrival timestamp may predate the new command.
    if (sample_ms <= phase_started_ms_)
        return false;

    if (history_count_ != 0) {
        const uint64_t previous_ms = history_[history_count_ - 1].sample_ms;
        if (sample_ms <= previous_ms) {
            if (sample_ms < previous_ms)
                fail(Phase::FAILED, Reason::SAMPLE_TIMESTAMP_REGRESSION);
            return false;
        }
        const uint64_t gap = sample_ms - previous_ms;
        if (gap < config_.sample_period_ms)
            return false;
        if (gap > config_.maximum_sample_gap_ms) {
            fail(Phase::FAILED, Reason::SAMPLE_GAP);
            return false;
        }
    }
    add_sample({sample_ms, observation.exhaust.milli_c, observation.hot_air.milli_c});
    return true;
}

bool Calibrator::analyze_history(PointSummary *summary) const {
    if (!summary)
        return false;
    const uint16_t samples_per_window = config_.samples_per_window;
    const uint16_t required = samples_per_window * kRequiredAnalysisWindows;
    if (history_count_ < required)
        return false;

    auto percentile = [](const std::array<double, kMaximumSamplesPerWindow> &values, uint16_t count,
                         double fraction) {
        const double position = static_cast<double>(count - 1) * fraction;
        const uint16_t lower = static_cast<uint16_t>(std::floor(position));
        const uint16_t upper = static_cast<uint16_t>(std::ceil(position));
        return values[lower] + (values[upper] - values[lower]) * (position - static_cast<double>(lower));
    };

    auto summarize_channel = [&](bool exhaust, int32_t *mean_out, uint32_t *drift_out, uint32_t *noise_out,
                                 uint32_t *span_out) {
        std::array<double, kRequiredAnalysisWindows> means{};
        uint32_t maximum_drift = 0;
        uint32_t maximum_noise = 0;
        for (uint16_t window = 0; window < kRequiredAnalysisWindows; ++window) {
            const uint16_t first = static_cast<uint16_t>(window * samples_per_window);
            const uint64_t first_ms = history_[first].sample_ms;
            double mean_time = 0.0;
            double mean_value = 0.0;
            for (uint16_t index = 0; index < samples_per_window; ++index) {
                const RecordedSample &sample = history_[first + index];
                mean_time += static_cast<double>(sample.sample_ms - first_ms);
                mean_value += static_cast<double>(exhaust ? sample.exhaust_milli_c : sample.hot_air_milli_c);
            }
            mean_time /= samples_per_window;
            mean_value /= samples_per_window;

            double numerator = 0.0;
            double denominator = 0.0;
            std::array<double, kMaximumSamplesPerWindow> residuals{};
            for (uint16_t index = 0; index < samples_per_window; ++index) {
                const RecordedSample &sample = history_[first + index];
                const double time = static_cast<double>(sample.sample_ms - first_ms);
                const double value =
                    static_cast<double>(exhaust ? sample.exhaust_milli_c : sample.hot_air_milli_c);
                numerator += (time - mean_time) * (value - mean_value);
                denominator += (time - mean_time) * (time - mean_time);
            }
            if (!(denominator > 0.0))
                return false;
            const double slope = numerator / denominator;
            for (uint16_t index = 0; index < samples_per_window; ++index) {
                const RecordedSample &sample = history_[first + index];
                const double time = static_cast<double>(sample.sample_ms - first_ms);
                const double value =
                    static_cast<double>(exhaust ? sample.exhaust_milli_c : sample.hot_air_milli_c);
                residuals[index] = value - mean_value - slope * (time - mean_time);
            }
            std::sort(residuals.begin(), residuals.begin() + samples_per_window);
            const uint64_t span_ms = history_[first + samples_per_window - 1].sample_ms - first_ms;
            maximum_drift = std::max(maximum_drift, ceil_uint32(std::fabs(slope) * span_ms));
            maximum_noise =
                std::max(maximum_noise, ceil_uint32(percentile(residuals, samples_per_window, 0.90) -
                                                    percentile(residuals, samples_per_window, 0.10)));
            means[window] = mean_value;
        }
        const auto minimum = *std::min_element(means.begin(), means.end());
        const auto maximum = *std::max_element(means.begin(), means.end());
        double average = 0.0;
        for (double value : means)
            average += value;
        *mean_out = rounded_int32(average / kRequiredAnalysisWindows);
        *drift_out = maximum_drift;
        *noise_out = maximum_noise;
        *span_out = ceil_uint32(maximum - minimum);
        return true;
    };

    PointSummary calculated{};
    if (!summarize_channel(
            true, &calculated.exhaust_mean_milli_c, &calculated.exhaust_max_window_drift_milli_c,
            &calculated.exhaust_max_window_noise_milli_c, &calculated.exhaust_window_mean_span_milli_c) ||
        !summarize_channel(
            false, &calculated.hot_air_mean_milli_c, &calculated.hot_air_max_window_drift_milli_c,
            &calculated.hot_air_max_window_noise_milli_c, &calculated.hot_air_window_mean_span_milli_c))
        return false;
    *summary = calculated;
    return true;
}

bool Calibrator::stable_against_reference(const PointSummary &summary) const {
    const uint64_t exhaust_span_limit = static_cast<uint64_t>(exhaust_drift_band_milli_c_) * 2U;
    const uint64_t air_span_limit = static_cast<uint64_t>(hot_air_drift_band_milli_c_) * 2U;
    return summary.exhaust_max_window_drift_milli_c <= exhaust_drift_band_milli_c_ &&
           summary.hot_air_max_window_drift_milli_c <= hot_air_drift_band_milli_c_ &&
           summary.exhaust_max_window_noise_milli_c <= exhaust_noise_band_milli_c_ &&
           summary.hot_air_max_window_noise_milli_c <= hot_air_noise_band_milli_c_ &&
           summary.exhaust_window_mean_span_milli_c <= exhaust_span_limit &&
           summary.hot_air_window_mean_span_milli_c <= air_span_limit;
}

bool Calibrator::establish_reference(const PointSummary &summary) {
    if (summary.exhaust_max_window_drift_milli_c > config_.reference_drift_ceiling_milli_c ||
        summary.hot_air_max_window_drift_milli_c > config_.reference_drift_ceiling_milli_c ||
        summary.exhaust_max_window_noise_milli_c > config_.reference_noise_ceiling_milli_c ||
        summary.hot_air_max_window_noise_milli_c > config_.reference_noise_ceiling_milli_c)
        return false;

    const uint64_t exhaust_noise = std::max<uint64_t>(
        config_.reference_noise_floor_milli_c,
        multiplied(summary.exhaust_max_window_noise_milli_c, config_.reference_noise_multiplier));
    const uint64_t air_noise = std::max<uint64_t>(
        config_.reference_noise_floor_milli_c,
        multiplied(summary.hot_air_max_window_noise_milli_c, config_.reference_noise_multiplier));
    const uint64_t exhaust_drift = std::max<uint64_t>(
        config_.reference_drift_floor_milli_c,
        multiplied(summary.exhaust_max_window_drift_milli_c, config_.reference_drift_multiplier));
    const uint64_t air_drift = std::max<uint64_t>(
        config_.reference_drift_floor_milli_c,
        multiplied(summary.hot_air_max_window_drift_milli_c, config_.reference_drift_multiplier));
    if (exhaust_noise > config_.reference_noise_ceiling_milli_c ||
        air_noise > config_.reference_noise_ceiling_milli_c ||
        exhaust_drift > config_.reference_drift_ceiling_milli_c ||
        air_drift > config_.reference_drift_ceiling_milli_c)
        return false;

    exhaust_noise_band_milli_c_ = static_cast<uint32_t>(exhaust_noise);
    hot_air_noise_band_milli_c_ = static_cast<uint32_t>(air_noise);
    exhaust_drift_band_milli_c_ = static_cast<uint32_t>(exhaust_drift);
    hot_air_drift_band_milli_c_ = static_cast<uint32_t>(air_drift);
    if (!stable_against_reference(summary))
        return false;

    reference_ = summary;
    pre_point_reference_ = summary;
    // Only movements after the reference was qualified appear in the profile.
    observed_slew_up_codes_per_s_ = 0.0;
    observed_slew_down_codes_per_s_ = 0.0;
    return true;
}

bool Calibrator::reference_matches(const PointSummary &expected, const PointSummary &observed) const {
    const uint64_t exhaust_band =
        std::max<uint32_t>(exhaust_noise_band_milli_c_, exhaust_drift_band_milli_c_);
    const uint64_t air_band = std::max<uint32_t>(hot_air_noise_band_milli_c_, hot_air_drift_band_milli_c_);
    return absolute_difference(expected.exhaust_mean_milli_c, observed.exhaust_mean_milli_c) <=
               exhaust_band * config_.repeat_mean_multiplier &&
           absolute_difference(expected.hot_air_mean_milli_c, observed.hot_air_mean_milli_c) <=
               air_band * config_.repeat_mean_multiplier;
}

bool Calibrator::point_matches(const PointSummary &first, const PointSummary &repeat) const {
    return reference_matches(first, repeat);
}

bool Calibrator::has_command_response(const PointSummary &from, const PointSummary &to) const {
    const uint64_t exhaust_threshold =
        std::max<uint64_t>(config_.minimum_command_response_milli_c,
                           multiplied(exhaust_noise_band_milli_c_, config_.response_multiplier));
    const uint64_t air_threshold =
        std::max<uint64_t>(config_.minimum_command_response_milli_c,
                           multiplied(hot_air_noise_band_milli_c_, config_.response_multiplier));
    return absolute_difference(from.exhaust_mean_milli_c, to.exhaust_mean_milli_c) >= exhaust_threshold ||
           absolute_difference(from.hot_air_mean_milli_c, to.hot_air_mean_milli_c) >= air_threshold;
}

bool Calibrator::transition_is_repeatable(const PointSummary &before_first, const PointSummary &first,
                                          const PointSummary &returned, const PointSummary &repeat) const {
    const auto channel_matches = [&](int32_t before, int32_t first_value, int32_t returned_value,
                                     int32_t repeat_value, uint32_t band) {
        const int64_t down = static_cast<int64_t>(first_value) - before;
        const int64_t up = static_cast<int64_t>(returned_value) - first_value;
        const int64_t repeated_down = static_cast<int64_t>(repeat_value) - returned_value;
        const uint64_t threshold = std::max<uint64_t>(config_.minimum_command_response_milli_c,
                                                      multiplied(band, config_.response_multiplier));
        const uint64_t tolerance = multiplied(std::max<uint32_t>(band, 1U), config_.repeat_mean_multiplier);
        return absolute_difference_i64(down, 0) >= threshold && absolute_difference_i64(up, 0) >= threshold &&
               absolute_difference_i64(repeated_down, 0) >= threshold &&
               absolute_difference_i64(up, -down) <= tolerance &&
               absolute_difference_i64(repeated_down, down) <= tolerance;
    };
    return channel_matches(before_first.exhaust_mean_milli_c, first.exhaust_mean_milli_c,
                           returned.exhaust_mean_milli_c, repeat.exhaust_mean_milli_c,
                           exhaust_noise_band_milli_c_) ||
           channel_matches(before_first.hot_air_mean_milli_c, first.hot_air_mean_milli_c,
                           returned.hot_air_mean_milli_c, repeat.hot_air_mean_milli_c,
                           hot_air_noise_band_milli_c_);
}

void Calibrator::begin_wait(Phase phase, uint16_t requested_target_code, uint64_t now_ms) {
    phase_ = phase;
    target_code = requested_target_code;
    phase_started_ms_ = now_ms;
    clear_history();
}

void Calibrator::begin_settle(Phase phase, uint64_t now_ms) {
    phase_ = phase;
    phase_started_ms_ = now_ms;
    clear_history();
}

uint16_t Calibrator::next_lower_code(uint16_t current_code) const {
    const uint16_t difference = static_cast<uint16_t>(current_code - config_.floor_code);
    return difference <= config_.step_code ? config_.floor_code
                                           : static_cast<uint16_t>(current_code - config_.step_code);
}

void Calibrator::fail(Phase terminal_phase, Reason reason) {
    phase_ = terminal_phase;
    reason_ = reason;
    result_ = {};
    result_.reason = reason;
}

void Calibrator::complete() {
    if (accepted_point_count_ < 2) {
        fail(Phase::FAILED, Reason::INSUFFICIENT_MARGIN);
        return;
    }
    Profile profile{};
    profile.valid = true;
    profile.min_code = accepted_points_[accepted_point_count_ - 2].code;
    profile.max_code = config_.ceiling_code;
    profile.tested_slew_up_codes_per_s = observed_slew_up_codes_per_s_;
    profile.tested_slew_down_codes_per_s = observed_slew_down_codes_per_s_;
    profile.reference = reference_;
    profile.exhaust_fixed_noise_band_milli_c = exhaust_noise_band_milli_c_;
    profile.hot_air_fixed_noise_band_milli_c = hot_air_noise_band_milli_c_;
    profile.exhaust_fixed_drift_band_milli_c = exhaust_drift_band_milli_c_;
    profile.hot_air_fixed_drift_band_milli_c = hot_air_drift_band_milli_c_;
    profile.tested_point_count = accepted_point_count_;
    profile.tested_points = accepted_points_;

    phase_ = Phase::COMPLETE_PENDING_STOP;
    reason_ = Reason::COMPLETE;
    result_ = {true, Reason::COMPLETE, profile};
}

Snapshot Calibrator::tick(const Observation &observation) {
    if (!active())
        return snapshot(observation.now_ms);
    if (observation.now_ms < last_now_ms_) {
        fail(Phase::FAILED, Reason::BACKDATED_TIME);
        return snapshot(observation.now_ms);
    }
    last_now_ms_ = observation.now_ms;

    if (observation.operator_negative) {
        fail(Phase::CANCELLED, Reason::OPERATOR_NEGATIVE);
        return snapshot(observation.now_ms);
    }
    if (observation.now_ms - started_ms_ >= config_.maximum_total_duration_ms) {
        fail(Phase::FAILED, Reason::OVERALL_TIMEOUT);
        return snapshot(observation.now_ms);
    }
    if (!validate_context(observation) || !validate_samples(observation) ||
        !observe_command_slew(observation))
        return snapshot(observation.now_ms);

    const auto begin_waiting_settle = [&](Phase settled_phase) {
        if (phase_elapsed(observation.now_ms) >= config_.command_arrival_timeout_ms) {
            fail(Phase::FAILED, Reason::COMMAND_NOT_REACHED);
        } else if (observation.commanded_code == target_code) {
            begin_settle(settled_phase, observation.now_ms);
        }
    };

    switch (phase_) {
    case Phase::WAIT_REFERENCE_COMMAND:
        begin_waiting_settle(Phase::SETTLE_REFERENCE);
        break;
    case Phase::WAIT_DESCENT_COMMAND:
        begin_waiting_settle(Phase::SETTLE_POINT);
        break;
    case Phase::WAIT_RETURN_COMMAND:
        begin_waiting_settle(Phase::SETTLE_RETURN_REFERENCE);
        break;
    case Phase::WAIT_REPEAT_COMMAND:
        begin_waiting_settle(Phase::SETTLE_REPEAT_POINT);
        break;
    case Phase::WAIT_POST_REPEAT_REFERENCE_COMMAND:
        begin_waiting_settle(Phase::SETTLE_POST_REPEAT_REFERENCE);
        break;
    default:
        break;
    }
    if (!active() || phase_ == Phase::WAIT_REFERENCE_COMMAND || phase_ == Phase::WAIT_DESCENT_COMMAND ||
        phase_ == Phase::WAIT_RETURN_COMMAND || phase_ == Phase::WAIT_REPEAT_COMMAND ||
        phase_ == Phase::WAIT_POST_REPEAT_REFERENCE_COMMAND)
        return snapshot(observation.now_ms);

    const bool reference_settle = phase_ == Phase::SETTLE_REFERENCE ||
                                  phase_ == Phase::SETTLE_RETURN_REFERENCE ||
                                  phase_ == Phase::SETTLE_POST_REPEAT_REFERENCE;
    const uint64_t settle_timeout =
        reference_settle ? config_.reference_timeout_ms : config_.point_timeout_ms;
    if (phase_elapsed(observation.now_ms) >= settle_timeout) {
        fail(Phase::FAILED, reference_settle ? Reason::REFERENCE_TIMEOUT : Reason::POINT_TIMEOUT);
        return snapshot(observation.now_ms);
    }

    record_sample_if_due(observation);
    if (!active())
        return snapshot(observation.now_ms);
    const uint16_t required_samples =
        static_cast<uint16_t>(config_.samples_per_window) * kRequiredAnalysisWindows;
    if (history_count_ < required_samples)
        return snapshot(observation.now_ms);

    PointSummary summary{};
    if (!analyze_history(&summary)) {
        fail(Phase::FAILED, Reason::SAMPLE_TIMESTAMP_REGRESSION);
        return snapshot(observation.now_ms);
    }
    summary.settled_after_ms = phase_elapsed(observation.now_ms);

    switch (phase_) {
    case Phase::SETTLE_REFERENCE:
        if (establish_reference(summary)) {
            current_point_code_ = next_lower_code(config_.ceiling_code);
            begin_wait(Phase::WAIT_DESCENT_COMMAND, current_point_code_, observation.now_ms);
        }
        break;
    case Phase::SETTLE_POINT:
        if (stable_against_reference(summary)) {
            first_point_ = summary;
            if (!has_command_response(pre_point_reference_, first_point_)) {
                fail(Phase::FAILED, Reason::NO_COMMAND_RESPONSE);
            } else {
                begin_wait(Phase::WAIT_RETURN_COMMAND, config_.ceiling_code, observation.now_ms);
            }
        }
        break;
    case Phase::SETTLE_RETURN_REFERENCE:
        if (stable_against_reference(summary)) {
            returned_reference_ = summary;
            if (!reference_matches(pre_point_reference_, returned_reference_)) {
                fail(Phase::FAILED, Reason::REFERENCE_NOT_REPRODUCIBLE);
            } else {
                begin_wait(Phase::WAIT_REPEAT_COMMAND, current_point_code_, observation.now_ms);
            }
        }
        break;
    case Phase::SETTLE_REPEAT_POINT:
        if (stable_against_reference(summary)) {
            if (!point_matches(first_point_, summary)) {
                fail(Phase::FAILED, Reason::POINT_NOT_REPRODUCIBLE);
            } else if (absolute_difference_u64(first_point_.settled_after_ms, summary.settled_after_ms) >
                       config_.maximum_transition_settle_difference_ms) {
                fail(Phase::FAILED, Reason::DYNAMIC_NOT_REPRODUCIBLE);
            } else if (!transition_is_repeatable(pre_point_reference_, first_point_, returned_reference_,
                                                 summary)) {
                fail(Phase::FAILED, Reason::DYNAMIC_NOT_REPRODUCIBLE);
            } else if (accepted_point_count_ >= kMaximumTestedPoints) {
                fail(Phase::FAILED, Reason::INSUFFICIENT_MARGIN);
            } else {
                accepted_points_[accepted_point_count_] = {current_point_code_, first_point_, summary};
                ++accepted_point_count_;
                if (current_point_code_ == config_.floor_code) {
                    complete();
                } else {
                    begin_wait(Phase::WAIT_POST_REPEAT_REFERENCE_COMMAND, config_.ceiling_code,
                               observation.now_ms);
                }
            }
        }
        break;
    case Phase::SETTLE_POST_REPEAT_REFERENCE:
        if (stable_against_reference(summary)) {
            if (!reference_matches(returned_reference_, summary)) {
                fail(Phase::FAILED, Reason::REFERENCE_NOT_REPRODUCIBLE);
            } else {
                pre_point_reference_ = summary;
                current_point_code_ = next_lower_code(current_point_code_);
                begin_wait(Phase::WAIT_DESCENT_COMMAND, current_point_code_, observation.now_ms);
            }
        }
        break;
    default:
        break;
    }
    return snapshot(observation.now_ms);
}

void Calibrator::cancel(Reason reason) {
    if (!active() && phase_ != Phase::COMPLETE_PENDING_STOP)
        return;
    if (reason == Reason::NONE || reason == Reason::COMPLETE) {
        reason = Reason::EXTERNAL_CANCEL;
    }
    fail(Phase::CANCELLED, reason);
}

Snapshot Calibrator::snapshot(uint64_t now_ms) const {
    Snapshot snapshot{};
    snapshot.phase = phase_;
    snapshot.reason = reason_;
    snapshot.target_code = target_code;
    snapshot.accepted_point_count = accepted_point_count_;
    snapshot.elapsed_ms = now_ms >= started_ms_ ? now_ms - started_ms_ : 0;
    snapshot.active = active();
    snapshot.stop_requested =
        phase_ == Phase::COMPLETE_PENDING_STOP || phase_ == Phase::FAILED || phase_ == Phase::CANCELLED;
    snapshot.completed_pending_stop = phase_ == Phase::COMPLETE_PENDING_STOP;
    snapshot.result = result_;
    return snapshot;
}

const char *phase_name(Phase phase) {
    switch (phase) {
    case Phase::IDLE:
        return "IDLE";
    case Phase::WAIT_REFERENCE_COMMAND:
        return "WAIT_REFERENCE_COMMAND";
    case Phase::SETTLE_REFERENCE:
        return "SETTLE_REFERENCE";
    case Phase::WAIT_DESCENT_COMMAND:
        return "WAIT_DESCENT_COMMAND";
    case Phase::SETTLE_POINT:
        return "SETTLE_POINT";
    case Phase::WAIT_RETURN_COMMAND:
        return "WAIT_RETURN_COMMAND";
    case Phase::SETTLE_RETURN_REFERENCE:
        return "SETTLE_RETURN_REFERENCE";
    case Phase::WAIT_REPEAT_COMMAND:
        return "WAIT_REPEAT_COMMAND";
    case Phase::SETTLE_REPEAT_POINT:
        return "SETTLE_REPEAT_POINT";
    case Phase::WAIT_POST_REPEAT_REFERENCE_COMMAND:
        return "WAIT_POST_REPEAT_REFERENCE_COMMAND";
    case Phase::SETTLE_POST_REPEAT_REFERENCE:
        return "SETTLE_POST_REPEAT_REFERENCE";
    case Phase::COMPLETE_PENDING_STOP:
        return "COMPLETE_PENDING_STOP";
    case Phase::FAILED:
        return "FAILED";
    case Phase::CANCELLED:
        return "CANCELLED";
    }
    return "UNKNOWN";
}

const char *reason_name(Reason reason) {
    switch (reason) {
    case Reason::NONE:
        return "NONE";
    case Reason::COMPLETE:
        return "COMPLETE";
    case Reason::INVALID_CONFIG:
        return "INVALID_CONFIG";
    case Reason::BACKDATED_TIME:
        return "BACKDATED_TIME";
    case Reason::OVERALL_TIMEOUT:
        return "OVERALL_TIMEOUT";
    case Reason::RUN_CONTEXT_LOST:
        return "RUN_CONTEXT_LOST";
    case Reason::COMMAND_OUT_OF_RANGE:
        return "COMMAND_OUT_OF_RANGE";
    case Reason::COMMAND_SLEW_VIOLATION:
        return "COMMAND_SLEW_VIOLATION";
    case Reason::COMMAND_NOT_REACHED:
        return "COMMAND_NOT_REACHED";
    case Reason::SENSOR_INVALID:
        return "SENSOR_INVALID";
    case Reason::STALE_SAMPLE:
        return "STALE_SAMPLE";
    case Reason::SAMPLE_TIMESTAMP_REGRESSION:
        return "SAMPLE_TIMESTAMP_REGRESSION";
    case Reason::SAMPLE_SKEW:
        return "SAMPLE_SKEW";
    case Reason::REPEATED_SAMPLE:
        return "REPEATED_SAMPLE";
    case Reason::SAMPLE_GAP:
        return "SAMPLE_GAP";
    case Reason::REFERENCE_TIMEOUT:
        return "REFERENCE_TIMEOUT";
    case Reason::POINT_TIMEOUT:
        return "POINT_TIMEOUT";
    case Reason::REFERENCE_NOT_REPRODUCIBLE:
        return "REFERENCE_NOT_REPRODUCIBLE";
    case Reason::NO_COMMAND_RESPONSE:
        return "NO_COMMAND_RESPONSE";
    case Reason::POINT_NOT_REPRODUCIBLE:
        return "POINT_NOT_REPRODUCIBLE";
    case Reason::DYNAMIC_NOT_REPRODUCIBLE:
        return "DYNAMIC_NOT_REPRODUCIBLE";
    case Reason::INSUFFICIENT_MARGIN:
        return "INSUFFICIENT_MARGIN";
    case Reason::OPERATOR_NEGATIVE:
        return "OPERATOR_NEGATIVE";
    case Reason::EXTERNAL_CANCEL:
        return "EXTERNAL_CANCEL";
    }
    return "UNKNOWN";
}

} // namespace sirokko::calibration
