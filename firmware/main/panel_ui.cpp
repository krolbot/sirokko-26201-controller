#include "panel_ui.hpp"

#include <cstddef>
#include <cstdio>
#include <limits>

namespace panel_ui {
namespace {

constexpr uint64_t kMinuteMs = 60ULL * 1000ULL;
constexpr uint64_t kTransientNoticeMs = 2000;
constexpr uint64_t kTimerConfirmationSlackMs = 1500;

uint8_t ab_value(bool a, bool b) {
    return static_cast<uint8_t>((a ? 2U : 0U) | (b ? 1U : 0U));
}

int32_t rounded_minutes(uint64_t milliseconds) {
    const uint64_t rounded = (milliseconds + kMinuteMs - 1U) / kMinuteMs;
    return rounded > static_cast<uint64_t>(kMinutesMaximum) ? kMinutesMaximum : static_cast<int32_t>(rounded);
}

} // namespace

void EncoderInput::reset(bool a, bool b, bool push_pressed, uint64_t now_ms) {
    ab_state_ = ab_value(a, b);
    quarter_turns_ = 0;
    initialized_ = true;
    push_raw_ = push_pressed;
    push_stable_ = push_pressed;
    boot_release_required_ = push_pressed;
    saw_pressed_ = false;
    push_changed_ms_ = now_ms;
}

InputEvent EncoderInput::poll(bool a, bool b, bool push_pressed, uint64_t now_ms) {
    InputEvent event{};
    if (!initialized_) {
        reset(a, b, push_pressed, now_ms);
        return event;
    }

    const uint8_t next_ab = ab_value(a, b);
    if (next_ab != ab_state_) {
        // Legal Gray-code transitions contribute one quarter-turn. Illegal/bounce transitions are zero.
        static constexpr int8_t kTransitions[16] = {
            0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
        };
        quarter_turns_ += kTransitions[(ab_state_ << 2U) | next_ab];
        ab_state_ = next_ab;
        if (quarter_turns_ >= 4) {
            event.turns = 1;
            quarter_turns_ -= 4;
        } else if (quarter_turns_ <= -4) {
            event.turns = -1;
            quarter_turns_ += 4;
        }
    }

    if (push_pressed != push_raw_ || now_ms < push_changed_ms_) {
        push_raw_ = push_pressed;
        push_changed_ms_ = now_ms;
        return event;
    }
    if (push_raw_ == push_stable_ || now_ms - push_changed_ms_ < kEncoderDebounceMs)
        return event;

    push_stable_ = push_raw_;
    if (push_stable_) {
        saw_pressed_ = true;
    } else if (boot_release_required_) {
        boot_release_required_ = false;
        saw_pressed_ = false;
    } else if (saw_pressed_) {
        saw_pressed_ = false;
        event.click = true;
    }
    return event;
}

bool Model::same_settings(const sirokko::Settings &left, const sirokko::Settings &right) {
    return left.room_setpoint_milli_c == right.room_setpoint_milli_c &&
           left.duration_ms == right.duration_ms && left.mode == right.mode &&
           left.manual_percent == right.manual_percent && left.slew_up_per_s == right.slew_up_per_s &&
           left.slew_down_per_s == right.slew_down_per_s;
}

bool Model::settings_changes_match(const sirokko::Settings &baseline, const sirokko::Settings &expected,
                                   const sirokko::Settings &actual) {
    if (same_settings(baseline, expected))
        return same_settings(expected, actual);
    const auto matches = [](auto before, auto after, auto current) {
        return before == after || current == after;
    };
    return matches(baseline.room_setpoint_milli_c, expected.room_setpoint_milli_c,
                   actual.room_setpoint_milli_c) &&
           matches(baseline.duration_ms, expected.duration_ms, actual.duration_ms) &&
           matches(baseline.mode, expected.mode, actual.mode) &&
           matches(baseline.manual_percent, expected.manual_percent, actual.manual_percent) &&
           matches(baseline.slew_up_per_s, expected.slew_up_per_s, actual.slew_up_per_s) &&
           matches(baseline.slew_down_per_s, expected.slew_down_per_s, actual.slew_down_per_s);
}

bool Model::is_active_session(sirokko::State state) {
    return state == sirokko::State::PREPURGE || state == sirokko::State::PREHEAT ||
           state == sirokko::State::IGNITION || state == sirokko::State::VERIFY ||
           state == sirokko::State::RUN || state == sirokko::State::DEBUG;
}

bool Model::is_fault_or_stop(sirokko::State state) {
    return state == sirokko::State::STOP_PURGE || state == sirokko::State::FAULT_PURGE ||
           state == sirokko::State::FAULT;
}

int32_t Model::clamp_step(int32_t value, int turns, int32_t low, int32_t high, int32_t step) {
    const int64_t candidate = static_cast<int64_t>(value) + static_cast<int64_t>(turns) * step;
    if (candidate < low)
        return low;
    if (candidate > high)
        return high;
    return static_cast<int32_t>(candidate);
}

uint64_t Model::minutes_to_ms(int32_t minutes) {
    return static_cast<uint64_t>(minutes < 0 ? 0 : minutes) * kMinuteMs;
}

uint64_t Model::saturating_add(uint64_t left, uint64_t right) {
    return left > std::numeric_limits<uint64_t>::max() - right ? std::numeric_limits<uint64_t>::max()
                                                               : left + right;
}

void Model::advance_clock(uint64_t now_ms) {
    if (now_ms > last_now_ms_)
        last_now_ms_ = now_ms;
}

void Model::bump_interaction_epoch() {
    ++interaction_epoch_;
    if (interaction_epoch_ == 0)
        interaction_epoch_ = 1;
}

void Model::invalidate_pending() {
    pending_click_.active = false;
}

bool Model::presentation_blocked() const {
    const auto state = view_.control.state;
    if ((state != sirokko::State::IDLE && state != sirokko::State::UNCONFIGURED &&
         state != sirokko::State::RUN && state != sirokko::State::FAULT) ||
        view_.control.commissioning.active ||
        view_.control.combustion.phase == sirokko::combustion::Phase::SUSPECT)
        return true;
    if (state == sirokko::State::RUN && view_.settings.mode == sirokko::Mode::TEMPERATURE) {
        const auto &room = view_.sensors.room;
        return !room.valid || room.status != 0 || last_now_ms_ < room.sample_ms ||
               last_now_ms_ - room.sample_ms > sirokko::kRoomSensorStaleMs;
    }
    return false;
}
bool Model::interaction_blocked() const {
    return !view_available_ || command_in_flight_ || confirmation_.active || view_.control.faults != 0 ||
           presentation_blocked();
}

bool Model::reset_fault_available() const {
    return view_available_ && view_.control.state == sirokko::State::FAULT && view_.control.faults != 0 &&
           !command_in_flight_ && !confirmation_.active && queued_command_.kind == CommandKind::NONE;
}

MenuItem Model::menu_item_at(uint8_t index) const {
    switch (index) {
    case 0:
        return MenuItem::MODE;
    case 1:
        return view_.settings.mode == sirokko::Mode::MANUAL ? MenuItem::MANUAL_POWER : MenuItem::TEMPERATURE;
    case 2:
        return MenuItem::SHUTDOWN_TIMER;
    case 3:
        return MenuItem::CALIBRATION;
    case 4:
        return MenuItem::DIAGNOSTICS;
    case 5:
        return MenuItem::WIFI;
    case 6:
        return MenuItem::OTA;
    case 7:
        return MenuItem::BACK;
    default:
        return MenuItem::BACK;
    }
}

void Model::synchronize_menu() {
    if (menu_index_ >= kMenuCount)
        menu_index_ = 0;
    const uint8_t maximum_first = kMenuCount - kVisibleMenuRows;
    if (menu_first_ > maximum_first)
        menu_first_ = maximum_first;
    if (menu_index_ < menu_first_)
        menu_first_ = menu_index_;
    if (menu_index_ >= static_cast<uint8_t>(menu_first_ + kVisibleMenuRows))
        menu_first_ = static_cast<uint8_t>(menu_index_ - kVisibleMenuRows + 1U);
    menu_item_ = menu_item_at(menu_index_);
}

void Model::move_menu(int turns) {
    const int count = static_cast<int>(kMenuCount);
    int selected = static_cast<int>(menu_index_);
    int delta = turns % count;
    selected = (selected + delta) % count;
    if (selected < 0)
        selected += count;
    menu_index_ = static_cast<uint8_t>(selected);
    synchronize_menu();
}

void Model::reset(const View &view) {
    view_ = view;
    initialized_ = true;
    view_available_ = true;
    page_ = Page::HOME;
    menu_item_ = MenuItem::MODE;
    menu_index_ = 0;
    menu_first_ = 0;
    edit_field_ = EditField::NONE;
    edit_return_page_ = Page::HOME;
    timer_target_ = TimerTarget::NONE;
    draft_settings_ = view.settings;
    edit_value_ = 0;
    editing_ = false;
    interaction_epoch_ = 1;
    pending_click_ = {};
    queued_command_ = {};
    queued_context_ = {};
    in_flight_command_ = {};
    in_flight_context_ = {};
    command_in_flight_ = false;
    confirmation_ = {};
    notice_ = Notice::NONE;
    notice_expires_ms_ = 0;
    last_now_ms_ = view.now_ms;
    credentials_ = {};
    synchronize_menu();
}

void Model::set_notice(Notice notice) {
    if (!view_available_ && notice != Notice::VIEW_UNAVAILABLE)
        return;
    notice_ = notice;
    if (notice == Notice::NONE || notice == Notice::QUEUED || notice == Notice::VIEW_UNAVAILABLE) {
        notice_expires_ms_ = 0;
    } else {
        notice_expires_ms_ = saturating_add(last_now_ms_, kTransientNoticeMs);
    }
}

void Model::clear_command_state() {
    queued_command_ = {};
    queued_context_ = {};
    in_flight_command_ = {};
    in_flight_context_ = {};
    command_in_flight_ = false;
    confirmation_ = {};
}

void Model::abort_stale_interaction(bool show_external_change) {
    const bool had_interaction = editing_ || pending_click_.active ||
                                 queued_command_.kind != CommandKind::NONE || command_in_flight_ ||
                                 confirmation_.active;
    invalidate_pending();
    clear_command_state();
    draft_settings_ = view_.settings;
    edit_value_ = 0;
    edit_field_ = EditField::NONE;
    timer_target_ = TimerTarget::NONE;
    editing_ = false;
    page_ = Page::HOME;
    clear_credentials();
    bump_interaction_epoch();
    if (show_external_change && had_interaction)
        set_notice(Notice::EXTERNAL_CHANGE);
}

void Model::update(const View &view) {
    if (!initialized_) {
        reset(view);
        return;
    }

    const View previous = view_;
    const bool recovered = !view_available_;
    const bool state_changed = view.control.state_version != previous.control.state_version;
    const bool settings_changed = !same_settings(view.settings, previous.settings);
    const bool boot_changed = view.control.boot_id != previous.control.boot_id;
    const bool entered_stop_or_fault =
        (view.control.faults != 0 && previous.control.faults == 0) ||
        (!is_fault_or_stop(previous.control.state) && is_fault_or_stop(view.control.state)) ||
        (is_active_session(previous.control.state) && view.control.state == sirokko::State::IDLE &&
         view.control.run_id == 0);

    view_ = view;
    view_available_ = true;
    if (boot_changed)
        last_now_ms_ = view.now_ms;
    else
        advance_clock(view.now_ms);
    synchronize_menu();

    if (recovered && notice_ == Notice::VIEW_UNAVAILABLE)
        set_notice(Notice::NONE);
    if (notice_ == Notice::CALIBRATION_BLOCKED && view_.control.commissioning.block == sirokko::Block::NONE)
        set_notice(Notice::NONE);
    if (page_ == Page::CALIBRATION && view_.control.commissioning.block != sirokko::Block::NONE)
        pending_click_.calibration_ready = false;

    if (boot_changed || entered_stop_or_fault) {
        abort_stale_interaction(true);
        return;
    }

    if (presentation_blocked()) {
        const bool confirmed = confirmation_.active && snapshot_confirms(confirmation_, view_);
        abort_stale_interaction(!confirmed);
        if (confirmed)
            set_notice(Notice::NONE);
        return;
    }

    if (editing_ && edit_conflicts(previous)) {
        cancel_edit();
        set_notice(Notice::EXTERNAL_CHANGE);
        return;
    }

    if ((queued_command_.kind != CommandKind::NONE || command_in_flight_) &&
        (state_changed || settings_changed)) {
        abort_stale_interaction(true);
        return;
    }

    if (state_changed) {
        invalidate_pending();
        bump_interaction_epoch();
    }
    if (confirmation_.active) {
        if (snapshot_confirms(confirmation_, view_)) {
            confirmation_ = {};
            set_notice(Notice::NONE);
        } else {
            expire_confirmation(last_now_ms_);
        }
    }
}

void Model::set_view_unavailable() {
    if (!initialized_)
        reset({});
    view_available_ = false;
    invalidate_pending();
    clear_command_state();
    draft_settings_ = view_.settings;
    edit_value_ = 0;
    edit_field_ = EditField::NONE;
    timer_target_ = TimerTarget::NONE;
    editing_ = false;
    page_ = Page::HOME;
    clear_credentials();
    bump_interaction_epoch();
    notice_ = Notice::VIEW_UNAVAILABLE;
    notice_expires_ms_ = 0;
}

void Model::begin_edit(EditField field, Page return_page) {
    if (interaction_blocked())
        return;
    bump_interaction_epoch();
    invalidate_pending();
    draft_settings_ = view_.settings;
    edit_field_ = field;
    edit_return_page_ = return_page;
    timer_target_ = TimerTarget::NONE;
    editing_ = true;
    page_ = Page::EDIT;

    switch (field) {
    case EditField::MODE:
        edit_value_ = draft_settings_.mode == sirokko::Mode::MANUAL ? 1 : 0;
        break;
    case EditField::MANUAL_POWER:
        edit_value_ = draft_settings_.manual_percent;
        break;
    case EditField::TEMPERATURE:
        edit_value_ = draft_settings_.room_setpoint_milli_c;
        break;
    case EditField::SHUTDOWN_TIMER:
        if (is_active_session(view_.control.state)) {
            timer_target_ = TimerTarget::ACTIVE_SESSION;
            edit_value_ =
                view_.control.duration_active ? rounded_minutes(view_.control.duration_remaining_ms) : 0;
        } else if (view_.control.state == sirokko::State::IDLE ||
                   view_.control.state == sirokko::State::UNCONFIGURED) {
            timer_target_ = TimerTarget::NEXT_START;
            edit_value_ = rounded_minutes(draft_settings_.duration_ms);
        } else {
            finish_edit();
            set_notice(Notice::COMMAND_REJECTED);
        }
        break;
    case EditField::NONE:
        finish_edit();
        break;
    }
}

bool Model::edit_conflicts(const View &previous) const {
    if (view_.control.run_id != previous.control.run_id)
        return true;
    const auto &before = previous.settings;
    const auto &after = view_.settings;
    switch (edit_field_) {
    case EditField::MODE:
    case EditField::MANUAL_POWER:
    case EditField::TEMPERATURE:
        return before.mode != after.mode || before.manual_percent != after.manual_percent ||
               before.room_setpoint_milli_c != after.room_setpoint_milli_c ||
               sirokko::minimum_manual_percent(previous.control.learned_model) !=
                   sirokko::minimum_manual_percent(view_.control.learned_model);
    case EditField::SHUTDOWN_TIMER:
        if (timer_target_ == TimerTarget::NEXT_START)
            return before.duration_ms != after.duration_ms;
        return previous.control.duration_active != view_.control.duration_active ||
               (view_.control.duration_active &&
                saturating_add(previous.now_ms, previous.control.duration_remaining_ms) !=
                    saturating_add(view_.now_ms, view_.control.duration_remaining_ms));
    case EditField::NONE:
        return false;
    }
    return false;
}

void Model::finish_edit() {
    page_ = edit_return_page_;
    edit_field_ = EditField::NONE;
    timer_target_ = TimerTarget::NONE;
    editing_ = false;
    bump_interaction_epoch();
    invalidate_pending();
}

void Model::cancel_edit() {
    if (!editing_)
        return;
    draft_settings_ = view_.settings;
    edit_value_ = 0;
    finish_edit();
}

Model::CommandContext Model::command_context() const {
    return {
        .settings = view_.settings,
        .state_version = view_.control.state_version,
        .boot_id = view_.control.boot_id,
        .run_id = view_.control.run_id,
    };
}

void Model::queue_command(const Command &command) {
    if (command.kind == CommandKind::NONE || queued_command_.kind != CommandKind::NONE)
        return;
    if (command.kind == CommandKind::RESET_FAULT) {
        if (!reset_fault_available())
            return;
    } else if (interaction_blocked()) {
        return;
    }
    queued_command_ = command;
    queued_context_ = command_context();
    set_notice(Notice::NONE);
}

void Model::commit_edit() {
    if (!editing_ || interaction_blocked())
        return;

    // Only this field is edited locally; preserve changes made by the other panel.
    draft_settings_ = view_.settings;
    Command command{};
    switch (edit_field_) {
    case EditField::MODE:
        draft_settings_.mode = edit_value_ == 0 ? sirokko::Mode::TEMPERATURE : sirokko::Mode::MANUAL;
        command.kind = CommandKind::SETTINGS;
        command.settings = draft_settings_;
        break;
    case EditField::MANUAL_POWER:
        draft_settings_.manual_percent = static_cast<uint8_t>(edit_value_);
        command.kind = CommandKind::SETTINGS;
        command.settings = draft_settings_;
        break;
    case EditField::TEMPERATURE:
        draft_settings_.room_setpoint_milli_c = edit_value_;
        command.kind = CommandKind::SETTINGS;
        command.settings = draft_settings_;
        break;
    case EditField::SHUTDOWN_TIMER:
        if (timer_target_ == TimerTarget::ACTIVE_SESSION) {
            command.kind = CommandKind::TIMER;
            command.delay_ms = minutes_to_ms(edit_value_); // Zero cancels only the active shutdown timer.
        } else if (timer_target_ == TimerTarget::NEXT_START) {
            draft_settings_.duration_ms = minutes_to_ms(edit_value_);
            command.kind = CommandKind::SETTINGS;
            command.settings = draft_settings_;
        } else {
            set_notice(Notice::COMMAND_REJECTED);
            return;
        }
        break;
    case EditField::NONE:
        return;
    }

    const bool unchanged_settings =
        command.kind == CommandKind::SETTINGS && same_settings(command.settings, view_.settings);
    finish_edit();
    if (!unchanged_settings)
        queue_command(command);
}

void Model::open_menu_item(MenuItem item) {
    if (interaction_blocked())
        return;
    switch (item) {
    case MenuItem::MODE:
        begin_edit(EditField::MODE, Page::MENU);
        break;
    case MenuItem::MANUAL_POWER:
        begin_edit(EditField::MANUAL_POWER, Page::MENU);
        break;
    case MenuItem::TEMPERATURE:
        begin_edit(EditField::TEMPERATURE, Page::MENU);
        break;
    case MenuItem::SHUTDOWN_TIMER:
        begin_edit(EditField::SHUTDOWN_TIMER, Page::MENU);
        break;
    case MenuItem::CALIBRATION: {
        if (view_.control.commissioning.block != sirokko::Block::NONE) {
            set_notice(Notice::CALIBRATION_BLOCKED);
            break;
        }
        bump_interaction_epoch();
        page_ = Page::CALIBRATION;
        break;
    }
    case MenuItem::DIAGNOSTICS:
        bump_interaction_epoch();
        page_ = Page::DIAGNOSTICS;
        break;
    case MenuItem::WIFI: {
        bump_interaction_epoch();
        clear_credentials();
        page_ = Page::WIFI;
        Command command{};
        command.kind = CommandKind::REQUEST_NETWORK_CREDENTIALS;
        queue_command(command);
        break;
    }
    case MenuItem::OTA:
        bump_interaction_epoch();
        page_ = Page::OTA;
        break;
    case MenuItem::BACK:
        bump_interaction_epoch();
        page_ = Page::HOME;
        break;
    case MenuItem::COUNT:
        break;
    }
}

void Model::rotate(int turns) {
    if (!turns || interaction_blocked())
        return;
    set_notice(Notice::NONE);

    if (page_ == Page::HOME) {
        begin_edit(view_.settings.mode == sirokko::Mode::MANUAL ? EditField::MANUAL_POWER
                                                                : EditField::TEMPERATURE,
                   Page::HOME);
    }

    if (page_ == Page::MENU) {
        bump_interaction_epoch();
        invalidate_pending();
        move_menu(turns);
        return;
    }

    if (page_ != Page::EDIT || !editing_)
        return;
    bump_interaction_epoch();
    invalidate_pending();
    switch (edit_field_) {
    case EditField::MODE:
        edit_value_ = clamp_step(edit_value_, turns, 0, 1, 1);
        break;
    case EditField::MANUAL_POWER:
        edit_value_ =
            clamp_step(edit_value_, turns, sirokko::minimum_manual_percent(view_.control.learned_model),
                       kManualMaximumPercent, sirokko::kManualStepPercent);
        break;
    case EditField::TEMPERATURE:
        edit_value_ = clamp_step(edit_value_, turns, sirokko::kRoomSetpointMinimumMilliC,
                                 sirokko::kRoomSetpointMaximumMilliC, sirokko::kRoomSetpointStepMilliC);
        break;
    case EditField::SHUTDOWN_TIMER:
        edit_value_ = clamp_step(edit_value_, turns, 0, kMinutesMaximum, 1);
        break;
    case EditField::NONE:
        break;
    }
}

void Model::execute_pending_single() {
    const PendingClick pending = pending_click_;
    pending_click_.active = false;
    if (pending.epoch != interaction_epoch_ || pending.page != page_ || pending.edit_field != edit_field_) {
        return;
    }
    if (pending.page == Page::HOME && reset_fault_available()) {
        Command command{};
        command.kind = CommandKind::RESET_FAULT;
        queue_command(command);
        return;
    }
    if (interaction_blocked())
        return;
    if (pending.page == Page::MENU &&
        (pending.menu_index != menu_index_ || pending.menu_item != menu_item_at(menu_index_))) {
        return;
    }

    switch (pending.page) {
    case Page::HOME:
        page_ = Page::MENU;
        synchronize_menu();
        bump_interaction_epoch();
        break;
    case Page::MENU:
        open_menu_item(pending.menu_item);
        break;
    case Page::EDIT:
        commit_edit();
        break;
    case Page::CALIBRATION: {
        if (view_.control.commissioning.block != sirokko::Block::NONE) {
            set_notice(Notice::CALIBRATION_BLOCKED);
            break;
        }
        if (!pending.calibration_ready)
            break;
        Command command{};
        command.kind = CommandKind::CALIBRATE;
        queue_command(command);
        break;
    }
    case Page::DIAGNOSTICS:
    case Page::WIFI:
    case Page::OTA:
        break;
    }
}

void Model::execute_pending_double() {
    const PendingClick pending = pending_click_;
    pending_click_.active = false;
    if (interaction_blocked() || pending.epoch != interaction_epoch_ || pending.page != page_ ||
        pending.edit_field != edit_field_) {
        return;
    }

    switch (pending.page) {
    case Page::EDIT:
        cancel_edit();
        break;
    case Page::MENU:
        page_ = Page::HOME;
        bump_interaction_epoch();
        break;
    case Page::DIAGNOSTICS:
    case Page::OTA:
    case Page::CALIBRATION:
        page_ = Page::MENU;
        synchronize_menu();
        bump_interaction_epoch();
        break;
    case Page::WIFI:
        clear_credentials();
        page_ = Page::MENU;
        synchronize_menu();
        bump_interaction_epoch();
        break;
    case Page::HOME:
        break;
    }
}

void Model::click(uint64_t now_ms) {
    advance_clock(now_ms);
    if (!reset_fault_available() && interaction_blocked())
        return;
    set_notice(Notice::NONE);
    if (pending_click_.active) {
        if (now_ms >= pending_click_.started_ms &&
            now_ms - pending_click_.started_ms <= kDoubleClickWindowMs) {
            execute_pending_double();
            return;
        }
        execute_pending_single();
        if (interaction_blocked())
            return;
    }

    pending_click_.active = true;
    pending_click_.calibration_ready = view_.control.commissioning.block == sirokko::Block::NONE;
    pending_click_.page = page_;
    pending_click_.menu_index = menu_index_;
    pending_click_.menu_item = menu_item_at(menu_index_);
    pending_click_.edit_field = edit_field_;
    pending_click_.started_ms = now_ms;
    pending_click_.epoch = interaction_epoch_;
}

bool Model::snapshot_confirms(const Confirmation &confirmation, const View &view) const {
    if (!confirmation.active || view.control.boot_id != confirmation.context.boot_id ||
        view.control.state_version == confirmation.context.state_version)
        return false;

    switch (confirmation.command.kind) {
    case CommandKind::SETTINGS:
        return settings_changes_match(confirmation.context.settings, confirmation.command.settings,
                                      view.settings);
    case CommandKind::TIMER: {
        if (!is_active_session(view.control.state) || view.control.run_id != confirmation.context.run_id)
            return false;
        if (confirmation.command.delay_ms == 0)
            return !view.control.duration_active;
        if (!view.control.duration_active ||
            view.control.duration_remaining_ms > confirmation.command.delay_ms)
            return false;
        const uint64_t elapsed =
            view.now_ms >= confirmation.started_ms ? view.now_ms - confirmation.started_ms : 0;
        const uint64_t expected_remaining =
            confirmation.command.delay_ms > elapsed ? confirmation.command.delay_ms - elapsed : 0;
        return saturating_add(view.control.duration_remaining_ms, kTimerConfirmationSlackMs) >=
               expected_remaining;
    }
    case CommandKind::CALIBRATE:
        return view.control.commissioning.active && is_active_session(view.control.state) &&
               view.control.run_id != 0;
    case CommandKind::RESET_FAULT:
        return view.control.state == sirokko::State::IDLE && view.control.faults == 0;
    case CommandKind::REQUEST_NETWORK_CREDENTIALS:
        return credentials_.available;
    case CommandKind::NONE:
        return false;
    }
    return false;
}

void Model::begin_confirmation(const Command &command, const CommandContext &context) {
    if (command.kind == CommandKind::REQUEST_NETWORK_CREDENTIALS && credentials_.available) {
        set_notice(Notice::NONE);
        return;
    }

    confirmation_ = {
        .active = true,
        .command = command,
        .context = context,
        .started_ms = last_now_ms_,
        .deadline_ms = saturating_add(last_now_ms_, kCommandConfirmationTimeoutMs),
    };
    set_notice(Notice::QUEUED);
}

void Model::expire_confirmation(uint64_t now_ms) {
    if (!confirmation_.active || now_ms < confirmation_.deadline_ms)
        return;
    const CommandKind kind = confirmation_.command.kind;
    confirmation_ = {};
    if (kind == CommandKind::REQUEST_NETWORK_CREDENTIALS) {
        clear_credentials();
        set_notice(Notice::CREDENTIALS_UNAVAILABLE);
    } else {
        set_notice(Notice::CONFIRMATION_TIMEOUT);
    }
}

void Model::tick(uint64_t now_ms) {
    advance_clock(now_ms);
    if (!view_available_)
        return;
    if (presentation_blocked()) {
        if (notice_expires_ms_ != 0 && last_now_ms_ >= notice_expires_ms_)
            set_notice(Notice::NONE);
        abort_stale_interaction(true);
        return;
    }

    expire_confirmation(last_now_ms_);
    if ((!interaction_blocked() || reset_fault_available()) && pending_click_.active &&
        now_ms >= pending_click_.started_ms && now_ms - pending_click_.started_ms >= kDoubleClickWindowMs) {
        execute_pending_single();
    }
    if (notice_expires_ms_ != 0 && last_now_ms_ >= notice_expires_ms_)
        set_notice(Notice::NONE);
}

bool Model::take_command(Command *command) {
    if (!command || !view_available_ || command_in_flight_ || confirmation_.active ||
        queued_command_.kind == CommandKind::NONE)
        return false;
    *command = queued_command_;
    in_flight_command_ = queued_command_;
    in_flight_context_ = queued_context_;
    queued_command_ = {};
    queued_context_ = {};
    command_in_flight_ = true;
    return true;
}

void Model::command_result(bool accepted) {
    if (!command_in_flight_)
        return;
    command_in_flight_ = false;
    const Command command = in_flight_command_;
    const CommandContext context = in_flight_context_;
    in_flight_command_ = {};
    in_flight_context_ = {};
    if (!accepted) {
        if (command.kind == CommandKind::CALIBRATE) {
            set_notice(Notice::CALIBRATION_BLOCKED);
        } else if (command.kind == CommandKind::REQUEST_NETWORK_CREDENTIALS) {
            clear_credentials();
            set_notice(Notice::CREDENTIALS_UNAVAILABLE);
        } else {
            set_notice(Notice::COMMAND_REJECTED);
        }
        return;
    }
    begin_confirmation(command, context);
}

void Model::clear_credentials() {
    credentials_.available = false;
    credentials_.ssid.fill('\0');
    credentials_.password.fill('\0');
}

void Model::set_credentials(const char *ssid, const char *password) {
    clear_credentials();
    std::snprintf(credentials_.ssid.data(), credentials_.ssid.size(), "%s", ssid ? ssid : "");
    std::snprintf(credentials_.password.data(), credentials_.password.size(), "%s", password ? password : "");
    credentials_.available = credentials_.ssid[0] != '\0';
    if (credentials_.available && confirmation_.active &&
        confirmation_.command.kind == CommandKind::REQUEST_NETWORK_CREDENTIALS) {
        confirmation_ = {};
        set_notice(Notice::NONE);
    } else if (!credentials_.available) {
        clear_credentials();
        set_notice(Notice::CREDENTIALS_UNAVAILABLE);
    }
}

void Model::credentials_unavailable() {
    clear_credentials();
    if (confirmation_.active && confirmation_.command.kind == CommandKind::REQUEST_NETWORK_CREDENTIALS)
        confirmation_ = {};
    set_notice(Notice::CREDENTIALS_UNAVAILABLE);
}

Screen Model::screen() const {
    Screen result{};
    result.page = page_;
    result.menu_item = menu_item_at(menu_index_);
    result.edit_field = edit_field_;
    result.edit_value = edit_value_;
    result.editing = editing_;
    result.pending_click = pending_click_.active;
    result.command_in_flight = command_in_flight_ || confirmation_.active;
    result.notice = notice_;
    result.menu_index = menu_index_;
    result.menu_first = menu_first_;
    result.menu_count = kMenuCount;
    for (uint8_t row = 0; row < kVisibleMenuRows; ++row)
        result.menu_rows[row] = menu_item_at(static_cast<uint8_t>(menu_first_ + row));
    return result;
}

const char *menu_label(MenuItem item) {
    switch (item) {
    case MenuItem::MODE:
        return "Режим";
    case MenuItem::MANUAL_POWER:
        return "Уровень";
    case MenuItem::TEMPERATURE:
        return "Температура";
    case MenuItem::SHUTDOWN_TIMER:
        return "Таймер";
    case MenuItem::CALIBRATION:
        return "Калибровка";
    case MenuItem::DIAGNOSTICS:
        return "Диагностика";
    case MenuItem::WIFI:
        return "Wi-Fi";
    case MenuItem::OTA:
        return "Обновление";
    case MenuItem::BACK:
        return "Назад";
    case MenuItem::COUNT:
        return "";
    }
    return "";
}

} // namespace panel_ui
