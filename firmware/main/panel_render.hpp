#pragma once

#include <cstddef>
#include <cstdint>
#include "panel_ui.hpp"

// Portable presentation only. No GPIO, timers, command submission, heap or RTOS calls.
namespace panel_render {
constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kStripeRows = 16;
constexpr uint32_t kTransitionMs = 200;
constexpr uint32_t kMarkerMs = 180;
constexpr uint32_t kMenuMs = 140;
constexpr uint32_t kFanPeriodMs = 2000;
constexpr uint32_t kHeatPulseCycleMs = 2400;
constexpr uint32_t kHeatPulseTravelMs = 1900;

enum class Scene : uint8_t {
    Heat, Idle, Manual, EditTemperature, EditManual, TimerRunning, Above,
    Prepurge, Preheat, Ignition, Verify, Cooldown, Recovery,
    FaultCooling, FaultLatched, SensorMissing, Calibrate, CalCooling, CalSaved,
    CalReference, CalRepeat, CalStore, CalCancelled,
    Menu, Mode, TimerEdit, Diagnostics, Wifi, Ota, MenuService, TimerOff,
    Maintenance, NoData, Blocked, Queued, Rejected, CommandRejected,
};

bool sample_fresh(const sirokko::SensorSample& sample, uint64_t now_ms, uint64_t maximum_age_ms);
Scene select_scene(const panel_ui::Screen& screen, const panel_ui::View& view,
                   uint64_t now_ms, bool show_saved = true);
const char* scene_id(Scene scene);
bool urgent(Scene scene);
void format_temperature(char* out, size_t capacity, int32_t milli_c, bool units = true);
void format_duration(char* out, size_t capacity, uint64_t milliseconds);

struct Frame {
    panel_ui::Screen screen{};
    panel_ui::View view{};
    panel_ui::LocalCredentials credentials{};
    Scene scene{Scene::NoData};
    uint64_t at_ms{};
    float target{};  // graphical value only; the numeric setpoint is never interpolated
    float current{};
    float menu_row{};
};

class Renderer {
public:
    void reset();
    void update(const panel_ui::Screen& screen, const panel_ui::View& view,
                const panel_ui::LocalCredentials& credentials, uint64_t now_ms);
    // Freeze one update for the entire stripe sequence. Buffers each hold width*rows RGB565 pixels.
    bool draw_band(int y, int rows, uint16_t* pixels, uint16_t* scratch) const;
    const Frame& frame() const { return frame_; }
    bool transitioning() const { return transition_ < 1.0f; }
    bool animated() const;
    float transition_fraction() const { return transition_; }
private:
    bool initialized_{};
    Frame frame_{};
    Frame previous_{};
    uint64_t transition_started_{};
    uint64_t target_started_{};
    uint64_t current_started_{};
    uint64_t menu_started_{};
    uint64_t saved_started_{};
    bool saved_seen_{};
    float target_from_{}, target_to_{};
    float current_from_{}, current_to_{};
    float menu_from_{}, menu_to_{};
    float transition_{1.0f};
};
}  // namespace panel_render
