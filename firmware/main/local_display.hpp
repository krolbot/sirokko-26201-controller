#pragma once

#include <cstdint>

#include "sirokko/control.hpp"

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
    void* bus_{};
    void* device_{};
    bool healthy_{};
};
