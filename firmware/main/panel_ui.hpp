#pragma once

#include <array>
#include <cstdint>

#include "sirokko/control.hpp"

namespace panel_ui {

constexpr uint32_t kEncoderDebounceMs = 30;
constexpr uint32_t kDoubleClickWindowMs = 280;
constexpr int32_t kSetpointMinimumMilliC = 15000;
constexpr int32_t kSetpointMaximumMilliC = 30000;
constexpr int32_t kSetpointStepMilliC = 1000;
constexpr int32_t kManualMinimumPercent = 5;
constexpr int32_t kManualMaximumPercent = 100;
constexpr int32_t kManualStepPercent = 5;
constexpr int32_t kMinutesMaximum = 10080;

enum class Page : uint8_t { HOME, MENU, EDIT, DIAGNOSTICS, WIFI, OTA };
enum class MenuItem : uint8_t {
    MODE,
    MANUAL_POWER,
    TEMPERATURE,
    SHUTDOWN_TIMER,
    START_DURATION,
    DELAYED_START,
    CALIBRATION,
    DIAGNOSTICS,
    WIFI,
    OTA,
    BACK,
    COUNT,
};
enum class EditField : uint8_t { NONE, MODE, MANUAL_POWER, TEMPERATURE, SHUTDOWN_TIMER, START_DURATION, DELAYED_START };
enum class CommandKind : uint8_t {
    NONE,
    SETTINGS,
    TIMER,
    ARM_DELAYED,
    CANCEL_ARM,
    CALIBRATE,
    REQUEST_NETWORK_CREDENTIALS,
};
enum class Notice : uint8_t {
    NONE,
    QUEUED,
    COMMAND_REJECTED,
    CREDENTIALS_UNAVAILABLE,
    VIEW_UNAVAILABLE,
    EXTERNAL_CHANGE,
};

struct View {
    sirokko::Snapshot control{};
    sirokko::Settings settings{};
    uint64_t now_ms{};
    bool storage_ok{true};
};

struct Command {
    CommandKind kind{CommandKind::NONE};
    sirokko::Settings settings{};
    uint64_t delay_ms{};
};

// This stays in RAM in the local panel task. Nothing in panel_ui logs or serializes it.
struct LocalCredentials {
    bool available{};
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
};

struct Screen {
    Page page{Page::HOME};
    MenuItem menu_item{MenuItem::MODE};
    EditField edit_field{EditField::NONE};
    int32_t edit_value{};
    bool editing{};
    bool pending_click{};
    bool command_in_flight{};
    Notice notice{Notice::NONE};
};

struct InputEvent {
    int turns{};
    bool click{};
};

// Portable EC11 decoder: A/B transitions are accumulated to a detent; PUSH emits a click on
// stable release. A button held during reset is consumed until it has been released once.
class EncoderInput {
public:
    void reset(bool a, bool b, bool push_pressed, uint64_t now_ms);
    InputEvent poll(bool a, bool b, bool push_pressed, uint64_t now_ms);

private:
    uint8_t ab_state_{};
    int quarter_turns_{};
    bool initialized_{};
    bool push_raw_{};
    bool push_stable_{};
    bool boot_release_required_{};
    bool saw_pressed_{};
    uint64_t push_changed_ms_{};
};

class Model {
public:
    void reset(const View& view);
    void update(const View& view);
    void set_view_unavailable();

    void rotate(int turns);
    void click(uint64_t now_ms);
    void tick(uint64_t now_ms);

    bool take_command(Command* command);
    void command_result(bool accepted);
    void set_credentials(const char* ssid, const char* password);
    void credentials_unavailable();

    Screen screen() const;
    const View& view() const { return view_; }
    const LocalCredentials& credentials() const { return credentials_; }

private:
    struct PendingClick {
        bool active{};
        Page page{Page::HOME};
        MenuItem menu_item{MenuItem::MODE};
        EditField edit_field{EditField::NONE};
        uint64_t started_ms{};
        uint64_t epoch{};
    };

    static bool same_settings(const sirokko::Settings& left, const sirokko::Settings& right);
    static int32_t clamp_step(int32_t value, int turns, int32_t low, int32_t high, int32_t step);
    static uint64_t minutes_to_ms(int32_t minutes);

    void invalidate_pending();
    void begin_edit(EditField field, Page return_page);
    void cancel_edit();
    void commit_edit();
    void open_menu_item(MenuItem item);
    void execute_pending_single();
    void execute_pending_double();
    void queue_command(const Command& command);
    void copy_credentials(std::array<char, 33>* target, const char* source);
    void copy_credentials(std::array<char, 65>* target, const char* source);

    View view_{};
    bool initialized_{};
    Page page_{Page::HOME};
    MenuItem menu_item_{MenuItem::MODE};
    EditField edit_field_{EditField::NONE};
    Page edit_return_page_{Page::HOME};
    sirokko::Settings draft_settings_{};
    int32_t edit_value_{};
    bool editing_{};
    uint64_t interaction_epoch_{};
    PendingClick pending_click_{};
    Command queued_command_{};
    bool command_in_flight_{};
    Notice notice_{Notice::NONE};
    LocalCredentials credentials_{};
};

const char* menu_label(MenuItem item);
const char* edit_label(EditField field);
const char* notice_label(Notice notice);

}  // namespace panel_ui
