#pragma once

#include <array>
#include <cstdint>

#include "sirokko/control.hpp"

namespace panel_ui {

constexpr uint32_t kEncoderDebounceMs = 30;
constexpr uint32_t kDoubleClickWindowMs = 280;
constexpr uint32_t kCommandConfirmationTimeoutMs = 5000;
constexpr int32_t kManualMaximumPercent = 100;
constexpr int32_t kMinutesMaximum = sirokko::kMaximumTimerMs / 60000;

enum class Page : uint8_t { HOME, MENU, EDIT, DIAGNOSTICS, WIFI, OTA };
enum class MenuItem : uint8_t {
    MODE,
    MANUAL_POWER,
    TEMPERATURE,
    SHUTDOWN_TIMER,
    DIAGNOSTICS,
    WIFI,
    OTA,
    BACK,
    COUNT,
};
enum class EditField : uint8_t { NONE, MODE, MANUAL_POWER, TEMPERATURE, SHUTDOWN_TIMER };
enum class CommandKind : uint8_t {
    NONE,
    SETTINGS,
    TIMER,
    RESET_FAULT,
    REQUEST_NETWORK_CREDENTIALS,
};
enum class Notice : uint8_t {
    NONE,
    QUEUED,
    COMMAND_REJECTED,
    CONFIRMATION_TIMEOUT,
    CREDENTIALS_UNAVAILABLE,
    VIEW_UNAVAILABLE,
    EXTERNAL_CHANGE,
};

struct View {
    sirokko::Snapshot control{};
    sirokko::Settings settings{};
    uint64_t now_ms{};
    bool storage_ok{true};
    sirokko::Sensors sensors{};
    sirokko::SafetyEnvelope envelope{};
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
    uint8_t menu_index{};
    uint8_t menu_first{};
    uint8_t menu_count{};
    std::array<MenuItem, 4> menu_rows{};
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
    void reset(const View &view);
    void update(const View &view);
    void set_view_unavailable();

    void rotate(int turns);
    void click(uint64_t now_ms);
    void tick(uint64_t now_ms);

    bool take_command(Command *command);
    void command_result(bool accepted);
    void set_credentials(const char *ssid, const char *password);
    void credentials_unavailable();

    Screen screen() const;
    const View &view() const { return view_; }
    const LocalCredentials &credentials() const { return credentials_; }

  private:
    static constexpr uint8_t kVisibleMenuRows = 4;
    static constexpr uint8_t kMenuCount = 7;

    enum class TimerTarget : uint8_t { NONE, ACTIVE_SESSION, NEXT_START };

    struct PendingClick {
        bool active{};
        Page page{Page::HOME};
        uint8_t menu_index{};
        MenuItem menu_item{MenuItem::MODE};
        EditField edit_field{EditField::NONE};
        uint64_t started_ms{};
        uint64_t epoch{};
    };

    struct CommandContext {
        sirokko::Settings settings{};
        uint64_t state_version{};
        uint64_t boot_id{};
        uint64_t run_id{};
    };

    struct Confirmation {
        bool active{};
        Command command{};
        CommandContext context{};
        uint64_t started_ms{};
        uint64_t deadline_ms{};
    };

    static bool same_settings(const sirokko::Settings &left, const sirokko::Settings &right);
    static bool settings_changes_match(const sirokko::Settings &baseline, const sirokko::Settings &expected,
                                       const sirokko::Settings &actual);
    static bool is_active_session(sirokko::State state);
    static bool is_fault_or_stop(sirokko::State state);
    static int32_t clamp_step(int32_t value, int turns, int32_t low, int32_t high, int32_t step);
    static uint64_t minutes_to_ms(int32_t minutes);
    static uint64_t saturating_add(uint64_t left, uint64_t right);

    void advance_clock(uint64_t now_ms);
    void bump_interaction_epoch();
    void invalidate_pending();
    bool presentation_blocked() const;
    bool interaction_blocked() const;
    bool reset_fault_available() const;
    void synchronize_menu();
    MenuItem menu_item_at(uint8_t index) const;
    void move_menu(int turns);

    void begin_edit(EditField field, Page return_page);
    bool edit_conflicts(const View &previous) const;
    void cancel_edit();
    void finish_edit();
    void commit_edit();
    void open_menu_item(MenuItem item);
    void execute_pending_single();
    void execute_pending_double();

    CommandContext command_context() const;
    void queue_command(const Command &command);
    void clear_command_state();
    void abort_stale_interaction(bool show_external_change);
    void begin_confirmation(const Command &command, const CommandContext &context);
    bool snapshot_confirms(const Confirmation &confirmation, const View &view) const;
    void expire_confirmation(uint64_t now_ms);
    void set_notice(Notice notice);

    void clear_credentials();

    View view_{};
    bool initialized_{};
    bool view_available_{};
    Page page_{Page::HOME};
    MenuItem menu_item_{MenuItem::MODE};
    uint8_t menu_index_{};
    uint8_t menu_first_{};
    EditField edit_field_{EditField::NONE};
    Page edit_return_page_{Page::HOME};
    TimerTarget timer_target_{TimerTarget::NONE};
    sirokko::Settings draft_settings_{};
    int32_t edit_value_{};
    bool editing_{};
    uint64_t interaction_epoch_{};
    PendingClick pending_click_{};
    Command queued_command_{};
    CommandContext queued_context_{};
    Command in_flight_command_{};
    CommandContext in_flight_context_{};
    bool command_in_flight_{};
    Confirmation confirmation_{};
    Notice notice_{Notice::NONE};
    uint64_t notice_expires_ms_{};
    uint64_t last_now_ms_{};
    LocalCredentials credentials_{};
};

const char *menu_label(MenuItem item);

} // namespace panel_ui
