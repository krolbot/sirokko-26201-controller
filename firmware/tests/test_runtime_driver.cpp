// Production app_main passes with deterministic synthetic ESP-IDF boundaries.
// Synthetic envelopes/models below are host fixtures only; they are not a production
// commissioning writer or an assertion about physical NVS power-cut behavior.
#include "fake_runtime_idf.hpp"
#include "../main/app_main.cpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

bool SensorSuite::begin() { return true; }
void SensorSuite::poll(uint64_t) {}
sirokko::Sensors SensorSuite::readings() const { return {}; }
bool panel_begin() { return fake_runtime_idf::panel_ok; }
bool web_start(const char*) { return fake_runtime_idf::web_ok; }

namespace {
using namespace sirokko;
namespace io = fake_runtime_idf;

SafetyEnvelope synthetic_envelope() {
    SafetyEnvelope envelope{};
    envelope.provisioned = true;
    envelope.recovery_purge_ms = 100;
    envelope.prepurge_ms = 10;
    envelope.preheat_ms = 10;
    envelope.ignition_ms = 10;
    envelope.verify_ms = 10;
    envelope.postpurge_ms = 20;
    envelope.maximum_purge_exit_exhaust_milli_c = 60000;
    envelope.maximum_purge_exit_air_milli_c = 50000;
    envelope.maximum_exhaust_milli_c = 500000;
    envelope.maximum_air_milli_c = 250000;
    envelope.minimum_ignition_rise_milli_c = 10000;
    envelope.minimum_ignition_air_rise_milli_c = 5000;
    envelope.minimum_run_exhaust_milli_c = 110000;
    envelope.purge_speed_code = 1400;
    envelope.reference_speed_code = 1000;
    envelope.exploration_floor_code = 400;
    return envelope;
}

LearnedOperatingModel synthetic_model(uint16_t minimum = 600, uint16_t maximum = 1000) {
    LearnedOperatingModel model{};
    model.valid = true;
    model.minimum_speed_code = minimum;
    model.maximum_speed_code = maximum;
    model.slew_up_per_s = .004;
    model.slew_down_per_s = .003;
    model.provenance = {envelope_fingerprint(synthetic_envelope()), 1, 101, 3};
    model.measurements = {100, 20, 30, 200000, 40000, 180000, 45000, 1, 1, 1, 1, 10, 10, 2};
    model.fingerprint = learned_model_fingerprint(model);
    return model;
}

Sensors samples(uint64_t now, int exhaust = 20000, int air = 20000) {
    return {{exhaust, now, 0, true}, {air, now, 0, true}, {20000, now, 0, true}};
}

void reset_globals() {
    delete g_controller;
    g_controller = nullptr;
    io::reset();
    g_gate = {};
    g_output_driver = {};
    g_ota_activation.reset();
    g_model_commit.reset();
    g_storage_faults = 0;
    g_storage_busy = false;
    g_model_pending = false;
    g_stop = false;
    g_operator_problem = false;
    g_exit_maintenance = false;
    g_output_driver_fault = false;
    g_boot_ready = false;
    g_web_ready = false;
    g_credentials_ready = false;
    g_control_passes = 0;
    g_maintenance_epoch = 0;
    g_view = {};
    g_sensor_queue = g_command_queue = g_storage_jobs = g_store_results = g_ota_activation_queue = nullptr;
    g_view_mutex = g_gate_mutex = nullptr;
}

void seed_factory_boot(const LearnedOperatingModel& model = synthetic_model()) {
    // Synthetic host preconditioning of the read-only factory source.
    assert(save_record(kEnvelopeKey, kEnvelopeMagic, synthetic_envelope()));
    assert(save_record(kModelKey, kModelMagic, model));
}

void boot_provisioned(const LearnedOperatingModel& model = synthetic_model()) {
    reset_globals();
    seed_factory_boot(model);
    app_main();
    g_boot_ready = true;
}

void reboot_preserving_flash() {
    auto flash = io::flash;
    reset_globals();
    io::flash = std::move(flash);
    app_main();
    g_boot_ready = true;
}

void step(ControlWork& work, uint64_t now, int exhaust = 20000, int air = 20000) {
    io::clock_ms = now;
    const Sensors sensor = samples(now, exhaust, air);
    assert(xQueueOverwrite(g_sensor_queue, &sensor) == pdTRUE);
    control_pass(work);
}

void next(ControlWork& work) { step(work, io::clock_ms + 20); }

bool submit(Action action, const Settings* settings = nullptr) {
    Grant grant{};
    assert(runtime_issue(action, 9, &grant));
    return runtime_submit(grant, action, 0, settings);
}

bool issue(Action action, Grant* grant) { return runtime_issue(action, 9, grant); }

void test_empty_boot_is_safe() {
    reset_globals();
    app_main();
    ControlWork work{};
    step(work, 1000);
    RuntimeView view{};
    assert(runtime_view(&view));
    assert(!view.envelope.provisioned);
    assert(view.control.start_block == Block::ENVELOPE_MISSING);
    assert(!view.control.outputs.fuel && !view.control.outputs.glow);
    std::cout << "PASS runtime empty NVS: missing factory source remains safely blocked\n";
}

void test_factory_record_and_model_boot() {
    boot_provisioned(synthetic_model(650, 900));
    ControlWork work{};
    step(work, 1000);
    assert(g_controller->safety_envelope().provisioned);
    assert(g_controller->learned_model().valid);
    assert(g_controller->learned_model().minimum_speed_code == 650);
    assert(g_controller->learned_model().maximum_speed_code == 900);
    assert(g_controller->snapshot(1000).state == State::IDLE);

    auto corrupt = io::flash["sirokko/envelope_v1"];
    corrupt[0] ^= 1;
    reset_globals();
    io::flash["sirokko/envelope_v1"] = corrupt;
    app_main();
    assert(!g_controller->safety_envelope().provisioned);
    assert(!storage_healthy());

    reset_globals();
    assert(save_record(kEnvelopeKey, kEnvelopeMagic, synthetic_envelope()));
    assert(save_record(kModelKey, kModelMagic, synthetic_model()));
    io::flash["sirokko/model_active4"][0] ^= 1;
    app_main();
    assert(g_controller->safety_envelope().provisioned);
    assert(!g_controller->learned_model().valid);
    assert(!storage_healthy());
    std::cout << "PASS factory envelope/model: codec+semantic source loads before Controller; corrupt records fail closed\n";
}

void test_restart_lockout_stale_key0_and_fault_purge() {
    for (bool fault : {false, true}) {
        boot_provisioned();
        ControlWork work{};
        step(work, 1000);
        const uint64_t stale_version = g_view.control.state_version;
        assert(submit(Action::START));
        next(work);
        if (fault) {
            step(work, io::clock_ms + 20, 20000, 20000);
            step(work, io::clock_ms + 20, 20000, 20000);
            step(work, io::clock_ms + 20, 20000, 20000);
            io::clock_ms += 20;
            Sensors bad = samples(io::clock_ms);
            bad.exhaust.valid = false;
            assert(xQueueOverwrite(g_sensor_queue, &bad) == pdTRUE);
            control_pass(work);
        } else {
            runtime_stop();
            next(work);
        }
        const State stopping = fault ? State::FAULT_PURGE : State::STOP_PURGE;
        assert(g_view.control.state == stopping);
        assert(!g_view.control.outputs.fuel && !g_view.control.outputs.glow);
        assert(!submit(Action::START));
        assert(!button_start(nullptr, g_view.control.state_version));
        step(work, io::clock_ms + 500, 120000, 60000);
        assert(g_view.control.state == stopping && g_view.control.outputs.fan_request);
        assert(!submit(Action::START));
        step(work, io::clock_ms + 500);
        if (fault) {
            assert(g_view.control.state == State::FAULT);
            assert(submit(Action::RESET_FAULT));
            next(work);
        }
        assert(g_view.control.state == State::IDLE);
        assert(!button_start(nullptr, stale_version));
        for (unsigned pass = 0; pass < 10; ++pass) next(work);
        assert(g_view.control.state == State::IDLE);
        assert(button_start(nullptr, g_view.control.state_version));
    }
    std::cout << "PASS STOP/fault purge lockout: stale KEY0 cannot replay a deferred START\n";
}

void test_settings_barriers_and_ack_backpressure() {
    boot_provisioned();
    ControlWork control{};
    StorageWork storage{};
    step(control, 1000);
    Settings first{};
    first.mode = Mode::MANUAL;
    first.manual_percent = 90;
    assert(submit(Action::SETTINGS, &first));
    next(control);
    step(control, io::clock_ms + 1500);
    assert(g_storage_busy);
    bool start_accepted = true;
    unsigned passes = 0;
    io::before_commit = [&](const std::string& key) {
        if (key != "sirokko/settings_v2") return;
        start_accepted = submit(Action::START);
        runtime_stop();
        for (unsigned pass = 0; pass < 40; ++pass) {
            next(control);
            ++passes;
            assert(!g_view.control.outputs.fuel && g_storage_busy);
        }
    };
    storage_pass(storage);
    io::before_commit = {};
    assert(!start_accepted && passes == 40 && g_storage_busy);
    next(control);
    assert(!g_storage_busy && g_view.control.state == State::IDLE);

    assert(submit(Action::SETTINGS, &first));
    next(control);
    step(control, io::clock_ms + 1500);
    StoreResult stale{control.inflight.id + 1, StoreKind::SETTINGS, true};
    assert(xQueueSend(g_store_results, &stale, 0) == pdTRUE);
    storage_pass(storage);
    assert(storage.waiting && g_storage_busy);
    next(control);
    assert(g_storage_busy && (g_storage_faults.load() & STORAGE_INTERNAL));
    storage_pass(storage);
    next(control);
    assert(!g_storage_busy && !storage.waiting && io::maximum_queue_wait == 0);
    std::cout << "PASS settings: START barrier, STOP priority, 40 control passes and nonblocking stale-ACK retry\n";
}

void begin_revision(ControlWork& control, uint16_t minimum, uint16_t maximum) {
    const auto old = g_controller->learned_model();
    LearnedModelRevision revision{old.provenance.revision, minimum, maximum, .003, .002};
    Grant grant{};
    assert(issue(Action::MODEL_REVISION, &grant));
    assert(runtime_revise_model(grant, revision));
    next(control);
    assert(g_storage_busy && g_model_pending);
}

void test_revision_narrows_both_bounds_and_bootloads() {
    boot_provisioned();
    ControlWork control{};
    StorageWork storage{};
    step(control, 1000);
    begin_revision(control, 650, 900);
    storage_pass(storage);
    next(control);
    assert(g_model_commit.claimed() && g_storage_busy);
    storage_pass(storage);
    next(control);
    assert(!g_storage_busy && !g_model_pending);
    assert(g_controller->learned_model().minimum_speed_code == 650);
    assert(g_controller->learned_model().maximum_speed_code == 900);
    reboot_preserving_flash();
    assert(g_controller->safety_envelope().provisioned);
    assert(g_controller->learned_model().minimum_speed_code == 650);
    assert(g_controller->learned_model().maximum_speed_code == 900);
    std::cout << "PASS revision: both bounds narrow, durable record loads through app_main factory boot\n";
}

void test_revision_stop_fault_and_storage_unknown_outcomes() {
    for (bool fault : {false, true}) {
        boot_provisioned();
        ControlWork control{};
        StorageWork storage{};
        step(control, 1000);
        const auto old = g_controller->learned_model();
        begin_revision(control, 650, 900);
        if (fault) runtime_stop(true); else runtime_stop();
        next(control);
        storage_pass(storage);
        next(control);
        assert(!g_model_pending && !g_storage_busy);
        assert(g_controller->learned_model().minimum_speed_code == old.minimum_speed_code);
        LearnedOperatingModel active{};
        assert(load_record(kModelKey, kModelMagic, &active));
        assert(active.minimum_speed_code == old.minimum_speed_code);
        assert(active.maximum_speed_code == old.maximum_speed_code);
    }

    for (unsigned failure = 0; failure < 3; ++failure) {
        boot_provisioned();
        ControlWork control{};
        StorageWork storage{};
        step(control, 1000);
        begin_revision(control, 650, 900);
        if (failure == 0) io::nvs_set_error = ESP_FAIL;
        if (failure == 2) {
            io::after_commit = [](const std::string& key) {
                if (key == "sirokko/model_stage4") io::nvs_get_error = ESP_FAIL;
            };
        }
        storage_pass(storage);
        io::nvs_set_error = ESP_OK;
        io::after_commit = {};
        io::nvs_get_error = ESP_OK;
        if (failure == 1) {
            next(control);
            io::nvs_commit_error = ESP_FAIL;
            storage_pass(storage);
            io::nvs_commit_error = ESP_OK;
        }
        next(control);
        assert(!storage_healthy());
        assert(g_controller->learned_model().minimum_speed_code == 600);
    }
    std::cout << "PASS model storage: pre-claim STOP/fault cancel; set/commit/readback failures retain old RAM model\n";
}

struct PowerCut {};
struct PreparedCommissioning {
    Controller controller{synthetic_envelope(), {}, 7};
    LearnedOperatingModel old{};
    LearnedOperatingModel candidate{};
    uint64_t ready_at{};
};

const PreparedCommissioning& calibrated_pending_fixture() {
    static const PreparedCommissioning prepared = [] {
        PreparedCommissioning result{};
        result.old = synthetic_model(700, 1000);
        assert(result.controller.install_learned_model(result.old));
        auto tick = [&](uint64_t now, int exhaust = 20000, int air = 20000, bool start = false) {
            TickInput input{};
            input.now_ms = now;
            input.sensors = samples(now, exhaust, air);
            input.start_commissioning = start;
            return result.controller.tick(input);
        };
        assert(tick(1000).state == State::IDLE);
        assert(tick(1001, 20000, 20000, true).state == State::PREPURGE);
        tick(1101); tick(1201); tick(1301, 270000, 70000);
        auto state = tick(1401, 270000, 70000);
        assert(state.state == State::RUN);
        double exhaust = 270000, air = 70000;
        TickInput input{};
        input.sensors = samples(1401, 270000, 70000);
        for (uint64_t now = 1421; now < 21600000; now += 20) {
            const double target_exhaust = state.outputs.fuel ? 160000 + 110.0 * state.outputs.speed_code : 20000;
            const double target_air = state.outputs.fuel ? 70000 + 15.0 * (1000 - state.outputs.speed_code) : 20000;
            exhaust += (target_exhaust - exhaust) * (-std::expm1(-0.02 / 5.0));
            air += (target_air - air) * (-std::expm1(-0.02 / 5.0));
            input.now_ms = now;
            if (now - input.sensors.exhaust.sample_ms >= 200)
                input.sensors = samples(now, std::lround(exhaust), std::lround(air));
            state = result.controller.tick(input);
            assert(state.state != State::FAULT && state.state != State::FAULT_PURGE);
            if (result.controller.pending_learned_model(&result.candidate)) {
                result.ready_at = now;
                break;
            }
        }
        assert(result.ready_at && result.candidate.valid);
        assert(result.candidate.minimum_speed_code < result.old.minimum_speed_code);
        assert(result.controller.learned_model().fingerprint == result.old.fingerprint);
        return result;
    }();
    return prepared;
}

void assert_model_equal(const LearnedOperatingModel& actual, const LearnedOperatingModel& expected) {
    assert(actual.valid == expected.valid);
    assert(actual.minimum_speed_code == expected.minimum_speed_code);
    assert(actual.maximum_speed_code == expected.maximum_speed_code);
    assert(actual.slew_up_per_s == expected.slew_up_per_s);
    assert(actual.slew_down_per_s == expected.slew_down_per_s);
    assert(actual.provenance.safety_envelope_fingerprint == expected.provenance.safety_envelope_fingerprint);
    assert(actual.fingerprint == expected.fingerprint);
}

LearnedOperatingModel durable_model() {
    LearnedOperatingModel model{};
    assert(load_record(kModelKey, kModelMagic, &model));
    return model;
}

void assert_reboot_model(const LearnedOperatingModel& expected, bool storage_ok = true) {
    reboot_preserving_flash();
    assert(storage_healthy() == storage_ok);
    assert_model_equal(g_controller->learned_model(), expected);
    const auto snap = g_controller->snapshot(0);
    assert(!snap.outputs.fuel && !snap.outputs.glow);
}

void pending(ControlWork& control) {
    const auto& fixture = calibrated_pending_fixture();
    boot_provisioned(fixture.old);
    delete g_controller;
    g_controller = new Controller(fixture.controller);
    g_boot_ready = true;
    step(control, fixture.ready_at);
    assert(g_storage_busy && g_model_pending && control.inflight.kind == StoreKind::MODEL_STAGE);
    assert(g_view.control.commissioning.phase == CommissioningPhase::PERSIST_STAGING);
    assert_model_equal(g_controller->learned_model(), fixture.old);
    assert_model_equal(durable_model(), fixture.old);
}

void sealed(ControlWork& control, StorageWork& storage) {
    pending(control);
    storage_pass(storage);
    assert_model_equal(durable_model(), calibrated_pending_fixture().old);
    next(control);
    assert(g_model_commit.claimed() && g_storage_busy);
    assert(g_view.control.commissioning.phase == CommissioningPhase::PERSIST_COMMITTING);
    assert_model_equal(g_controller->learned_model(), calibrated_pending_fixture().old);
}

void test_commissioning_normal_and_preclaim_cancellations() {
    const auto& fixture = calibrated_pending_fixture();
    { ControlWork control{}; StorageWork storage{}; sealed(control, storage); storage_pass(storage); next(control);
      assert(!g_storage_busy && !g_model_pending);
      assert(g_view.control.commissioning.phase == CommissioningPhase::SAVED);
      assert_model_equal(g_controller->learned_model(), fixture.candidate);
      assert_reboot_model(fixture.candidate); }
    for (unsigned point = 0; point < 4; ++point) {
        ControlWork control{}; StorageWork storage{}; pending(control);
        if (point == 0) { runtime_stop(); next(control); storage_pass(storage); next(control); }
        if (point == 1) {
            io::before_commit = [&](const std::string& key) {
                if (key == "sirokko/model_stage4") { runtime_stop(true); next(control); }
            };
            storage_pass(storage); io::before_commit = {}; next(control);
        }
        if (point == 2) { storage_pass(storage); runtime_stop(); next(control); }
        if (point == 3) { storage_pass(storage); g_output_driver_fault = true; next(control); }
        assert(!g_storage_busy && !g_model_pending && !g_controller->snapshot(io::clock_ms).commissioning.active);
        assert_model_equal(g_controller->learned_model(), fixture.old);
        assert_model_equal(durable_model(), fixture.old);
        assert_reboot_model(fixture.old);
    }
    std::cout << "PASS commissioning runtime: calibrated candidate stages, normal activation commits, STOP/fault before claim retain exact old model\n";
}

void test_commissioning_postclaim_stop_fault_and_deferred_activation() {
    const auto& fixture = calibrated_pending_fixture();
    { ControlWork control{}; StorageWork storage{}; sealed(control, storage);
      runtime_stop(true); next(control);
      assert(g_model_commit.claimed() && g_view.control.commissioning.active);
      storage_pass(storage); next(control);
      assert(g_view.control.commissioning.phase == CommissioningPhase::SAVED);
      assert_model_equal(g_controller->learned_model(), fixture.candidate);
      assert_reboot_model(fixture.candidate); }
    { ControlWork control{}; StorageWork storage{}; sealed(control, storage);
      g_output_driver_fault = true; next(control);
      assert(g_view.control.state == State::FAULT_PURGE && g_storage_busy);
      storage_pass(storage); step(control, io::clock_ms + 20, 120000, 60000);
      assert(g_storage_busy && g_view.control.state == State::FAULT_PURGE);
      assert_model_equal(g_controller->learned_model(), fixture.old);
      for (unsigned pass = 0; pass < 10 && g_storage_busy; ++pass) next(control);
      assert(!g_storage_busy && g_view.control.state == State::FAULT);
      assert_model_equal(g_controller->learned_model(), fixture.candidate);
      assert_reboot_model(fixture.candidate); }
    std::cout << "PASS post-claim runtime: STOP preserves sealed activation; fault purge defers RAM acceptance until FAULT\n";
}

void test_commissioning_storage_failures() {
    const auto& fixture = calibrated_pending_fixture();
    { ControlWork control{}; StorageWork storage{}; pending(control); io::nvs_set_error = ESP_FAIL; storage_pass(storage); io::nvs_set_error = ESP_OK; next(control);
      assert(!g_storage_busy && !storage_healthy()); assert_model_equal(g_controller->learned_model(), fixture.old); assert_reboot_model(fixture.old); }
    { ControlWork control{}; StorageWork storage{}; sealed(control, storage); io::nvs_commit_error = ESP_FAIL; storage_pass(storage); io::nvs_commit_error = ESP_OK; next(control);
      assert(!g_storage_busy && !storage_healthy()); assert_model_equal(g_controller->learned_model(), fixture.old); assert_reboot_model(fixture.old); }
    { ControlWork control{}; StorageWork storage{}; sealed(control, storage);
      io::after_commit = [](const std::string& key) { if (key == "sirokko/model_active4") io::nvs_get_error = ESP_FAIL; };
      storage_pass(storage); io::after_commit = {}; io::nvs_get_error = ESP_OK; next(control);
      assert(!g_storage_busy && !storage_healthy());
      assert(g_view.control.commissioning.phase == CommissioningPhase::PERSIST_FAILED);
      assert(g_view.control.commissioning.reason == CommissioningReason::STORAGE_UNCONFIRMED);
      assert_model_equal(g_controller->learned_model(), fixture.old);
      assert_reboot_model(fixture.candidate); }
    std::cout << "PASS commissioning storage failures: exact old RAM on failure, readback-ambiguous durable candidate remains unconfirmed\n";
}

void test_commissioning_power_cut_boundaries() {
    const auto& fixture = calibrated_pending_fixture();
    const auto hook = [](const std::string& expected) { return [expected](const std::string& actual) { if (actual == expected) throw PowerCut{}; }; };
    for (unsigned point = 0; point < 8; ++point) {
        ControlWork control{}; StorageWork storage{};
        const bool active = point >= 4;
        if (active) sealed(control, storage); else pending(control);
        const char* key = active ? "sirokko/model_active4" : "sirokko/model_stage4";
        const unsigned boundary = point % 4;
        if (boundary == 0) io::before_set = hook(key);
        if (boundary == 1) io::after_set = hook(key);
        if (boundary == 2) io::before_commit = hook(key);
        if (boundary == 3) io::after_commit = hook(key);
        if (active && boundary == 1) io::durable_at_set = true;
        bool interrupted = false;
        try { storage_pass(storage); } catch (const PowerCut&) { interrupted = true; }
        assert(interrupted);
        auto flash = io::flash;
        reset_globals(); io::flash = std::move(flash); app_main(); g_boot_ready = true;
        const auto& expected = active && (boundary == 1 || boundary == 3) ? fixture.candidate : fixture.old;
        assert(storage_healthy());
        assert_model_equal(g_controller->learned_model(), expected);
        assert(!g_controller->snapshot(0).outputs.fuel && !g_controller->snapshot(0).outputs.glow);
    }
    std::cout << "PASS commissioning journal: exact old/new reboot model and no storage fault at every stage/active set+commit boundary\n";
}

void test_ota_claim_cancellation() {
    for (unsigned which = 0; which < 3; ++which) {
        boot_provisioned();
        ControlWork control{};
        step(control, 1000);
        assert(submit(Action::OTA));
        next(control);
        assert(g_view.control.state == State::MAINTENANCE);
        const uint32_t lease = runtime_maintenance_lease();
        assert(lease && runtime_activate_ota(lease, esp_ota_get_next_update_partition(nullptr)));
        next(control);
        if (which == 0) runtime_stop();
        if (which == 1) runtime_exit_maintenance();
        if (which == 2) {
            io::clock_ms += 200;
            Sensors bad = samples(io::clock_ms);
            bad.exhaust.valid = false;
            assert(xQueueOverwrite(g_sensor_queue, &bad) == pdTRUE);
            control_pass(control);
        } else {
            step(control, io::clock_ms + 200);
        }
        assert(!io::boot_select_count && !io::restart_count && !runtime_maintenance_lease());
    }
    std::cout << "PASS OTA owner: STOP/exit/fault before claim prevents boot selection\n";
}
}  // namespace

int main() {
    test_empty_boot_is_safe();
    test_factory_record_and_model_boot();
    test_restart_lockout_stale_key0_and_fault_purge();
    test_settings_barriers_and_ack_backpressure();
    test_revision_narrows_both_bounds_and_bootloads();
    test_revision_stop_fault_and_storage_unknown_outcomes();
    test_commissioning_normal_and_preclaim_cancellations();
    test_commissioning_postclaim_stop_fault_and_deferred_activation();
    test_commissioning_storage_failures();
    test_commissioning_power_cut_boundaries();
    test_ota_claim_cancellation();
    delete g_controller;
}
