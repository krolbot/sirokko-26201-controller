#include "panel_ui.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace panel_ui;
using namespace sirokko;

namespace {

constexpr uint64_t kMinuteMs = 60ULL * 1000ULL;

View baseline() {
    View view{};
    view.control.state = State::IDLE;
    view.control.start_block = Block::NONE;
    view.control.state_version = 10;
    view.control.boot_id = 1;
    view.settings.room_setpoint_milli_c = 20000;
    view.settings.mode = Mode::TEMPERATURE;
    view.settings.manual_percent = 50;
    view.settings.duration_ms = 0;
    view.now_ms = 100;
    view.sensors.room.valid = true;
    view.sensors.room.milli_c = 19500;
    view.process.qualified = true;
    return view;
}

void wait_single(Model& model, uint64_t& now) {
    model.click(now);
    assert(model.screen().pending_click);
    now += kDoubleClickWindowMs;
    model.tick(now);
}

void open_menu(Model& model, uint64_t& now) {
    wait_single(model, now);
    assert(model.screen().page == Page::MENU);
}

Command take(Model& model) {
    Command command{};
    assert(model.take_command(&command));
    return command;
}

void select_menu(Model& model, uint8_t index, uint64_t& now) {
    const Screen before = model.screen();
    assert(before.page == Page::MENU);
    model.rotate(static_cast<int>(index) - static_cast<int>(before.menu_index));
    assert(model.screen().menu_index == index);
    wait_single(model, now);
}

void test_menu_is_exactly_eight_items_and_scrolls() {
    Model model;
    model.reset(baseline());
    uint64_t now = 100;
    open_menu(model, now);

    Screen screen = model.screen();
    assert(screen.menu_count == 8);
    assert(screen.menu_index == 0 && screen.menu_first == 0);
    assert(screen.menu_rows[0] == MenuItem::MODE);
    assert(screen.menu_rows[1] == MenuItem::TEMPERATURE);
    assert(screen.menu_rows[2] == MenuItem::SHUTDOWN_TIMER);
    assert(screen.menu_rows[3] == MenuItem::CALIBRATION);
    assert(std::strcmp(menu_label(MenuItem::MODE), "Режим") == 0);
    assert(std::strcmp(menu_label(MenuItem::SHUTDOWN_TIMER), "Таймер") == 0);

    model.rotate(5);
    screen = model.screen();
    assert(screen.menu_index == 5 && screen.menu_first == 2);
    assert(screen.menu_item == MenuItem::WIFI);
    assert(screen.menu_rows[0] == MenuItem::SHUTDOWN_TIMER);
    assert(screen.menu_rows[1] == MenuItem::CALIBRATION);
    assert(screen.menu_rows[2] == MenuItem::DIAGNOSTICS);
    assert(screen.menu_rows[3] == MenuItem::WIFI);

    model.rotate(2);
    screen = model.screen();
    assert(screen.menu_index == 7 && screen.menu_first == 4);
    assert(screen.menu_item == MenuItem::BACK);
    assert(screen.menu_rows[0] == MenuItem::DIAGNOSTICS);
    assert(screen.menu_rows[1] == MenuItem::WIFI);
    assert(screen.menu_rows[2] == MenuItem::OTA);
    assert(screen.menu_rows[3] == MenuItem::BACK);

    model.rotate(1);
    screen = model.screen();
    assert(screen.menu_index == 0 && screen.menu_first == 0);
    assert(screen.menu_rows[1] == MenuItem::TEMPERATURE);

    View manual = baseline();
    manual.settings.mode = Mode::MANUAL;
    model.reset(manual);
    now = 100;
    open_menu(model, now);
    screen = model.screen();
    assert(screen.menu_count == 8);
    assert(screen.menu_rows[1] == MenuItem::MANUAL_POWER);
    assert(std::strcmp(menu_label(screen.menu_rows[1]), "Уровень") == 0);
}

void test_home_edit_confirms_only_after_matching_snapshot_and_double_cancels() {
    Model model;
    View view = baseline();
    model.reset(view);
    uint64_t now = 100;

    model.rotate(1);
    assert(model.screen().page == Page::EDIT);
    assert(model.screen().edit_field == EditField::TEMPERATURE);
    assert(model.screen().edit_value == 21000);
    wait_single(model, now);
    const Command command = take(model);
    assert(command.kind == CommandKind::SETTINGS);
    assert(command.settings.room_setpoint_milli_c == 21000);
    model.command_result(true);
    assert(model.screen().notice == Notice::QUEUED);
    assert(model.screen().command_in_flight);

    View acknowledged = view;
    acknowledged.now_ms = now + 20;
    acknowledged.control.state_version++;
    acknowledged.settings.room_setpoint_milli_c = 21000;
    model.update(acknowledged);
    assert(model.screen().notice == Notice::NONE);
    assert(!model.screen().command_in_flight);
    assert(model.view().sensors.room.valid && model.view().process.qualified);

    Model manual_model;
    View manual = baseline();
    manual.settings.mode = Mode::MANUAL;
    manual.settings.manual_percent = 50;
    manual_model.reset(manual);
    manual_model.rotate(1);
    assert(manual_model.screen().edit_field == EditField::MANUAL_POWER);
    assert(manual_model.screen().edit_value == 55);
    manual_model.rotate(100);
    assert(manual_model.screen().edit_value == kManualMaximumPercent);
    manual_model.rotate(-100);
    assert(manual_model.screen().edit_value == kManualMinimumPercent);

    Model cancel_model;
    cancel_model.reset(baseline());
    now = 100;
    cancel_model.rotate(1);
    cancel_model.click(now);
    cancel_model.click(now + kDoubleClickWindowMs - 1);
    assert(cancel_model.screen().page == Page::HOME);
    assert(!cancel_model.screen().editing);
    Command absent{};
    assert(!cancel_model.take_command(&absent));
}

void test_external_snapshot_cancels_an_edit_but_normal_state_progress_does_not_stick() {
    Model editing;
    View view = baseline();
    editing.reset(view);
    editing.rotate(1);
    assert(editing.screen().editing);

    View external = view;
    external.now_ms += 100;
    external.control.state = State::RUN;
    external.control.run_id = 4;
    external.control.state_version++;
    editing.update(external);
    assert(!editing.screen().editing);
    assert(editing.screen().page == Page::HOME);
    assert(editing.screen().notice == Notice::EXTERNAL_CHANGE);
    editing.tick(external.now_ms + kCommandConfirmationTimeoutMs);
    assert(editing.screen().notice == Notice::NONE);

    Model passive;
    passive.reset(view);
    passive.update(external);
    assert(passive.screen().notice == Notice::NONE);
}

void test_single_shutdown_timer_uses_timer_in_run_and_settings_in_idle() {
    View running = baseline();
    running.control.state = State::RUN;
    running.control.run_id = 77;
    running.control.duration_active = true;
    running.control.duration_remaining_ms = 2 * kMinuteMs;

    Model active_model;
    active_model.reset(running);
    uint64_t now = 100;
    open_menu(active_model, now);
    select_menu(active_model, 2, now);
    assert(active_model.screen().page == Page::EDIT);
    assert(active_model.screen().edit_field == EditField::SHUTDOWN_TIMER);
    assert(active_model.screen().edit_value == 2);
    active_model.rotate(1);
    wait_single(active_model, now);
    const Command timer = take(active_model);
    assert(timer.kind == CommandKind::TIMER);
    assert(timer.delay_ms == 3 * kMinuteMs);
    active_model.command_result(true);
    assert(active_model.screen().notice == Notice::QUEUED);

    View timer_ack = running;
    timer_ack.now_ms = now + 100;
    timer_ack.control.state_version++;
    timer_ack.control.duration_active = true;
    timer_ack.control.duration_remaining_ms = 3 * kMinuteMs - 100;
    active_model.update(timer_ack);
    assert(active_model.screen().notice == Notice::NONE);

    Model cancel_timer;
    cancel_timer.reset(running);
    now = 100;
    open_menu(cancel_timer, now);
    select_menu(cancel_timer, 2, now);
    cancel_timer.rotate(-2);
    assert(cancel_timer.screen().edit_value == 0);
    wait_single(cancel_timer, now);
    const Command cancel = take(cancel_timer);
    assert(cancel.kind == CommandKind::TIMER && cancel.delay_ms == 0);
    cancel_timer.command_result(true);
    View cancel_ack = running;
    cancel_ack.now_ms = now + 50;
    cancel_ack.control.state_version++;
    cancel_ack.control.duration_active = false;
    cancel_ack.control.duration_remaining_ms = 0;
    cancel_timer.update(cancel_ack);
    assert(cancel_timer.screen().notice == Notice::NONE);

    Model idle_model;
    View idle = baseline();
    idle_model.reset(idle);
    now = 100;
    open_menu(idle_model, now);
    select_menu(idle_model, 2, now);
    assert(idle_model.screen().page == Page::EDIT);
    assert(idle_model.screen().edit_value == 0);
    idle_model.rotate(7);
    wait_single(idle_model, now);
    const Command stored_duration = take(idle_model);
    assert(stored_duration.kind == CommandKind::SETTINGS);
    assert(stored_duration.settings.duration_ms == 7 * kMinuteMs);
    idle_model.command_result(true);
    assert(idle_model.screen().notice == Notice::QUEUED);
    View idle_ack = idle;
    idle_ack.now_ms = now + 20;
    idle_ack.control.state_version++;
    idle_ack.settings.duration_ms = 7 * kMinuteMs;
    idle_model.update(idle_ack);
    assert(idle_model.screen().notice == Notice::NONE);
}

void test_queued_is_not_success_and_times_out_without_matching_snapshot() {
    Model model;
    View view = baseline();
    model.reset(view);
    uint64_t now = 100;
    model.rotate(1);
    wait_single(model, now);
    const Command command = take(model);
    assert(command.kind == CommandKind::SETTINGS);
    model.command_result(true);
    assert(model.screen().notice == Notice::QUEUED);
    assert(model.screen().command_in_flight);

    model.tick(now + kCommandConfirmationTimeoutMs - 1);
    assert(model.screen().notice == Notice::QUEUED);
    model.tick(now + kCommandConfirmationTimeoutMs);
    assert(model.screen().notice == Notice::CONFIRMATION_TIMEOUT);
    assert(!model.screen().command_in_flight);
}

void test_view_loss_blocks_old_work_and_recovery_clears_unavailable() {
    Model model;
    View view = baseline();
    model.reset(view);
    uint64_t now = 100;
    model.rotate(1);
    wait_single(model, now);  // Leaves a not-yet-submitted settings command in the model.
    model.set_view_unavailable();
    assert(model.screen().notice == Notice::VIEW_UNAVAILABLE);
    assert(model.screen().page == Page::HOME);
    assert(!model.screen().editing && !model.screen().pending_click);
    Command absent{};
    assert(!model.take_command(&absent));

    View recovered = view;
    recovered.now_ms = now + 1000;
    recovered.control.state_version++;
    model.update(recovered);
    assert(model.screen().notice == Notice::NONE);
    open_menu(model, now += 1100);
    assert(model.screen().page == Page::MENU);
}

void test_stop_fault_and_boot_cancel_stale_confirmations_and_clicks() {
    Model stop_model;
    View running = baseline();
    running.control.state = State::RUN;
    running.control.run_id = 12;
    stop_model.reset(running);
    uint64_t now = 100;
    stop_model.rotate(1);
    wait_single(stop_model, now);
    assert(take(stop_model).kind == CommandKind::SETTINGS);
    stop_model.command_result(true);
    assert(stop_model.screen().notice == Notice::QUEUED);

    View stopped = running;
    stopped.now_ms = now + 10;
    stopped.control.state = State::STOP_PURGE;
    stopped.control.state_version++;
    stopped.control.run_id = 0;
    stopped.settings.room_setpoint_milli_c = 21000;  // A matching value must not confirm through STOP.
    stop_model.update(stopped);
    assert(stop_model.screen().page == Page::HOME);
    assert(stop_model.screen().notice == Notice::EXTERNAL_CHANGE);
    assert(!stop_model.screen().command_in_flight);

    View later = stopped;
    later.now_ms++;
    later.control.state = State::IDLE;
    later.control.state_version++;
    stop_model.update(later);
    assert(stop_model.screen().notice == Notice::EXTERNAL_CHANGE);

    Model fault_model;
    fault_model.reset(running);
    now = 100;
    open_menu(fault_model, now);
    select_menu(fault_model, 2, now);
    fault_model.rotate(1);
    wait_single(fault_model, now);
    assert(take(fault_model).kind == CommandKind::TIMER);
    fault_model.command_result(true);
    View faulted = running;
    faulted.now_ms = now + 10;
    faulted.control.state = State::FAULT_PURGE;
    faulted.control.faults = FAULT_AIR;
    faulted.control.state_version++;
    fault_model.update(faulted);
    assert(fault_model.screen().page == Page::HOME);
    assert(fault_model.screen().notice == Notice::EXTERNAL_CHANGE);
    assert(!fault_model.screen().command_in_flight);
    fault_model.click(now + 20);
    fault_model.tick(now + 20 + kDoubleClickWindowMs);
    assert(fault_model.screen().page == Page::HOME);

    Model boot_model;
    View idle = baseline();
    boot_model.reset(idle);
    now = 100;
    boot_model.click(now);  // Deferred HOME click must not outlive a new boot.
    View rebooted = idle;
    rebooted.now_ms = now + 10;
    rebooted.control.boot_id++;
    rebooted.control.state_version = 1;
    boot_model.update(rebooted);
    boot_model.tick(now + kDoubleClickWindowMs + 1);
    assert(boot_model.screen().page == Page::HOME);
    assert(!boot_model.screen().pending_click);

    boot_model.rotate(1);
    wait_single(boot_model, now += 1000);
    assert(boot_model.screen().page == Page::HOME);
    Command queued{};
    assert(boot_model.take_command(&queued));
    boot_model.command_result(true);
    View second_boot = rebooted;
    second_boot.now_ms = now + 1;
    second_boot.control.boot_id++;
    second_boot.control.state_version = 1;
    second_boot.settings.room_setpoint_milli_c = queued.settings.room_setpoint_milli_c;
    boot_model.update(second_boot);
    assert(boot_model.screen().notice == Notice::EXTERNAL_CHANGE);
    assert(!boot_model.screen().command_in_flight);
}

void test_hidden_phase_cannot_accept_inputs_or_replay_a_click() {
    for(State state: {State::RECOVERY_PURGE,State::PREPURGE,State::PREHEAT,State::IGNITION,State::VERIFY,State::MAINTENANCE}) {
        Model model;View view=baseline();view.control.state=state;model.reset(view);
        model.rotate(1);model.click(100);model.tick(400);
        assert(model.screen().page==Page::HOME&&!model.screen().pending_click);
        Command absent{};assert(!model.take_command(&absent));
    }
    Model model;View view=baseline();model.reset(view);model.click(100);
    view.control.state=State::PREPURGE;++view.control.state_version;view.now_ms=120;model.update(view);
    assert(!model.screen().pending_click);
    view.control.state=State::RUN;view.control.run_id=1;++view.control.state_version;view.now_ms=500;
    view.sensors.room.sample_ms=500;model.update(view);model.tick(600);
    assert(model.screen().page==Page::HOME);
    model.click(600);view.sensors.room.valid=false;view.now_ms=620;model.update(view);
    model.rotate(1);model.tick(1000);assert(model.screen().page==Page::HOME&&!model.screen().pending_click);
    view.sensors.room.valid=true;view.sensors.room.sample_ms=1100;view.now_ms=1100;model.update(view);model.tick(1200);
    assert(model.screen().page==Page::HOME);
    model.rotate(1);assert(model.screen().page==Page::EDIT&&model.screen().notice==Notice::NONE);
    // Calibration is shown from its real state; entering its full-screen phase consumes confirmation.
    model.reset(baseline());uint64_t now=100;open_menu(model,now);select_menu(model,3,now);
    assert(take(model).kind==CommandKind::CALIBRATE);model.command_result(true);
    view=baseline();view.control.state=State::PREPURGE;view.control.calibration_active=true;
    view.control.run_id=2;++view.control.state_version;view.now_ms=now+20;model.update(view);
    assert(model.screen().page==Page::HOME&&!model.screen().command_in_flight&&model.screen().notice==Notice::NONE);
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
    test_menu_is_exactly_eight_items_and_scrolls();
    test_home_edit_confirms_only_after_matching_snapshot_and_double_cancels();
    test_external_snapshot_cancels_an_edit_but_normal_state_progress_does_not_stick();
    test_single_shutdown_timer_uses_timer_in_run_and_settings_in_idle();
    test_queued_is_not_success_and_times_out_without_matching_snapshot();
    test_view_loss_blocks_old_work_and_recovery_clears_unavailable();
    test_stop_fault_and_boot_cancel_stale_confirmations_and_clicks();
    test_hidden_phase_cannot_accept_inputs_or_replay_a_click();
    test_encoder_debounce_boot_hold_and_quadrature();
    std::cout << "PASS: panel UI menu, single shutdown timer, snapshot confirmation and stale-command safety\n";
}
