#pragma once

#include <atomic>
#include <cstdint>

#include "board.hpp"
#include "sirokko/control.hpp"

inline constexpr uint32_t local_display_startup_fault(bool initialized) {
    return initialized ? sirokko::FAULT_NONE : sirokko::FAULT_LOCAL_UI;
}

class LocalDisplay {
public:
    bool begin();
    bool render(int32_t setpoint_milli_c,
                const sirokko::SensorSample& room,
                const sirokko::SensorSample& exhaust,
                const sirokko::SensorSample& hot_air,
                const sirokko::Status& status);
    bool healthy() const { return healthy_; }

private:
    bool submit_band(uint8_t band);
    void compose_band(uint8_t band,
                      int32_t setpoint_milli_c,
                      const sirokko::SensorSample& room,
                      const sirokko::SensorSample& exhaust,
                      const sirokko::SensorSample& hot_air,
                      const sirokko::Status& status);

    void* io_{};
    void* panel_{};
    uint16_t* pixels_{};
    std::atomic<bool> transfer_in_progress_{};
    int64_t transfer_started_us_{};
    uint8_t next_band_{};
    bool healthy_{};
};
