#include "panel_ui.hpp"

#include <cstddef>

namespace panel_ui {
namespace {

constexpr uint64_t kMinuteMs = 60ULL * 1000ULL;

uint8_t ab_value(bool a, bool b) {
    return static_cast<uint8_t>((a ? 2U : 0U) | (b ? 1U : 0U));
}

int32_t rounded_minutes(uint64_t milliseconds) {
    const uint64_t rounded = (milliseconds + kMinuteMs - 1U) / kMinuteMs;
    return rounded > static_cast<uint64_t>(kMinutesMaximum) ? kMinutesMaximum : static_cast<int32_t>(rounded);
}

}  // namespace

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
            0, -1, 1, 0,
            1, 0, 0, -1,
            -1, 0, 0, 1,
            0, 1, -1, 0,
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
    if (push_raw_ == push_stable_ || now_ms - push_changed_ms_ < kEncoderDebounceMs) return event;

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

bool Model::same_settings(const sirokko::Settings& left, const sirokko::Settings& right) {
    return left.room_setpoint_milli_c == right.room_setpoint_milli_c &&
           left.room_hysteresis_milli_c == right.room_hysteresis_milli_c &&
           left.duration_ms == right.duration_ms && left.mode == right.mode &&
           left.manual_percent == right.manual_percent && left.slew_up_per_s == right.slew_up_per_s &&
           left.slew_down_per_s == right.slew_down_per_s;
}

int32_t Model::clamp_step(int32_t value, int turns, int32_t low, int32_t high, int32_t step) {
    const int64_t candidate = static_cast<int64_t>(value) + static_cast<int64_t>(turns) * step;
    if (candidate < low) return low;
    if (candidate > high) return high;
    return static_cast<int32_t>(candidate);
}

uint64_t Model::minutes_to_ms(int32_t minutes) {
    return static_cast<uint64_t>(minutes < 0 ? 0 : minutes) * kMinuteMs;
}

void Model::reset(const View& view) {
    view_ = view;
    initialized_ = true;
    page_ = Page::HOME;
    menu_item_ = MenuItem::MODE;
    edit_field_ = EditField::NONE;
    edit_return_page_ = Page::HOME;
    draft_settings_ = view.settings;
    edit_value_ = 0;
    editing_ = false;
    interaction_epoch_ = 1;
    pending_click_ = {};
    queued_command_ = {};
    command_in_flight_ = false;
    notice_ = Notice::NONE;
    credentials_ = {};
}

void Model::invalidate_pending() {
    pending_click_.active = false;
}

void Model::update(const View& view) {
    if (!initialized_) {
        reset(view);
        return;
    }

    const bool state_changed = view.control.state_version != view_.control.state_version;
    const bool settings_changed = !same_settings(view.settings, view_.settings);
    const bool fault_raised = view.control.faults != 0 && view_.control.faults == 0;
    view_ = view;

    if (!state_changed && !settings_changed && !fault_raised) return;
    ++interaction_epoch_;
    invalidate_pending();
    if (editing_) cancel_edit();
    if (!command_in_flight_) notice_ = Notice::EXTERNAL_CHANGE;
}

void Model::set_view_unavailable() {
    if (!command_in_flight_) notice_ = Notice::VIEW_UNAVAILABLE;
}

void Model::begin_edit(EditField field, Page return_page) {
    ++interaction_epoch_;
    invalidate_pending();
    draft_settings_ = view_.settings;
    edit_field_ = field;
    edit_return_page_ = return_page;
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
            edit_value_ = view_.control.duration_active ? rounded_minutes(view_.control.duration_remaining_ms) : 0;
            break;
        case EditField::START_DURATION:
            edit_value_ = rounded_minutes(draft_settings_.duration_ms);
            break;
        case EditField::DELAYED_START:
            edit_value_ = view_.control.delayed_armed ? rounded_minutes(view_.control.delayed_remaining_ms) : 1;
            if (edit_value_ < 1) edit_value_ = 1;
            break;
        case EditField::NONE:
            editing_ = false;
            page_ = return_page;
            break;
    }
}

void Model::cancel_edit() {
    draft_settings_ = view_.settings;
    page_ = edit_return_page_;
    edit_field_ = EditField::NONE;
    editing_ = false;
    ++interaction_epoch_;
    invalidate_pending();
}

void Model::queue_command(const Command& command) {
    if (command.kind == CommandKind::NONE || queued_command_.kind != CommandKind::NONE || command_in_flight_) return;
    queued_command_ = command;
    notice_ = Notice::NONE;
}

void Model::commit_edit() {
    if (!editing_) return;

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
            command.kind = CommandKind::TIMER;
            command.delay_ms = minutes_to_ms(edit_value_);  // 0 is the explicit timer-cancel command.
            break;
        case EditField::START_DURATION:
            draft_settings_.duration_ms = minutes_to_ms(edit_value_);
            command.kind = CommandKind::SETTINGS;
            command.settings = draft_settings_;
            break;
        case EditField::DELAYED_START:
            command.kind = CommandKind::ARM_DELAYED;
            command.delay_ms = minutes_to_ms(edit_value_);
            break;
        case EditField::NONE:
            return;
    }

    page_ = edit_return_page_;
    edit_field_ = EditField::NONE;
    editing_ = false;
    ++interaction_epoch_;
    queue_command(command);
}

