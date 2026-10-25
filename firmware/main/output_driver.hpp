#pragma once

#include "esp_err.h"
#include "sirokko/control.hpp"

// Single owner: main control task. CLI edits never reconfigure this live adapter.
class OutputDriver {
public:
    bool initialize(const sirokko::Config& config);
    bool apply(const sirokko::Outputs& outputs);
    bool healthy() const { return ready_; }
    // GPIO API success, not relay/contact feedback. Allows local repair of an invalid profile
    // only after fail() has independently deasserted every command and detached GPIO17.
    bool shutdown_applied() const { return shutdown_applied_; }
    uint16_t applied_speed() const { return applied_speed_; }
    bool arm_applied() const { return ready_ && arm_applied_; }
    const sirokko::Outputs& applied() const { return applied_; }

private:
    bool ready_{};
    bool started_{};
    bool channel_configured_{};
    bool failure_reported_{};
    bool shutdown_applied_{};
    bool stop_low_requested_{}, arm_applied_{};
    sirokko::Outputs applied_{};
    uint16_t applied_speed_{};
    bool fail(esp_err_t error);
    bool set_speed(uint16_t permille);
};
