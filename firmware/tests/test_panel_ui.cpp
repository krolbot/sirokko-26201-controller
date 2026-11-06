#include "panel_ui.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace panel_ui;
using namespace sirokko;

namespace {

View baseline() {
    View view{};
    view.control.state = State::IDLE;
    view.control.start_block = Block::NONE;
    view.control.state_version = 10;
    view.settings.room_setpoint_milli_c = 15000;
    view.settings.mode = Mode::TEMPERATURE;
    view.settings.manual_percent = 50;
    view.settings.duration_ms = 0;
    return view;
}

void open_menu(Model& model, uint64_t& now) {
    model.click(now);
    assert(model.screen().page == Page::HOME && model.screen().pending_click);
    now += kDoubleClickWindowMs;
    model.tick(now);
    assert(model.screen().page == Page::MENU);
}

Command take(Model& model) {
    Command command{};
    assert(model.take_command(&command));
    return command;
}

void test_home_single_click_is_deferred_then_opens_menu() {
    Model model;
    model.reset(baseline());
    uint64_t now = 100;
    open_menu(model, now);
    assert(model.screen().menu_item == MenuItem::MODE);
}

void test_home_edit_single_saves_and_double_cancels_before_save() {
    Model save_model;
    save_model.reset(baseline());
    uint64_t now = 0;
    save_model.rotate(1);
    assert(save_model.screen().page == Page::EDIT);
    assert(save_model.screen().edit_field == EditField::TEMPERATURE);
    assert(save_model.screen().edit_value == 16000);
    save_model.click(now);
    now += kDoubleClickWindowMs;
    save_model.tick(now);
    const Command saved = take(save_model);
    assert(saved.kind == CommandKind::SETTINGS);
    assert(saved.settings.room_setpoint_milli_c == 16000);
    save_model.command_result(true);

    Model cancel_model;
    cancel_model.reset(baseline());
    now = 0;
    cancel_model.rotate(1);
    cancel_model.click(now);
    cancel_model.click(now + kDoubleClickWindowMs - 1);
    assert(cancel_model.screen().page == Page::HOME);
    assert(!cancel_model.screen().editing);
    Command absent{};
    assert(!cancel_model.take_command(&absent));
}

void test_menu_back_and_timer_cancel() {
    Model model;
    model.reset(baseline());
    uint64_t now = 0;
    open_menu(model, now);

    model.click(now);
    model.click(now + 1);
    assert(model.screen().page == Page::HOME);

    open_menu(model, now += 1000);
    model.rotate(static_cast<int>(MenuItem::SHUTDOWN_TIMER));
    assert(model.screen().menu_item == MenuItem::SHUTDOWN_TIMER);
    model.click(now);
    now += kDoubleClickWindowMs;
    model.tick(now);
    assert(model.screen().page == Page::EDIT && model.screen().edit_field == EditField::SHUTDOWN_TIMER);
    assert(model.screen().edit_value == 0);
    model.click(now);
    now += kDoubleClickWindowMs;
    model.tick(now);
    const Command timer = take(model);
    assert(timer.kind == CommandKind::TIMER && timer.delay_ms == 0);
    model.command_result(true);
}

void test_delayed_cancel_and_calibration_error() {
    View armed = baseline();
    armed.control.delayed_armed = true;
    armed.control.delayed_remaining_ms = 15ULL * 60ULL * 1000ULL;
    Model cancel;
    cancel.reset(armed);
    uint64_t now = 0;
    open_menu(cancel, now);
    cancel.rotate(static_cast<int>(MenuItem::DELAYED_START));
    cancel.click(now);
    now += kDoubleClickWindowMs;
    cancel.tick(now);
    const Command cancel_arm = take(cancel);
    assert(cancel_arm.kind == CommandKind::CANCEL_ARM && cancel_arm.delay_ms == 0);
    cancel.command_result(true);

    Model calibration;
    calibration.reset(baseline());
    now = 0;
    open_menu(calibration, now);
    calibration.rotate(static_cast<int>(MenuItem::CALIBRATION));
    calibration.click(now);
    now += kDoubleClickWindowMs;
    calibration.tick(now);
    const Command start_calibration = take(calibration);
    assert(start_calibration.kind == CommandKind::CALIBRATE);
    calibration.command_result(false);
    assert(calibration.screen().notice == Notice::COMMAND_REJECTED);
}

void test_external_state_change_invalidates_pending_save() {
    Model model;
    View view = baseline();
    model.reset(view);
    uint64_t now = 0;
    model.rotate(1);
    model.click(now);
    view.control.state_version++;
    view.control.state = State::RUN;
    model.update(view);
    model.tick(now + kDoubleClickWindowMs + 1);
    Command command{};
    assert(!model.take_command(&command));
    assert(model.screen().notice == Notice::EXTERNAL_CHANGE);
}

void test_encoder_debounce_boot_hold_and_quadrature() {
    EncoderInput input;
    input.reset(false, false, true, 0);  // PUSH held at boot.
    assert(!input.poll(false, false, false, 1).click);
    assert(!input.poll(false, false, false, kEncoderDebounceMs + 1).click);

    // Press bounce is discarded; only the following stable press/release makes one click.
    assert(!input.poll(false, false, true, 40).click);
    assert(!input.poll(false, false, false, 50).click);
    assert(!input.poll(false, false, true, 60).click);
    assert(!input.poll(false, false, true, 60 + kEncoderDebounceMs).click);
    assert(!input.poll(false, false, false, 100).click);
    assert(input.poll(false, false, false, 100 + kEncoderDebounceMs).click);

    input.reset(false, false, false, 0);
    int turns = 0;
    turns += input.poll(false, true, false, 1).turns;
    turns += input.poll(true, true, false, 2).turns;
    turns += input.poll(true, false, false, 3).turns;
    turns += input.poll(false, false, false, 4).turns;
    assert(turns == 1 || turns == -1);
}

}  // namespace

int main() {
    test_home_single_click_is_deferred_then_opens_menu();
    test_home_edit_single_saves_and_double_cancels_before_save();
    test_menu_back_and_timer_cancel();
    test_delayed_cancel_and_calibration_error();
    test_external_state_change_invalidates_pending_save();
    test_encoder_debounce_boot_hold_and_quadrature();
    std::cout << "PASS: panel menu FSM, deferred single/double click, timer cancel, calibration error and EC11 debounce\n";
}