void Model::open_menu_item(MenuItem item) {
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
        case MenuItem::START_DURATION:
            begin_edit(EditField::START_DURATION, Page::MENU);
            break;
        case MenuItem::DELAYED_START:
            if (view_.control.delayed_armed) {
                Command command{};
                command.kind = CommandKind::CANCEL_ARM;
                queue_command(command);
            } else {
                begin_edit(EditField::DELAYED_START, Page::MENU);
            }
            break;
        case MenuItem::CALIBRATION: {
            Command command{};
            command.kind = CommandKind::CALIBRATE;
            queue_command(command);
            break;
        }
        case MenuItem::DIAGNOSTICS:
            ++interaction_epoch_;
            page_ = Page::DIAGNOSTICS;
            break;
        case MenuItem::WIFI: {
            ++interaction_epoch_;
            page_ = Page::WIFI;
            Command command{};
            command.kind = CommandKind::REQUEST_NETWORK_CREDENTIALS;
            queue_command(command);
            break;
        }
        case MenuItem::OTA:
            ++interaction_epoch_;
            page_ = Page::OTA;
            break;
        case MenuItem::BACK:
            ++interaction_epoch_;
            page_ = Page::HOME;
            break;
        case MenuItem::COUNT:
            break;
    }
}

void Model::rotate(int turns) {
    if (!turns || command_in_flight_) return;
    ++interaction_epoch_;
    invalidate_pending();

    if (page_ == Page::HOME) {
        begin_edit(view_.settings.mode == sirokko::Mode::MANUAL ? EditField::MANUAL_POWER : EditField::TEMPERATURE,
                   Page::HOME);
    }

    if (page_ == Page::MENU) {
        const int count = static_cast<int>(MenuItem::COUNT);
        int selected = static_cast<int>(menu_item_);
        selected = (selected + turns) % count;
        if (selected < 0) selected += count;
        menu_item_ = static_cast<MenuItem>(selected);
        return;
    }

    if (page_ != Page::EDIT || !editing_) return;
    switch (edit_field_) {
        case EditField::MODE:
            edit_value_ = clamp_step(edit_value_, turns, 0, 1, 1);
            break;
        case EditField::MANUAL_POWER:
            edit_value_ = clamp_step(edit_value_, turns, kManualMinimumPercent, kManualMaximumPercent,
                                     kManualStepPercent);
            break;
        case EditField::TEMPERATURE:
            edit_value_ = clamp_step(edit_value_, turns, kSetpointMinimumMilliC, kSetpointMaximumMilliC,
                                     kSetpointStepMilliC);
            break;
        case EditField::SHUTDOWN_TIMER:
        case EditField::START_DURATION:
            edit_value_ = clamp_step(edit_value_, turns, 0, kMinutesMaximum, 1);
            break;
        case EditField::DELAYED_START:
            edit_value_ = clamp_step(edit_value_, turns, 1, kMinutesMaximum, 1);
            break;
        case EditField::NONE:
            break;
    }
}

void Model::execute_pending_single() {
    const PendingClick pending = pending_click_;
    pending_click_.active = false;
    if (pending.epoch != interaction_epoch_ || pending.page != page_ ||
        pending.menu_item != menu_item_ || pending.edit_field != edit_field_) {
        notice_ = Notice::EXTERNAL_CHANGE;
        return;
    }

    switch (pending.page) {
        case Page::HOME:
            page_ = Page::MENU;
            ++interaction_epoch_;
            break;
        case Page::MENU:
            open_menu_item(pending.menu_item);
            break;
        case Page::EDIT:
            commit_edit();
            break;
        case Page::DIAGNOSTICS:
        case Page::WIFI:
        case Page::OTA:
            break;
    }
}

void Model::execute_pending_double() {
    const PendingClick pending = pending_click_;
    pending_click_.active = false;
    if (pending.epoch != interaction_epoch_ || pending.page != page_ ||
        pending.menu_item != menu_item_ || pending.edit_field != edit_field_) {
        notice_ = Notice::EXTERNAL_CHANGE;
        return;
    }

    switch (pending.page) {
        case Page::EDIT:
            cancel_edit();
            break;
        case Page::MENU:
            page_ = Page::HOME;
            ++interaction_epoch_;
            break;
        case Page::DIAGNOSTICS:
        case Page::WIFI:
        case Page::OTA:
            page_ = Page::MENU;
            ++interaction_epoch_;
            break;
        case Page::HOME:
            // HOME intentionally has no double-click action.
            break;
    }
}

