#include "startup_health.hpp"
#include "command_policy.hpp"
#include "ota_activation.hpp"
#include "persistence.hpp"
#include "web_sessions.hpp"
#include "web_state.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace sirokko;
int main() {
    StartupHealth h{true, true, true, true, true, true, 1000, 980, true, true};
    assert(h.controller_ready() && h.can_confirm_image());
    h.button_ready = false;
    assert(h.controller_ready() && !h.can_confirm_image());
    h.heater_configured = false;
    assert(h.controller_ready() && h.can_confirm_image());
    h.network_ready = false;
    assert(!h.can_confirm_image());
    h.network_ready = true;
    h.last_control_ms = 899;
    assert(!h.can_confirm_image());
    h.last_control_ms = 1001;
    assert(!h.can_confirm_image());
    h.last_control_ms = 1000;
    h.storage_ok = false;
    assert(!h.can_confirm_image());
    h.storage_ok = true;
    h.outputs_initialized = false;
    assert(!h.can_confirm_image());
    OtaActivationGate ota;
    int partition{};
    ota.observe(true);
    auto epoch = ota.epoch();
    assert(epoch);
    assert(ota.queue(epoch, &partition, 100));
    assert(!ota.claim_due(299));
    assert(ota.revoke_precommit());
    assert(!ota.claim_due(300));
    assert(!ota.acknowledge_revoke());
    ota.observe(false);
    assert(ota.acknowledge_revoke());
    ota.observe(true);
    assert(ota.epoch() != epoch && !ota.queue(epoch, &partition, 400));
    epoch = ota.epoch();
    assert(ota.queue(epoch, &partition, 400));
    assert(ota.claim_due(600) == &partition);
    assert(!ota.revoke_precommit());
    ota.finish_commit(true);
    assert(ota.state() == OtaCommitState::COMMITTED);
    assert(!ota.epoch());
    ModelCommitGate commit;
    assert(commit.open());
    assert(commit.revoke_precommit());
    assert(!commit.claim());
    commit.reset();
    assert(commit.open() && commit.claim());
    assert(!commit.revoke_precommit());
    Settings settings{};
    settings.room_setpoint_milli_c = 23000;
    settings.duration_ms = 60000;
    settings.mode = Mode::MANUAL;
    settings.manual_percent = 75;
    auto decoded = record_codec::encode(123, settings);
    assert(record_codec::valid(decoded, 123, sizeof(decoded)) && valid_settings(decoded.value));
    assert(!record_codec::valid(decoded, 124, sizeof(decoded)));
    assert(!record_codec::valid(decoded, 123, sizeof(decoded) - 1));
    decoded.value.manual_percent = 80;
    assert(!record_codec::valid(decoded, 123, sizeof(decoded)));
    ModelStage stage{};
    stage.boot_id = 1;
    stage.transaction_id = 2;
    LearnedOperatingModel m{};
    m.valid = true;
    stage.candidate = record_codec::encode(42, m);
    assert(valid_model_stage(stage, 42));
    assert(model_boot_consistent(stage, true, stage.candidate));
    assert(model_boot_consistent(stage, false, {}));
    auto corrupt = stage.candidate;
    corrupt.checksum++;
    assert(!model_boot_consistent(stage, true, corrupt));
    web_protocol::Sessions sessions;
    auto *a = sessions.create("00000000000000000000000000000001", 0);
    auto *b = sessions.create("00000000000000000000000000000002", 0);
    assert(a && b && a != b);
    assert(sessions.find("x=1; sirokko_session=00000000000000000000000000000001; y=2", 1) == a);
    assert(!sessions.find("other_sirokko_session=00000000000000000000000000000001", 1));
    assert(!sessions.find("sirokko_session=00000000000000000000000000000001x", 1));
    web_protocol::Command cmd{};
    cmd.action = Action::START;
    cmd.grant = {1, 2, 50};
    cmd.payload_digest[0] = 5;
    cmd.has_grant = true;
    a->challenge.bind(cmd.action, cmd.grant, cmd.payload_digest);
    b->challenge.bind(Action::OTA, {9, 2, 50}, {});
    assert(a->challenge.consume(cmd, 2, 2) == web_protocol::BindingResult::ACCEPTED);
    a->replays.remember(1, cmd.payload_digest, true);
    bool accepted{};
    assert(b->replays.lookup(1, cmd.payload_digest, &accepted) == web_protocol::ReplayResult::NEW);
    assert(!sessions.find("sirokko_session=00000000000000000000000000000001",
                          web_protocol::Sessions::lifetime_ms + 1));
    for (unsigned n = 3; n <= 8; ++n) {
        char token[33]{};
        std::snprintf(token, sizeof(token), "%032u", n);
        assert(sessions.create(token, 2));
    }
    assert(!sessions.create("00000000000000000000000000000009", 2));
    assert(sessions.create("00000000000000000000000000000009", web_protocol::Sessions::lifetime_ms + 2));
    RuntimeView eligibility{};
    eligibility.startup_ready = true;
    eligibility.control.state = State::UNCONFIGURED;
    eligibility.control.start_block = eligibility.control.commissioning.block = Block::ENVELOPE_MISSING;
    assert(command_policy::block(eligibility, Action::START) == Block::ENVELOPE_MISSING);
    assert(command_policy::block(eligibility, Action::CALIBRATE) == Block::ENVELOPE_MISSING);
    assert(command_policy::block(eligibility, Action::SETTINGS) == Block::NONE);
    assert(command_policy::block(eligibility, Action::OTA) == Block::NONE);
    Snapshot maintenance{};
    maintenance.state = State::MAINTENANCE;
    assert(ota_control_safe(maintenance, {}, {}, 100));
    SafetyEnvelope configured{};
    configured.provisioned = true;
    assert(!ota_control_safe(maintenance, {}, configured, 100));
    maintenance.outputs.fuel = true;
    assert(!ota_control_safe(maintenance, {}, {}, 100));
    eligibility.storage_busy = true;
    assert(command_policy::block(eligibility, Action::OTA) == Block::BUSY);
    eligibility.storage_busy = false;
    eligibility.envelope.provisioned = true;
    assert(command_policy::block(eligibility, Action::START) == Block::LOCAL_CONTROLS_UNAVAILABLE);
    RuntimeView wifi{};
    assert(command_policy::wifi_password_block(wifi) == Block::INITIALIZING);
    wifi.startup_ready = true;
    for (const auto state : {State::UNCONFIGURED, State::IDLE, State::FAULT}) {
        wifi.control.state = state;
        assert(command_policy::wifi_password_block(wifi) == Block::NONE);
    }
    for (const auto state : {State::RECOVERY_PURGE, State::PREHEAT, State::IGNITION, State::RUN,
                             State::STOP_PURGE, State::FAULT_PURGE, State::MAINTENANCE}) {
        wifi.control.state = state;
        assert(command_policy::wifi_password_block(wifi) != Block::NONE);
    }
    wifi.control.state = State::IDLE;
    wifi.control.commissioning.active = true;
    assert(command_policy::wifi_password_block(wifi) == Block::BUSY);
    wifi.control.commissioning.active = false;
    for (auto output :
         {&wifi.control.outputs.fan_request, &wifi.control.outputs.fuel, &wifi.control.outputs.glow}) {
        *output = true;
        assert(command_policy::wifi_password_block(wifi) == Block::BUSY);
        *output = false;
    }
    wifi.storage_busy = true;
    assert(command_policy::wifi_password_block(wifi) == Block::BUSY);
    wifi.storage_busy = false;
    wifi.storage_ok = false;
    assert(command_policy::wifi_password_block(wifi) == Block::STORAGE_UNAVAILABLE);
    wifi.storage_ok = true;
    char wifi_output[kStateJsonCapacity]{};
    JsonBuilder wifi_json(wifi_output, sizeof(wifi_output));
    assert(build_state_json(wifi, {}, &wifi_json));
    assert(std::strstr(wifi_output, "\"change_allowed\":true"));
    wifi.control.state = State::RUN;
    JsonBuilder wifi_running(wifi_output, sizeof(wifi_output));
    assert(build_state_json(wifi, {}, &wifi_running));
    assert(std::strstr(wifi_output, "\"change_allowed\":false"));
    assert(std::strstr(wifi_output, "Смена пароля доступна после остановки"));
    RuntimeView view{};
    view.control.start_block = Block::ENVELOPE_MISSING;
    char output[kStateJsonCapacity]{};
    JsonBuilder json(output, sizeof(output));
    assert(build_state_json(view, {"test", false, false, false}, &json));
    assert(std::strstr(output, "\"outputs_enabled\":false") && std::strstr(output, "ENVELOPE_MISSING"));
    view.control.faults = (1U << 10) - 1;
    view.event_count = view.events.size();
    for (auto &event : view.events) {
        event.kind = "OPERATOR_COMBUSTION_PROBLEM";
        event.state = State::RECOVERY_PURGE;
        event.at_ms = UINT64_MAX;
        event.speed_code = UINT16_MAX;
        event.exhaust_milli_c = event.air_milli_c = INT32_MIN;
        event.faults = UINT32_MAX;
    }
    JsonBuilder full(output, sizeof(output));
    assert(build_state_json(view, {"1.1.1-dev", true, true, true}, &full));
    assert(std::strstr(output, "Достигнут предел температуры выхлопа; топливо отключено"));
    view.events[0].kind = "CALIBRATION_SAVED";
    view.events[1].kind = "SETTLE_POINT";
    view.events[2].kind = "FUTURE_INTERNAL_EVENT";
    JsonBuilder translated(output, sizeof(output));
    assert(build_state_json(view, {}, &translated));
    assert(std::strstr(output, "\"kind_ru\":\"Результат калибровки сохранён\""));
    assert(std::strstr(output, "\"kind_ru\":\"Ожидание стабильных температур при сниженном ШИМ\""));
    assert(std::strstr(output, "\"kind_ru\":\"Неизвестное событие\""));
    // Long translated phase/reason strings must still fit with a full event ring.
    view.control.commissioning.phase = CommissioningPhase::SETTLE_POST_REPEAT_REFERENCE;
    view.control.commissioning.reason = CommissioningReason::DYNAMIC_NOT_REPRODUCIBLE;
    for (auto &event : view.events)
        event.kind = "SETTLE_POST_REPEAT_REFERENCE";
    JsonBuilder longest(output, sizeof(output));
    assert(build_state_json(view, {}, &longest));
    // API validity agrees with the controller, including plausible range failures.
    view.now_ms = 1000;
    view.sensors.exhaust = {kProcessMeasurementCeilingMilliC, 1000, 0, true};
    view.sensors.air = {20000, 1000, 0, true};
    view.sensors.room = {126000, 1000, 0, true};
    JsonBuilder invalid_range(output, sizeof(output));
    assert(build_state_json(view, {}, &invalid_range));
    assert(std::strstr(output,
                       "\"milli_c\":600000,\"sample_ms\":1000,\"status\":0,\"valid\":false,\"fresh\":false"));
    assert(std::strstr(output,
                       "\"milli_c\":20000,\"sample_ms\":1000,\"status\":0,\"valid\":true,\"fresh\":true"));
    assert(std::strstr(output,
                       "\"milli_c\":126000,\"sample_ms\":1000,\"status\":0,\"valid\":false,\"fresh\":false"));
    char small[8];
    JsonBuilder truncated(small, sizeof(small));
    assert(!build_state_json(view, {}, &truncated));
    std::cout << "PASS runtime: boot health, OTA cancellation, record integrity, sessions, JSON\n";
}
