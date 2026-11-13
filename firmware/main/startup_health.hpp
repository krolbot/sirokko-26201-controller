#pragma once
#include <cstdint>
namespace sirokko {
struct StartupHealth {
    bool heater_configured{};
    bool outputs_initialized{};
    bool storage_ok{};
    bool button_ready{};
    bool panel_ready{};
    bool network_ready{};
    uint64_t now_ms{};
    uint64_t last_control_ms{};
    bool control_started{};
    bool sensors_initialized{};
    bool control_alive() const {
        return control_started && now_ms >= last_control_ms && now_ms - last_control_ms <= 100;
    }
    bool local_controls_initialized() const { return button_ready && panel_ready; }
    bool controller_ready() const { return outputs_initialized && storage_ok && control_alive(); }
    bool can_confirm_image() const {
        return controller_ready() && network_ready &&
               (!heater_configured || (sensors_initialized && local_controls_initialized()));
    }
};
} // namespace sirokko
