#include "runtime_api.hpp"
#include "thermal_history.hpp"
#include "command_policy.hpp"
#include "network.hpp"
#include "startup_health.hpp"
#include "start_stop_button.hpp"
#include "ota_activation.hpp"
#include "persistence.hpp"
#include "record_codec.hpp"
#include "panel.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include "board.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensors.hpp"
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "Set CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192 in the active sdkconfig");
namespace {
constexpr char kTag[] = "sirokko";
constexpr uint32_t kSettingsMagic = 0x53495233;
constexpr uint32_t kEnvelopeMagic = 0x454E5636;
constexpr uint32_t kCycleMagic = 0x43594331;
constexpr char kSettingsKey[] = "settings_v3";
constexpr char kEnvelopeKey[] = "envelope_v6";
constexpr char kCycleKey[] = "cycle_v1";
enum class StoreKind : uint8_t { SETTINGS, PROFILE, CYCLE_ACTIVE, CYCLE_CLEAN };
struct RuntimeCommand {
    sirokko::Grant grant{};
    sirokko::Action action{};
    uint64_t delay_ms{};
    uint16_t speed_code{};
    sirokko::Settings settings{};
    sirokko::SafetyEnvelope envelope{};
};
struct StoreRequest {
    uint64_t id{};
    StoreKind kind{};
    sirokko::Settings settings{};
    sirokko::SafetyEnvelope envelope{};
};
struct StoreResult {
    uint64_t id{};
    StoreKind kind{};
    bool ok{};
};
struct OtaActivation {
    uint32_t lease{};
    const void *partition{};
};
enum StorageFault : uint32_t {
    STORAGE_INIT = 1,
    STORAGE_SETTINGS = 2,
    STORAGE_ENVELOPE = 4,
    STORAGE_INTERNAL = 32,
    STORAGE_CYCLE = 64,
};
std::atomic<uint32_t> g_storage_faults{};
std::atomic<bool> g_storage_busy{}, g_stop{}, g_exit_maintenance{}, g_output_driver_fault{}, g_boot_ready{};
std::atomic<uint32_t> g_maintenance_epoch{};
std::atomic<bool> g_button_ready{}, g_startup_complete{}, g_cancel_commands{};
std::atomic<sirokko::Block> g_wifi_password_block{sirokko::Block::INITIALIZING};
sirokko::OtaActivationGate g_ota_activation;
sirokko::CommandGate g_gate;
sirokko::FailClosedOutputDriver g_output_driver;
sirokko::Controller *g_controller{};
sirokko::Startup g_startup{sirokko::Startup::RECOVER};
SensorSuite g_sensor_suite;
QueueHandle_t g_sensor_queue{}, g_button_samples{}, g_command_queue{}, g_storage_jobs{}, g_store_results{},
    g_ota_activation_queue{};
SemaphoreHandle_t g_view_mutex{}, g_gate_mutex{};
SemaphoreHandle_t g_history_mutex{};
ThermalHistory g_history;
RuntimeView g_view{};
uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}
bool storage_healthy() {
    return g_storage_faults.load() == 0;
}
uint32_t fault_for(const char *key) {
    if (!std::strcmp(key, kSettingsKey))
        return STORAGE_SETTINGS;
    if (!std::strcmp(key, kEnvelopeKey))
        return STORAGE_ENVELOPE;
    if (!std::strcmp(key, kCycleKey))
        return STORAGE_CYCLE;
    return STORAGE_INTERNAL;
}
void storage_status(uint32_t fault, bool ok) {
    if (ok)
        g_storage_faults.fetch_and(~fault);
    else
        g_storage_faults.fetch_or(fault);
}
enum class RecordRead : uint8_t { MISSING, VALID, INVALID };
template <class T> RecordRead read_record(const char *key, uint32_t magic, record_codec::Record<T> *out) {
    nvs_handle_t nvs{};
    const auto opened = nvs_open("sirokko", NVS_READONLY, &nvs);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
        return RecordRead::MISSING;
    if (opened != ESP_OK) {
        storage_status(fault_for(key), false);
        return RecordRead::INVALID;
    }
    record_codec::Record<T> record{};
    size_t size = sizeof(record);
    const auto e = nvs_get_blob(nvs, key, &record, &size);
    nvs_close(nvs);
    if (e == ESP_ERR_NVS_NOT_FOUND)
        return RecordRead::MISSING;
    if (e != ESP_OK || !record_codec::valid(record, magic, size)) {
        storage_status(fault_for(key), false);
        return RecordRead::INVALID;
    }
    *out = record;
    return RecordRead::VALID;
}
template <class T> bool load_record(const char *key, uint32_t magic, T *out) {
    record_codec::Record<T> r{};
    if (read_record(key, magic, &r) != RecordRead::VALID)
        return false;
    *out = r.value;
    return true;
}
template <class T>
bool save_encoded_record(const char *key, uint32_t magic, const record_codec::Record<T> &record) {
    nvs_handle_t nvs{};
    if (nvs_open("sirokko", NVS_READWRITE, &nvs) != ESP_OK) {
        storage_status(fault_for(key), false);
        return false;
    }
    bool ok = record_codec::valid(record, magic, sizeof(record)) &&
              nvs_set_blob(nvs, key, &record, sizeof(record)) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    record_codec::Record<T> check{};
    size_t size = sizeof(check);
    ok = ok && nvs_get_blob(nvs, key, &check, &size) == ESP_OK && record_codec::valid(check, magic, size) &&
         std::memcmp(&record, &check, sizeof(record)) == 0;
    nvs_close(nvs);
    storage_status(fault_for(key), ok);
    return ok;
}
template <class T> bool save_record(const char *key, uint32_t magic, const T &value) {
    return save_encoded_record(key, magic, record_codec::encode(magic, value));
}
sirokko::Startup load_startup(bool configured) {
    record_codec::Record<uint32_t> record{};
    const auto state = read_record(kCycleKey, kCycleMagic, &record);
    using Status = sirokko::CycleRecordStatus;
    Status status = Status::INVALID;
    if (state == RecordRead::VALID && record.value <= 1)
        status = record.value == 0 ? Status::CLEAN : Status::INTERRUPTED;
    else if (state == RecordRead::MISSING)
        status = Status::MISSING;
    else if (state == RecordRead::VALID)
        storage_status(STORAGE_CYCLE, false);
    return sirokko::startup_for_cycle(status, configured, [] {
        return storage_healthy() && save_record(kCycleKey, kCycleMagic, uint32_t{0});
    });
}
sirokko::Settings load_settings() {
    sirokko::Settings result{};
    if (load_record(kSettingsKey, kSettingsMagic, &result) && !sirokko::valid_settings(result)) {
        storage_status(STORAGE_SETTINGS, false);
        result = {};
    }
    return result;
}
sirokko::SafetyEnvelope load_factory_envelope() {
    record_codec::Record<sirokko::SafetyEnvelope> envelope_record{};
    const RecordRead state = read_record(kEnvelopeKey, kEnvelopeMagic, &envelope_record);
    const sirokko::SafetyEnvelope &envelope = envelope_record.value;
    if (state == RecordRead::MISSING)
        return {};
    if (state != RecordRead::VALID || !sirokko::valid_profile_draft(envelope)) {
        storage_status(STORAGE_ENVELOPE, false);
        return {};
    }
    return envelope;
}

