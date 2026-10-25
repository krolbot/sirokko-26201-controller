#pragma once
#include "sirokko/operator.hpp"
#include "sirokko/display_geometry.hpp"

namespace sirokko {
enum class UiPage : uint8_t { HOME, SETTINGS, SETPOINT, HYSTERESIS, ROOM_OFFSET, DIAGNOSTICS, PROFILE, COUNTERS, RESET_COUNTERS, VERSION };
struct UiKeys {
    bool up_press{};
    bool down_press{};
    bool up_held{};
    bool down_held{};
    bool stop{};
};
struct UiAction {
    bool settings_changed{};
    bool reset_counters{};
};
// START/RESET are deliberately absent; the control adapter always handles them independently.
class LocalMenu {
public:
    UiAction tick(const UiKeys&, uint32_t now_ms, OperatorSettings&, const Config&);
    UiPage page() const { return page_; }
    uint8_t selection() const { return selection_; }
private:
    UiPage page_{UiPage::HOME};
    uint8_t selection_{};
    uint32_t chord_since_{};
    uint32_t last_activity_{};
    bool chord_active_{};
    bool chord_fired_{};
    bool initialized_{};
};
struct UiFrame {
    char firmware_version[32]{};
    Status status{};
    Config config{};
    OperatorSettings settings{};
    SensorSample raw_room{};
    SensorSample room{};
    SensorSample exhaust{};
    SensorSample hot_air{};
    CommandCounters counters{};
    EventRecord last_event{};
    uint32_t now_ms{};
    uint32_t boot_id{};
    uint32_t session_id{};
    uint32_t profile_token{};
    uint32_t settings_revision{};
    uint8_t exhaust_fault{};
    uint8_t hot_air_fault{};
    bool safety_ok{};
    bool stop_active{};
    bool settings_ok{};
    bool storage_ok{};
    bool settings_saved{true};
    bool network_configured{};
    bool network_connected{};
    bool update_active{};
    UiPage page{UiPage::HOME};
    uint8_t selection{};
};
// Same big-endian RGB565 bands go to esp_lcd and host images; no independent mock UI.
void render_ui_band(uint16_t* pixels, uint8_t band, const UiFrame&);
const char* ui_state_label(State);
const char* ui_fault_label(uint32_t);
} // namespace sirokko
