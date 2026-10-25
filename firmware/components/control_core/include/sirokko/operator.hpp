#pragma once

#include <cstddef>
#include <cstdint>
#include "sirokko/control.hpp"

namespace sirokko {
inline constexpr uint32_t kOperatorSchema = 1;
inline constexpr int32_t kHysteresisMinMilliC = 500;
inline constexpr int32_t kHysteresisMaxMilliC = 10000;
inline constexpr int32_t kRoomOffsetLimitMilliC = 10000;

struct OperatorSettings {
    uint32_t schema{kOperatorSchema};
    int32_t setpoint_milli_c{20000};
    int32_t hysteresis_milli_c{2000};
    int32_t room_offset_milli_c{};
};
bool operator_settings_valid(const OperatorSettings&, const Config&);
SensorSample calibrated_room(const SensorSample& raw, int32_t offset_milli_c, uint32_t now_ms,
                             uint32_t stale_ms);

enum class RoomZone : uint8_t { INVALID, BELOW, BAND, ABOVE };
RoomZone room_zone(const SensorSample&, const OperatorSettings&, uint32_t now_ms, uint32_t stale_ms);

struct CommandCounters {
    uint64_t starts{};
    uint64_t failed_ignitions{};
    uint64_t run_ms{};
    uint64_t glow_ms{};
};
enum class StopReason : uint8_t { NONE, LOCAL_STOP, REMOTE_STOP, THERMOSTAT, FAULT, RESET, BOOT };
struct EventRecord {
    uint32_t boot_id{};
    uint32_t session_id{};
    uint64_t uptime_ms{};
    State from{State::BOOT_SELF_TEST};
    State to{State::BOOT_SELF_TEST};
    StopReason reason{StopReason::NONE};
    uint32_t faults{};
};
inline constexpr size_t kEventCapacity = 24;
class CommandHistory {
public:
    void begin(uint32_t boot_id, const CommandCounters& restored = {});
    void observe(const Status&, uint64_t now_ms, StopReason reason = StopReason::NONE);
    void reset_counters();
    const CommandCounters& counters() const { return counters_; }
    size_t count() const { return count_; }
    EventRecord event(size_t oldest_index) const;
    uint32_t session_id() const { return session_id_; }
private:
    CommandCounters counters_{};
    EventRecord events_[kEventCapacity]{};
    size_t next_{};
    size_t count_{};
    uint32_t boot_id_{};
    uint32_t session_id_{};
    uint64_t previous_ms_{};
    Status previous_{};
    bool initialized_{};
};
} // namespace sirokko