void Model::click(uint64_t now_ms) {
    if (command_in_flight_) return;
    if (pending_click_.active) {
        if (now_ms >= pending_click_.started_ms && now_ms - pending_click_.started_ms <= kDoubleClickWindowMs) {
            execute_pending_double();
            return;
        }
        execute_pending_single();
    }

    pending_click_.active = true;
    pending_click_.page = page_;
    pending_click_.menu_item = menu_item_;
    pending_click_.edit_field = edit_field_;
    pending_click_.started_ms = now_ms;
    pending_click_.epoch = interaction_epoch_;
}

void Model::tick(uint64_t now_ms) {
    if (pending_click_.active && now_ms >= pending_click_.started_ms &&
        now_ms - pending_click_.started_ms >= kDoubleClickWindowMs) {
        execute_pending_single();
    }
}

bool Model::take_command(Command* command) {
    if (!command || command_in_flight_ || queued_command_.kind == CommandKind::NONE) return false;
    *command = queued_command_;
    queued_command_ = {};
    command_in_flight_ = true;
    return true;
}

void Model::command_result(bool accepted) {
    if (!command_in_flight_) return;
    command_in_flight_ = false;
    notice_ = accepted ? Notice::QUEUED : Notice::COMMAND_REJECTED;
}

void Model::copy_credentials(std::array<char, 33>* target, const char* source) {
    if (!target) return;
    target->fill(0);
    if (!source) return;
    size_t index = 0;
    while (index + 1 < target->size() && source[index]) {
        (*target)[index] = source[index];
        ++index;
    }
}

void Model::copy_credentials(std::array<char, 65>* target, const char* source) {
    if (!target) return;
    target->fill(0);
    if (!source) return;
    size_t index = 0;
    while (index + 1 < target->size() && source[index]) {
        (*target)[index] = source[index];
        ++index;
    }
}

void Model::set_credentials(const char* ssid, const char* password) {
    copy_credentials(&credentials_.ssid, ssid);
    copy_credentials(&credentials_.password, password);
    credentials_.available = credentials_.ssid[0] != '\0' && credentials_.password[0] != '\0';
    if (!credentials_.available) notice_ = Notice::CREDENTIALS_UNAVAILABLE;
}

void Model::credentials_unavailable() {
    credentials_ = {};
    notice_ = Notice::CREDENTIALS_UNAVAILABLE;
}

Screen Model::screen() const {
    return {
        .page = page_,
        .menu_item = menu_item_,
        .edit_field = edit_field_,
        .edit_value = edit_value_,
        .editing = editing_,
        .pending_click = pending_click_.active,
        .command_in_flight = command_in_flight_,
        .notice = notice_,
    };
}

const char* menu_label(MenuItem item) {
    switch (item) {
        case MenuItem::MODE: return "РЕЖИМ";
        case MenuItem::MANUAL_POWER: return "РУЧНАЯ МОЩНОСТЬ";
        case MenuItem::TEMPERATURE: return "ТЕМПЕРАТУРА";
        case MenuItem::SHUTDOWN_TIMER: return "ВЫКЛЮЧИТЬ ЧЕРЕЗ";
        case MenuItem::START_DURATION: return "ДЛИТЕЛЬНОСТЬ ПУСКА";
        case MenuItem::DELAYED_START: return "ОТЛОЖЕННЫЙ ПУСК";
        case MenuItem::CALIBRATION: return "АВТОКАЛИБРОВКА";
        case MenuItem::DIAGNOSTICS: return "ДИАГНОСТИКА";
        case MenuItem::WIFI: return "WI-FI";
        case MenuItem::OTA: return "OTA: БРАУЗЕР";
        case MenuItem::BACK: return "НАЗАД";
        case MenuItem::COUNT: return "";
    }
    return "";
}

const char* edit_label(EditField field) {
    switch (field) {
        case EditField::MODE: return "РЕЖИМ";
        case EditField::MANUAL_POWER: return "РУЧНАЯ МОЩНОСТЬ";
        case EditField::TEMPERATURE: return "ТЕМПЕРАТУРА";
        case EditField::SHUTDOWN_TIMER: return "ВЫКЛЮЧИТЬ ЧЕРЕЗ";
        case EditField::START_DURATION: return "ДЛИТЕЛЬНОСТЬ ПУСКА";
        case EditField::DELAYED_START: return "ОТЛОЖЕННЫЙ ПУСК";
        case EditField::NONE: return "";
    }
    return "";
}

const char* notice_label(Notice notice) {
    switch (notice) {
        case Notice::NONE: return "";
        case Notice::QUEUED: return "ОЧЕРЕДЬ";
        case Notice::COMMAND_REJECTED: return "КОМАНДА ОТКЛОНЕНА";
        case Notice::CREDENTIALS_UNAVAILABLE: return "НЕТ WI-FI";
        case Notice::VIEW_UNAVAILABLE: return "НЕТ ДАННЫХ";
        case Notice::EXTERNAL_CHANGE: return "СОСТОЯНИЕ НОВОЕ";
    }
    return "";
}

}  // namespace panel_ui
