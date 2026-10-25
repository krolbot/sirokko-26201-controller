#include "sirokko/operator.hpp"
#include <algorithm>
#include <limits>

namespace sirokko {
namespace {
uint64_t saturated_add(uint64_t a, uint64_t b) {
    return b > std::numeric_limits<uint64_t>::max() - a ? std::numeric_limits<uint64_t>::max() : a + b;
}
bool run(State s) { return s == State::RUN_HIGH || s == State::RUN_LOW; }
}
bool operator_settings_valid(const OperatorSettings& s, const Config& c) {
    return s.schema == kOperatorSchema && s.setpoint_milli_c >= c.setpoint_min_milli_c &&
           s.setpoint_milli_c <= c.setpoint_max_milli_c &&
           s.hysteresis_milli_c >= kHysteresisMinMilliC && s.hysteresis_milli_c <= kHysteresisMaxMilliC &&
           s.room_offset_milli_c >= -kRoomOffsetLimitMilliC && s.room_offset_milli_c <= kRoomOffsetLimitMilliC;
}
SensorSample calibrated_room(const SensorSample& raw, int32_t offset, uint32_t now, uint32_t stale) {
    SensorSample sample = raw;
    // Never turn missing/CRC/85C/stale/raw implausible samples into a usable measurement.
    if (sample.quality != SensorQuality::OK) return sample;
    if (raw.milli_c < -55000 || raw.milli_c > 125000) sample.quality = SensorQuality::IMPLAUSIBLE;
    else if (raw.milli_c == 85000) sample.quality = SensorQuality::POWER_ON_DEFAULT;
    else if (static_cast<uint32_t>(now - raw.sample_ms) > stale) sample.quality = SensorQuality::TIMEOUT;
    else if (offset < -kRoomOffsetLimitMilliC || offset > kRoomOffsetLimitMilliC) sample.quality = SensorQuality::IMPLAUSIBLE;
    else sample.milli_c = raw.milli_c + offset;
    return sample;
}
RoomZone room_zone(const SensorSample& room, const OperatorSettings& s, uint32_t now, uint32_t stale) {
    if (room.quality != SensorQuality::OK || static_cast<uint32_t>(now - room.sample_ms) > stale) return RoomZone::INVALID;
    const int64_t delta = static_cast<int64_t>(room.milli_c) - s.setpoint_milli_c;
    if (delta <= -s.hysteresis_milli_c / 2) return RoomZone::BELOW;
    if (delta >= s.hysteresis_milli_c / 2) return RoomZone::ABOVE;
    return RoomZone::BAND;
}
void CommandHistory::begin(uint32_t boot_id, const CommandCounters& restored) {
    *this = CommandHistory{};
    boot_id_ = boot_id;
    counters_ = restored;
}
void CommandHistory::observe(const Status& s, uint64_t now, StopReason reason) {
    if (initialized_) {
        const uint64_t dt = now >= previous_ms_ ? now - previous_ms_ : 0;
        if (run(previous_.state)) counters_.run_ms = saturated_add(counters_.run_ms, dt);
        if (previous_.outputs.glow_command) counters_.glow_ms = saturated_add(counters_.glow_ms, dt);
    }
    const bool changed = !initialized_ || previous_.state != s.state || previous_.faults != s.faults;
    if (changed) {
        if (s.state == State::PREPURGE && (!initialized_ || previous_.state != s.state)) {
            counters_.starts = saturated_add(counters_.starts, 1);
            if (session_id_ != UINT32_MAX) ++session_id_;
        }
        if ((s.faults & FAULT_IGNITION_NOT_PROVEN) && !(previous_.faults & FAULT_IGNITION_NOT_PROVEN)) {
            counters_.failed_ignitions = saturated_add(counters_.failed_ignitions, 1);
        }
        if (!initialized_) reason = StopReason::BOOT;
        else if (s.faults) reason = StopReason::FAULT;
        else if (s.state == State::POST_PURGE && reason == StopReason::NONE) reason = StopReason::THERMOSTAT;
        else if (previous_.state == State::LOCKOUT && !s.faults) reason = StopReason::RESET;
        events_[next_] = {boot_id_, session_id_, now, previous_.state, s.state, reason, s.faults};
        next_ = (next_ + 1) % kEventCapacity;
        count_ = std::min(count_ + 1, kEventCapacity);
    }
    previous_ = s;
    previous_ms_ = now;
    initialized_ = true;
}
void CommandHistory::reset_counters() { counters_ = {}; }
EventRecord CommandHistory::event(size_t index) const {
    return index < count_ ? events_[(next_ + kEventCapacity - count_ + index) % kEventCapacity] : EventRecord{};
}
} // namespace sirokko
