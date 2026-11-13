#include "panel_ui.hpp"
#include "panel_render.hpp"
#include "panel_damage.hpp"
#include "control_fixture.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
using namespace panel_ui;
using namespace sirokko;
void press(Model &panel, View &view) {
    view.now_ms += 300;
    panel.click(view.now_ms);
    panel.tick(view.now_ms + kDoubleClickWindowMs);
    view.now_ms += kDoubleClickWindowMs;
}
void timer_editor(Model &panel, View &view) {
    press(panel, view);
    panel.rotate(2);
    press(panel, view);
    assert(panel.screen().edit_field == EditField::SHUTDOWN_TIMER);
}
void synchronization() {
    using namespace control_fixture;
    Controller controller(envelope(), Settings{}, 1, Startup::CLEAN);
    View view{};
    view.now_ms = 100;
    auto refresh = [&] {
        view.control = controller.snapshot(view.now_ms);
        view.settings = controller.settings();
        view.envelope = controller.safety_envelope();
        view.sensors = input(view.now_ms).sensors;
    };
    refresh();
    Model panel;
    panel.reset(view);
    panel.rotate(1);
    const auto target = panel.screen().edit_value;
    auto web = controller.settings();
    web.duration_ms = 120000;
    web.slew_up_per_s = .001;
    assert(controller.update_settings(web));
    refresh();
    panel.update(view);
    assert(panel.screen().editing && panel.screen().edit_value == target);
    press(panel, view);
    Command command{};
    assert(panel.take_command(&command));
    assert(command.settings.duration_ms == 120000 && command.settings.slew_up_per_s == .001);
    panel.command_result(true);
    panel.update(view);
    assert(panel.screen().notice == Notice::QUEUED);
    assert(controller.update_settings(command.settings));
    refresh();
    panel.update(view);
    assert(panel.screen().notice == Notice::NONE && view.settings.room_setpoint_milli_c == target);

    // A conflicting edit is discarded; it must not overwrite the other client.
    panel.rotate(1);
    web = controller.settings();
    web.room_setpoint_milli_c = 26000;
    assert(controller.update_settings(web));
    refresh();
    panel.update(view);
    assert(!panel.screen().editing && panel.screen().notice == Notice::EXTERNAL_CHANGE);
    assert(!panel.take_command(&command));

    panel.reset(view);
    timer_editor(panel, view);
    panel.rotate(1);
    web = controller.settings();
    web.room_setpoint_milli_c = 25000;
    assert(controller.update_settings(web));
    refresh();
    panel.update(view);
    assert(panel.screen().editing && panel.screen().edit_value == 3);
    press(panel, view);
    assert(panel.take_command(&command));
    assert(command.settings.duration_ms == 180000 && command.settings.room_setpoint_milli_c == 25000);

    // The encoder and the controller use the same configured minimum.
    web.mode = Mode::MANUAL;
    assert(controller.update_settings(web));
    refresh();
    panel.reset(view);
    panel.rotate(-100);
    assert(panel.screen().edit_value == minimum_manual_percent(envelope()));
    press(panel, view);
    assert(panel.take_command(&command));
    panel.command_result(true);
    assert(controller.update_settings(command.settings));
    refresh();
    panel.update(view);
    assert(panel.screen().notice == Notice::NONE);

    auto running = ignition();
    confirm_ignition(running);
    auto in = input(kRunAt + 20, 50000, 30000);
    in.set_shutdown_timer = true;
    in.shutdown_after_ms = 600000;
    view.control = running.tick(in);
    view.settings = running.settings();
    view.sensors = in.sensors;
    view.now_ms = in.now_ms;
    assert(view.control.state == State::RUN);
    panel.reset(view);
    timer_editor(panel, view);
    panel.rotate(1);
    view.now_ms += 100;
    view.control = running.snapshot(view.now_ms);
    panel.update(view);
    assert(panel.screen().editing && panel.screen().edit_value == 11);
    in = input(view.now_ms, 50000, 30000);
    in.set_shutdown_timer = true;
    in.shutdown_after_ms = 120000;
    view.control = running.tick(in);
    panel.update(view);
    assert(!panel.screen().editing && panel.screen().notice == Notice::EXTERNAL_CHANGE);
    assert(!panel.take_command(&command));

    panel.reset(view);
    panel.rotate(1);
    view.control.boot_id++;
    panel.update(view);
    assert(!panel.screen().editing && !panel.take_command(&command));
}
} // namespace

