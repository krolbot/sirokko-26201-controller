#pragma once
#include "sirokko/control.hpp"
#include "json_builder.hpp"
#include <algorithm>

constexpr size_t kHistoryCapacity = 360;
constexpr uint64_t kHistoryPeriodMs = 5000;
struct HistoryPoint {
    uint64_t at_ms{};
    int32_t exhaust{}, air{}, room{};
    uint32_t faults{};
    uint16_t speed{};
    uint8_t valid{};
    sirokko::State state{};
};
struct HistorySnapshot {
    uint64_t boot_id{};
    size_t count{};
    std::array<HistoryPoint, kHistoryCapacity> points{};
};
static_assert(sizeof(HistorySnapshot) < 16384);

// RAM only. Both the writer and snapshot copy are guarded by the runtime history mutex.
class ThermalHistory {
  public:
    void record(uint64_t now, const sirokko::Sensors &sensors, const sirokko::Snapshot &control) {
        if (boot_id_ != control.boot_id || (count_ && now < last_ms_)) {
            count_ = next_ = 0;
            boot_id_ = control.boot_id;
        }
        HistoryPoint p{};
        p.at_ms = now;
        p.exhaust = sensors.exhaust.milli_c;
        p.air = sensors.air.milli_c;
        p.room = sensors.room.milli_c;
        p.faults = control.faults;
        p.speed = control.outputs.speed_code;
        p.state = control.state;
        unsigned bit = 1;
        for (const auto *sample : {&sensors.exhaust, &sensors.air, &sensors.room}) {
            const auto age = bit == 4 ? sirokko::kRoomSensorStaleMs : sirokko::kSensorStaleMs;
            if (sample->valid && !sample->status && sample->sample_ms <= now &&
                now - sample->sample_ms <= age)
                p.valid |= bit;
            bit <<= 1;
        }
        if (count_) {
            const auto &previous = points_[(next_ + kHistoryCapacity - 1) % kHistoryCapacity];
            if (now - last_ms_ < kHistoryPeriodMs && p.state == previous.state &&
                p.faults == previous.faults && p.valid == previous.valid)
                return;
            if (now == last_ms_) {
                points_[(next_ + kHistoryCapacity - 1) % kHistoryCapacity] = p;
                return;
            }
        }
        points_[next_] = p;
        next_ = (next_ + 1) % kHistoryCapacity;
        count_ = std::min(count_ + 1, kHistoryCapacity);
        last_ms_ = now;
    }
    void copy(HistorySnapshot *out) const {
        out->boot_id = boot_id_;
        out->count = count_;
        for (size_t i = 0; i < count_; ++i)
            out->points[i] = points_[(next_ + kHistoryCapacity - count_ + i) % kHistoryCapacity];
    }

  private:
    std::array<HistoryPoint, kHistoryCapacity> points_{};
    size_t count_{}, next_{};
    uint64_t boot_id_{}, last_ms_{};
};

inline void append_history_point(JsonBuilder *json, const HistoryPoint &p) {
    json->character('[');
    json->unsigned_integer(p.at_ms);
    unsigned bit = 1;
    for (const auto value : {p.exhaust, p.air, p.room}) {
        json->character(',');
        if (p.valid & bit)
            json->integer(value);
        else
            json->raw("null");
        bit <<= 1;
    }
    json->character(',');
    json->unsigned_integer(p.speed);
    json->character(',');
    json->quoted(sirokko::state_name(p.state));
    json->character(',');
    json->unsigned_integer(p.faults);
    json->character(']');
}
