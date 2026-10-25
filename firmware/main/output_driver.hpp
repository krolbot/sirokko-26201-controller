#pragma once

#include "esp_err.h"
#include "sirokko/control.hpp"

// Single owner: main control task. CLI edits never reconfigure this live adapter.
class OutputDriver {
public:
    bool initialize(const sirokko::Config& config);
    bool apply(const sirokko::Outputs& outputs);
    bool healthy() const { return ready_; }
    uint16_t applied_speed() const { return applied_speed_; }

private:
    bool ready_{};
    bool started_{};
    bool channel_configured_{};
    bool failure_reported_{};
    uint16_t applied_speed_{};
    bool fail(esp_err_t error);
    bool set_speed(uint16_t permille);
};