int main() {
    synchronization();
    using namespace panel_ui;
    using namespace sirokko;
    View view{};
    view.now_ms = 100;
    view.control.state = State::IDLE;
    view.control.start_block = Block::NONE;
    view.control.boot_id = 1;
    view.sensors.room = {20000, 100, 0, true};
    view.settings.room_setpoint_milli_c = 20000;
    View unconfigured = view;
    unconfigured.control.state = State::UNCONFIGURED;
    unconfigured.control.start_block = Block::ENVELOPE_MISSING;
    Model preview;
    preview.reset(unconfigured);
    assert(panel_render::select_scene(preview.screen(), unconfigured, 100) == panel_render::Scene::Blocked);
    preview.rotate(1);
    assert(preview.screen().editing);
    Model ui;
    ui.reset(view);
    ui.rotate(100);
    assert(ui.screen().edit_value == kRoomSetpointMaximumMilliC);
    ui.rotate(-100);
    assert(ui.screen().edit_value == kRoomSetpointMinimumMilliC);
    ui.rotate(1);
    ui.click(110);
    ui.tick(400);
    Command command{};
    assert(ui.take_command(&command) && command.kind == CommandKind::SETTINGS);
    assert(command.settings.room_setpoint_milli_c == 16000);
    assert(!ui.take_command(&command));
    ui.command_result(true);
    assert(ui.screen().notice == Notice::QUEUED);
    view.now_ms = 420;
    view.control.state_version++;
    view.settings.room_setpoint_milli_c = 16000;
    // An unrelated setting may change while this edit is being confirmed.
    view.settings.duration_ms = 60000;
    ui.update(view);
    assert(ui.screen().notice == Notice::NONE);
    ui.rotate(1);
    ui.click(430);
    view.now_ms = 440;
    view.control.state = State::STOP_PURGE;
    view.control.state_version++;
    ui.update(view);
    ui.tick(1000);
    assert(!ui.screen().editing && !ui.take_command(&command));

    ui.set_credentials("Sirokko", "12345678");
    assert(ui.credentials().available);
    ui.set_view_unavailable();
    assert(!ui.credentials().available && ui.credentials().password[0] == 0);
    ui.rotate(10);
    ui.click(1200);
    ui.tick(1500);
    assert(!ui.take_command(&command));

    EncoderInput encoder;
    // The first service item opens diagnostics without issuing a heating command.
    view.control.state = State::IDLE;
    view.control.faults = 0;
    Model first_run;
    first_run.reset(view);
    press(first_run, view);
    first_run.rotate(3);
    press(first_run, view);
    assert(first_run.screen().page == Page::DIAGNOSTICS && !first_run.take_command(&command));
    view.control.combustion.phase = combustion::Phase::SUSPECT;
    assert(panel_render::select_scene({}, view, view.now_ms) == panel_render::Scene::CombustionCheck);
    view.control.combustion = {};
    encoder.reset(false, false, true, 0);
    assert(!encoder.poll(false, false, false, 10).click);
    assert(!encoder.poll(false, false, false, 50).click);
    assert(!encoder.poll(false, false, true, 60).click);
    assert(!encoder.poll(false, false, true, 100).click);
    assert(!encoder.poll(false, false, false, 110).click);
    assert(encoder.poll(false, false, false, 150).click);
    assert(!encoder.poll(false, false, false, 200).click);

    Screen screen{};
    screen.page = Page::MENU;
    view.control.state = State::FAULT_PURGE;
    assert(panel_render::select_scene(screen, view, 440) == panel_render::Scene::FaultCooling);
    view.control.state = State::RUN;
    assert(panel_render::select_scene(screen, view, 3000) == panel_render::Scene::SensorMissing);

    // Render the fault scene through the real canvas into bounded stripe buffers.
    panel_render::Renderer renderer;
    view.control.state = State::FAULT_PURGE;
    renderer.update(screen, view, {}, 3000);
    std::array<uint16_t, panel_render::kWidth * panel_render::kStripeRows> pixels{}, scratch{};
    for (int y = 0; y < panel_render::kHeight; y += panel_render::kStripeRows)
        assert(renderer.draw_band(y, panel_render::kStripeRows, pixels.data(), scratch.data()));
    panel_render::DamageTracker damage;
    const auto first = damage.inspect(pixels.data(), 0, panel_render::kStripeRows);
    assert(first.width == panel_render::kWidth);
    damage.commit(first);
    assert(damage.inspect(pixels.data(), 0, panel_render::kStripeRows).width == 0);
    damage.invalidate();
    assert(damage.inspect(pixels.data(), 0, panel_render::kStripeRows).width == panel_render::kWidth);
    std::cout << "PASS panel: edits, confirmation, STOP, credentials, encoder, fault rendering\n";
}