bool output_begin() {
    bool disabled = true;
    for (const auto pin : {board::kGlow, board::kFuel, board::kFanRequest})
        disabled = gpio_set_level(pin, 0) == ESP_OK && disabled;
    if (!disabled)
        return false;
    gpio_config_t cfg{};
    cfg.pin_bit_mask = (1ULL << board::kGlow) | (1ULL << board::kFuel) | (1ULL << board::kFanRequest);
    cfg.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&cfg) != ESP_OK)
        return false;
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_12_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = board::kPwmFrequencyHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK)
        return false;
    ledc_channel_config_t channel{};
    channel.gpio_num = board::kSpeedPwm;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    return ledc_channel_config(&channel) == ESP_OK;
}
bool digital_write(void *, sirokko::OutputLine line, bool enabled) {
    const gpio_num_t pin = line == sirokko::OutputLine::GLOW   ? board::kGlow
                           : line == sirokko::OutputLine::FUEL ? board::kFuel
                                                               : board::kFanRequest;
    return gpio_set_level(pin, enabled ? 1 : 0) == ESP_OK;
}
bool pwm_write(void *, uint16_t code) {
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, code) == ESP_OK &&
           ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK;
}
bool output_apply(const sirokko::Outputs &out) {
    return g_output_driver.apply(out, nullptr, digital_write, pwm_write);
}
bool button_snapshot(void *, start_stop_button::Snapshot *out) {
    RuntimeView v{};
    if (!out || !runtime_view(&v))
        return false;
    *out = {v.control.state, v.control.start_block, v.control.state_version, v.control.trial_active};
    return true;
}
bool button_start(void *, uint64_t version) {
    sirokko::Grant g{};
    return runtime_issue(sirokko::Action::START, 0x4b45593053544152ULL, &g) && g.state_version == version &&
           runtime_submit(g, sirokko::Action::START, 0, nullptr);
}
bool button_stop(void *) {
    runtime_stop();
    return true;
}
void button_task(void *) {
    start_stop_button::Button button({nullptr, button_snapshot, button_start, button_stop});
    button.reset(gpio_get_level(board::kStartStopButton) == 0, now_ms());
    while (true) {
        button.poll(gpio_get_level(board::kStartStopButton) == 0, now_ms());
        const auto held = button.held_sample();
        xQueueOverwrite(g_button_samples, &held);
        vTaskDelay(pdMS_TO_TICKS(start_stop_button::kTiming.polling_ms));
    }
}
bool start_stop_button_begin() {
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << board::kStartStopButton;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&cfg) == ESP_OK &&
           xTaskCreate(button_task, "key0_button", 3072, nullptr, 4, nullptr) == pdPASS;
}
void sensor_task(void *) {
    while (true) {
        g_sensor_suite.poll(now_ms());
        const auto s = g_sensor_suite.readings();
        xQueueOverwrite(g_sensor_queue, &s);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
struct ControlWork {
    sirokko::Sensors sensors{};
    bool settings_pending{};
    bool cycle_dirty{g_startup == sirokko::Startup::RECOVER};
    uint64_t edited_ms{}, next_id{};
    StoreRequest inflight{};
};
bool enqueue(ControlWork &w, const StoreRequest &r) {
    w.inflight = r;
    g_storage_busy = true;
    if (xQueueSend(g_storage_jobs, &r, 0) == pdTRUE)
        return true;
    g_storage_busy = false;
    storage_status(STORAGE_INTERNAL, false);
    return false;
}
void storage_task(void *) {
    StoreRequest request{};
    while (xQueueReceive(g_storage_jobs, &request, portMAX_DELAY) == pdTRUE) {
        StoreResult result{request.id, request.kind, false};
        if (request.kind == StoreKind::SETTINGS)
            result.ok = save_record(kSettingsKey, kSettingsMagic, request.settings);
        else if (request.kind == StoreKind::PROFILE)
            result.ok = save_record(kEnvelopeKey, kEnvelopeMagic, request.envelope);
        else if (request.kind == StoreKind::CYCLE_ACTIVE || request.kind == StoreKind::CYCLE_CLEAN)
            result.ok = save_record(kCycleKey, kCycleMagic,
                                    uint32_t{request.kind == StoreKind::CYCLE_ACTIVE ? 1U : 0U});
        // Only one storage request is in flight. A full result queue is a fault,
        // not a reason to leave the worker waiting forever.
        if (xQueueSend(g_store_results, &result, 0) != pdTRUE)
            storage_status(STORAGE_INTERNAL, false);
    }
}
void finish_storage(ControlWork &w, const sirokko::TickInput &input) {
    if (!g_storage_busy)
        return;
    StoreResult result{};
    if (xQueueReceive(g_store_results, &result, 0) != pdTRUE)
        return;
    if (result.id != w.inflight.id || result.kind != w.inflight.kind) {
        storage_status(STORAGE_INTERNAL, false);
        return;
    }
    if (w.inflight.kind == StoreKind::PROFILE) {
        if (result.ok &&
            !g_controller->install_safety_envelope(w.inflight.envelope, w.cycle_dirty, input.now_ms))
            storage_status(STORAGE_INTERNAL, false);
    } else if (w.inflight.kind == StoreKind::CYCLE_ACTIVE || w.inflight.kind == StoreKind::CYCLE_CLEAN) {
        if (result.ok)
            w.cycle_dirty = w.inflight.kind == StoreKind::CYCLE_ACTIVE;
    }
    g_storage_busy = false;
}
void grant_storage(ControlWork &w, const sirokko::Snapshot &snap, const sirokko::TickInput &input) {
    if (g_storage_busy)
        return;
    StoreRequest r{};
    r.id = ++w.next_id;
    if (!r.id) {
        storage_status(STORAGE_INTERNAL, false);
        return;
    }
    if (w.cycle_dirty && storage_healthy() &&
        (snap.state == sirokko::State::IDLE || snap.state == sirokko::State::UNCONFIGURED ||
         snap.state == sirokko::State::FAULT || snap.state == sirokko::State::MAINTENANCE) &&
        !snap.outputs.fan_request && !snap.outputs.fuel && !snap.outputs.glow &&
        uxQueueMessagesWaiting(g_command_queue) == 0) {
        r.kind = StoreKind::CYCLE_CLEAN;
        enqueue(w, r);
        return;
    }
    if (w.settings_pending && input.now_ms - w.edited_ms >= 1500) {
        r.kind = StoreKind::SETTINGS;
        r.settings = g_controller->settings();
        if (enqueue(w, r))
            w.settings_pending = false;
    }
}
void control_pass(ControlWork &w) {
    xQueueReceive(g_sensor_queue, &w.sensors, 0);
    sirokko::TickInput input{};
    input.sensors = w.sensors;
    start_stop_button::HeldSample button{};
    if (xQueuePeek(g_button_samples, &button, 0) == pdTRUE) {
        input.local_button_held = button.held;
        input.local_button_valid = button.valid;
        input.local_button_sample_ms = button.sample_ms;
    }
    input.now_ms = now_ms();
    const auto before = g_controller->snapshot(input.now_ms);
    input.cycle_record_ready = w.cycle_dirty;
    input.external_faults =
        (g_output_driver_fault ? static_cast<uint32_t>(sirokko::FAULT_CONTROL_WATCHDOG) : 0) |
        (storage_healthy() ? 0 : static_cast<uint32_t>(sirokko::FAULT_STORAGE));
    if ((g_controller->safety_envelope().provisioned || before.trial_active) && g_startup_complete &&
        (!g_button_ready || !panel_ready()))
        input.external_faults |= sirokko::FAULT_LOCAL_CONTROLS;
    input.stop = g_stop.exchange(false);
    input.exit_maintenance = g_exit_maintenance.exchange(false);
    RuntimeCommand c{};
    bool have = xQueuePeek(g_command_queue, &c, 0) == pdTRUE;
    bool locked = false;
    const bool starting = have && command_policy::starts_cycle(c.action);
    const bool exclusive =
        starting || (have && (c.action == sirokko::Action::OTA || c.action == sirokko::Action::PROFILE));
    if (have && exclusive && g_storage_busy)
        have = false;
    if (have && g_storage_busy && w.inflight.kind == StoreKind::PROFILE)
        have = false;
    if (have && starting && !w.cycle_dirty && storage_healthy() && !input.stop && !g_cancel_commands) {
        StoreRequest request{};
        request.id = ++w.next_id;
        request.kind = StoreKind::CYCLE_ACTIVE;
        if (!request.id)
            storage_status(STORAGE_INTERNAL, false);
        else
            enqueue(w, request);
        have = false; // Keep the start command queued until the dirty marker is durably saved.
    }
    if ((have || g_cancel_commands) && xSemaphoreTake(g_gate_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        locked = true;
        if (g_cancel_commands.exchange(false))
            g_gate.invalidate();
        have = have && xQueueReceive(g_command_queue, &c, 0) == pdTRUE &&
               g_gate.consume(c.grant, input.now_ms, before.state_version);
    } else if (have)
        have = false;
    if (have &&
        (command_policy::starts_cycle(c.action) || c.action == sirokko::Action::OTA ||
         c.action == sirokko::Action::PROFILE) &&
        (g_storage_busy || !storage_healthy()))
        have = false;
    if (have && c.action == sirokko::Action::SETTINGS && g_controller->update_settings(c.settings)) {
        w.settings_pending = true;
        w.edited_ms = input.now_ms;
    }
    if (have) {
        input.start = c.action == sirokko::Action::START;
        input.trial_request = c.action == sirokko::Action::TRIAL_START;
        input.trial_confirm = c.action == sirokko::Action::TRIAL_CONFIRM;
        input.set_trial_pwm = c.action == sirokko::Action::TRIAL_PWM;
        input.trial_pwm_code = c.speed_code;
        if (input.trial_request && (!g_button_ready || !panel_ready()))
            input.external_faults |= sirokko::FAULT_LOCAL_CONTROLS;
        input.set_shutdown_timer = c.action == sirokko::Action::TIMER;
        input.shutdown_after_ms = c.delay_ms;
        input.reset_fault = c.action == sirokko::Action::RESET_FAULT;
        input.enter_maintenance = c.action == sirokko::Action::OTA;
        if (c.action == sirokko::Action::PROFILE && !input.stop && !g_stop && !before.faults &&
            !input.external_faults &&
            (before.state == sirokko::State::IDLE || before.state == sirokko::State::UNCONFIGURED) &&
            sirokko::valid_profile_draft(c.envelope)) {
            StoreRequest r{};
            r.id = ++w.next_id;
            r.kind = StoreKind::PROFILE;
            r.envelope = c.envelope;
            if (r.id)
                enqueue(w, r);
            else
                storage_status(STORAGE_INTERNAL, false);
        }
    }
    // Capture a STOP/fault arriving after command admission but before the only
    // state-owner tick. It must outrank the newly consumed command and storage ACK.
    input.stop = input.stop || g_stop.exchange(false);
    auto snap = g_controller->tick(input);
    finish_storage(w, input);
    snap = g_controller->snapshot(input.now_ms);
    if (!output_apply(snap.outputs)) {
        gpio_set_level(board::kFuel, 0);
        gpio_set_level(board::kGlow, 0);
        g_output_driver_fault = true;
    }
    grant_storage(w, snap, input);
    snap = g_controller->snapshot(input.now_ms);
    if (xSemaphoreTake(g_history_mutex, 0) == pdTRUE) {
        g_history.record(input.now_ms, w.sensors, snap);
        xSemaphoreGive(g_history_mutex);
    }
    const bool ota_safe =
        sirokko::ota_control_safe(snap, w.sensors, g_controller->safety_envelope(), input.now_ms) &&
        storage_healthy() && !g_storage_busy && !g_output_driver_fault && !input.stop && !g_stop &&
        !g_exit_maintenance;
    g_ota_activation.observe(ota_safe);
    if (!ota_safe && !g_stop && !g_exit_maintenance)
        g_ota_activation.acknowledge_revoke();
    g_maintenance_epoch = g_ota_activation.epoch();
    OtaActivation a{};
    if (xQueueReceive(g_ota_activation_queue, &a, 0) == pdTRUE)
        g_ota_activation.queue(a.lease, a.partition, input.now_ms);
    const auto *p = static_cast<const esp_partition_t *>(g_ota_activation.claim_due(input.now_ms));
    if (p) {
        const bool selected =
            p == esp_ota_get_next_update_partition(nullptr) && esp_ota_set_boot_partition(p) == ESP_OK;
        g_ota_activation.finish_commit(selected);
        if (selected)
            esp_restart();
        else {
            storage_status(STORAGE_INTERNAL, false);
            g_exit_maintenance = true;
        }
    }
    if (xSemaphoreTake(g_view_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        g_view.control = snap;
        g_view.local_controls_initialized = g_button_ready && panel_ready();
        g_view.startup_ready = g_boot_ready;
        g_view.storage_busy = g_storage_busy;
        g_view.sensors = w.sensors;
        g_view.settings = g_controller->settings();
        g_view.envelope = g_controller->safety_envelope();
        g_view.now_ms = input.now_ms;
        g_view.storage_ok = storage_healthy();
        g_view.events = g_controller->events();
        g_view.event_count = g_controller->event_count();
        g_view.control.start_block = command_policy::block(g_view, sirokko::Action::START);
        g_wifi_password_block = command_policy::wifi_password_block(g_view);
        xSemaphoreGive(g_view_mutex);
    } else {
        g_wifi_password_block = sirokko::Block::BUSY;
    }
    // Publish the new admission state before a password write can acquire the gate.
    if (locked)
        xSemaphoreGive(g_gate_mutex);
    if (esp_task_wdt_reset() != ESP_OK) {
        gpio_set_level(board::kFuel, 0);
        gpio_set_level(board::kGlow, 0);
        g_output_driver_fault = true;
    }
}
void control_task(void *) {
    ControlWork w{};
    TickType_t wake = xTaskGetTickCount();
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    while (true) {
        control_pass(w);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(sirokko::kControlPeriodMs));
    }
}

} // namespace
bool runtime_history(HistorySnapshot *history) {
    if (!history || !g_history_mutex || xSemaphoreTake(g_history_mutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return false;
    g_history.copy(history);
    xSemaphoreGive(g_history_mutex);
    return true;
}
sirokko::Block runtime_save_wifi_password(const char *password) {
    if (!g_gate_mutex || xSemaphoreTake(g_gate_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return sirokko::Block::BUSY;
    auto blocked = g_wifi_password_block.load();
    // Starts from both the panel and web wait until this write completes. STOP
    // and the control tick remain independent of the command gate.
    if (blocked == sirokko::Block::NONE && !network_save_wifi_password(password))
        blocked = sirokko::Block::STORAGE_UNAVAILABLE;
    xSemaphoreGive(g_gate_mutex);
    return blocked;
}
bool runtime_view(RuntimeView *out) {
    if (!out || !g_view_mutex || xSemaphoreTake(g_view_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return false;
    *out = g_view;
    xSemaphoreGive(g_view_mutex);
    return true;
}
bool runtime_issue(sirokko::Action action, uint64_t hash, sirokko::Grant *out) {
    RuntimeView view{};
    if (!out || !runtime_view(&view) || command_policy::block(view, action) != sirokko::Block::NONE ||
        xSemaphoreTake(g_gate_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return false;
    uint64_t token = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    if (!token)
        token = 1;
    *out = g_gate.issue(action, hash, now_ms(), view.control.state_version, token);
    xSemaphoreGive(g_gate_mutex);
    return out->valid;
}
bool runtime_submit(const sirokko::Grant &grant, sirokko::Action action, uint64_t delay,
                    const sirokko::Settings *settings, uint16_t speed_code) {
    RuntimeView view{};
    if (!grant.valid || grant.action != action || !runtime_view(&view))
        return false;
    if (command_policy::block(view, action) != sirokko::Block::NONE)
        return false;
    if (action == sirokko::Action::SETTINGS && (!settings || !sirokko::valid_settings(*settings)))
        return false;
    if (action == sirokko::Action::TIMER && delay > sirokko::kMaximumTimerMs)
        return false;
    if (action == sirokko::Action::TRIAL_PWM && (!speed_code || speed_code > sirokko::kPwmMaximum))
        return false;
    RuntimeCommand c{};
    c.grant = grant;
    c.action = action;
    c.delay_ms = delay;
    c.speed_code = speed_code;
    c.settings = settings ? *settings : view.settings;
    return xQueueSend(g_command_queue, &c, 0) == pdTRUE;
}

bool runtime_save_profile(const sirokko::Grant &grant, const sirokko::SafetyEnvelope &profile) {
    RuntimeView view{};
    if (!grant.valid || grant.action != sirokko::Action::PROFILE || !runtime_view(&view) ||
        command_policy::block(view, sirokko::Action::PROFILE) != sirokko::Block::NONE ||
        !sirokko::valid_profile_draft(profile))
        return false;
    RuntimeCommand c{};
    c.grant = grant;
    c.action = sirokko::Action::PROFILE;
    c.envelope = profile;
    return xQueueSend(g_command_queue, &c, 0) == pdTRUE;
}
void runtime_stop() {
    g_cancel_commands = true;
    g_stop = true;
    g_ota_activation.revoke_precommit();
    g_maintenance_epoch = 0;
}
uint32_t runtime_maintenance_lease() {
    if (g_stop || g_exit_maintenance || g_output_driver_fault || !storage_healthy() || g_storage_busy ||
        g_ota_activation.commit_started())
        return 0;
    return g_maintenance_epoch;
}
bool runtime_maintenance_safe() {
    return runtime_maintenance_lease() != 0;
}
bool runtime_activate_ota(uint32_t lease, const void *partition) {
    if (!lease || lease != runtime_maintenance_lease() || !partition)
        return false;
    const OtaActivation a{lease, partition};
    return xQueueSend(g_ota_activation_queue, &a, 0) == pdTRUE;
}
void runtime_exit_maintenance() {
    g_exit_maintenance = true;
    g_ota_activation.revoke_precommit();
    g_maintenance_epoch = 0;
}
namespace {
void verify_startup(bool network_ready, bool sensors_ready) {
    esp_ota_img_states_t image_state{};
    const bool pending =
        esp_ota_get_state_partition(esp_ota_get_running_partition(), &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY;
    uint64_t stable_since = 0;
    const uint64_t deadline = now_ms() + 10000;
    while (now_ms() < deadline) {
        RuntimeView view{};
        const bool have_view = runtime_view(&view);
        const auto now = now_ms();
        const sirokko::StartupHealth health{view.envelope.provisioned,
                                            !g_output_driver_fault.load(),
                                            storage_healthy(),
                                            g_button_ready.load(),
                                            panel_ready(),
                                            network_ready,
                                            now,
                                            view.now_ms,
                                            have_view && view.now_ms != 0,
                                            sensors_ready};
        const bool ready = pending ? health.can_confirm_image() : health.controller_ready();
        if (!ready)
            stable_since = 0;
        else if (!stable_since)
            stable_since = now;
        else if (now - stable_since >= 1000) {
            if (pending && esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
                ESP_LOGE(kTag, "Cannot confirm OTA image; heating remains blocked");
                return;
            }
            g_boot_ready = true;
            ESP_LOGI(kTag, "Startup checks passed; version=%s, state=%s, main_stack_free=%u",
                     esp_app_get_description()->version, sirokko::state_name(view.control.state),
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGE(kTag, "Startup checks failed; heating blocked, OTA image not confirmed");
}
} // namespace
extern "C" void app_main() {
    if (!output_begin()) {
        ESP_LOGE(kTag, "Output initialization failed; startup stopped");
        return;
    }
    if (nvs_flash_init() != ESP_OK)
        storage_status(STORAGE_INIT, false);
    g_view_mutex = xSemaphoreCreateMutex();
    g_gate_mutex = xSemaphoreCreateMutex();
    g_history_mutex = xSemaphoreCreateMutex();
    g_sensor_queue = xQueueCreate(1, sizeof(sirokko::Sensors));
    g_button_samples = xQueueCreate(1, sizeof(start_stop_button::HeldSample));
    g_command_queue = xQueueCreate(8, sizeof(RuntimeCommand));
    g_storage_jobs = xQueueCreate(1, sizeof(StoreRequest));
    g_store_results = xQueueCreate(1, sizeof(StoreResult));
    g_ota_activation_queue = xQueueCreate(1, sizeof(OtaActivation));
    if (!g_view_mutex || !g_gate_mutex || !g_history_mutex || !g_sensor_queue || !g_button_samples ||
        !g_command_queue || !g_storage_jobs || !g_store_results || !g_ota_activation_queue) {
        ESP_LOGE(kTag, "Control queue/mutex allocation failed; startup stopped");
        return;
    }
    const uint64_t boot = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    const auto envelope = load_factory_envelope();
    g_startup = load_startup(envelope.provisioned);
    g_controller = new (std::nothrow) sirokko::Controller(envelope, load_settings(), boot, g_startup);
    if (!g_controller) {
        ESP_LOGE(kTag, "Controller allocation failed; startup stopped");
        return;
    }
    const bool sensors_ready = g_sensor_suite.begin();
    if (!sensors_ready)
        ESP_LOGE(kTag, "Sensor initialization failed; heating blocked");
    if (xTaskCreatePinnedToCore(sensor_task, "sensors", 6144, nullptr, 8, nullptr, 0) != pdPASS ||
        xTaskCreatePinnedToCore(control_task, "control", 24576, nullptr, 20, nullptr, 1) != pdPASS ||
        xTaskCreate(storage_task, "storage", 4096, nullptr, 2, nullptr) != pdPASS) {
        storage_status(STORAGE_INTERNAL, false);
        ESP_LOGE(kTag, "Control task allocation failed; heating blocked");
        return;
    }
    g_button_ready = start_stop_button_begin();
    if (!g_button_ready)
        ESP_LOGE(kTag, "START/STOP unavailable; heating blocked");
    if (!panel_begin())
        ESP_LOGE(kTag, "Panel unavailable; heating blocked");
    g_startup_complete = true;
    const bool network_ready = network_begin();
    ESP_LOGI(kTag, "Web interface: %s", network_ready ? "ready" : "unavailable");
    verify_startup(network_ready, sensors_ready);
}
