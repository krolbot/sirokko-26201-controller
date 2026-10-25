#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "commissioning.hpp"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "local_display.hpp"
#include "output_driver.hpp"
#include "runtime.hpp"
#include "sensors.hpp"
#include "sirokko/button.hpp"

namespace {
constexpr gpio_num_t kSafety = GPIO_NUM_5, kReset = GPIO_NUM_6, kStart = GPIO_NUM_7,
                     kStop = GPIO_NUM_8, kUp = GPIO_NUM_39, kDown = GPIO_NUM_40;
CommissioningStore g_store;
sirokko::Config g_config{}; // Candidate; only the USB task may edit. Machine receives an immutable copy.
OutputDriver g_outputs;
SensorSuite g_sensors;
LocalDisplay g_display;
QueueHandle_t g_sensor_queue{}, g_setting_queue{};
SemaphoreHandle_t g_snapshot_mutex{}, g_service_mutex{};
sirokko::UiFrame g_frame{};
sirokko::CommandHistory g_history_snapshot{};
sirokko::OperatorSettings g_initial_settings{};
sirokko::CommandCounters g_initial_counters{};
std::atomic<bool> g_storage_ok{true}, g_settings_ok{}, g_config_changed{}, g_remote_stop{};
std::atomic<uint32_t> g_display_progress{}, g_passes{}, g_settings_revision{};
uint32_t g_startup_faults{};
bool g_inputs_ok{}, g_config_integrity{};
struct SensorFrame { sirokko::SensorSample room, exhaust, air; uint8_t exhaust_fault, air_fault; };
enum class LeaseState : uint8_t { IDLE, REQUESTED, GRANTED, REJECTED, RELEASE };
std::atomic<LeaseState> g_lease_state{LeaseState::IDLE};
std::atomic<FlashPurpose> g_lease_purpose{FlashPurpose::LOCAL_SETTINGS};
std::atomic<sirokko::ServiceResult> g_lease_result{sirokko::ServiceResult::BUSY};
uint32_t ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
bool off(const sirokko::Outputs& o) { return !o.fuel_command && !o.fuel_permit && !o.glow_command; }

void sensor_task(void*) {
    while (true) {
        g_sensors.poll(ms()); // Blocking SPI/1-wire APIs live here, never in the control task.
        SensorFrame f{g_sensors.room(), g_sensors.exhaust(), g_sensors.hot_air(),
                      g_sensors.exhaust_status(), g_sensors.hot_air_status()};
        xQueueOverwrite(g_sensor_queue, &f);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void display_task(void*) {
    while (true) {
        sirokko::UiFrame f{};
        if (runtime_snapshot(&f)) g_display.render(f);
        g_display_progress.store(ms());
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}
void control_task(void*) {
    const sirokko::Config config = g_config;
    sirokko::Machine machine(config, g_startup_faults);
    sirokko::CommandHistory history;
    history.begin(g_store.boot_count(), g_initial_counters);
    sirokko::LocalMenu menu;
    sirokko::OperatorSettings settings = g_initial_settings;
    sirokko::DebouncedActiveLowButton start, reset, up, down;
    sirokko::ServiceLease ota_lease;
    bool local_lease{};
    bool heat{};
    SensorFrame sensors{};
    sirokko::Outputs previous{};
    TickType_t wake = xTaskGetTickCount();
    esp_task_wdt_add(nullptr);
    while (true) {
        const uint32_t now = ms();
        xQueueReceive(g_sensor_queue, &sensors, 0);
        const bool stopped = gpio_get_level(kStop) != 0;
        const bool start_event = start.update(gpio_get_level(kStart) != 0, now);
        reset.update(gpio_get_level(kReset) != 0, now);
        const bool u = up.update(gpio_get_level(kUp) != 0, now);
        const bool d = down.update(gpio_get_level(kDown) != 0, now);
        const bool remote_stop = g_remote_stop.exchange(false);
        const auto before = machine.status(now);
        if (stopped || remote_stop || before.state == sirokko::State::LOCKOUT ||
            before.state == sirokko::State::POST_PURGE) heat = false;
        auto lease_state = g_lease_state.load();
        if (lease_state == LeaseState::RELEASE) {
            ota_lease.release(); local_lease = false;
            g_lease_state.store(LeaseState::IDLE);
        }
        // Every START is an edge-local decision, never a queued/delayed ignition request.
        if (start_event && previous.arm_ready && !stopped && !remote_stop &&
            !local_lease && !ota_lease.held() && before.state == sirokko::State::WAIT_COOLDOWN) heat = true;
        sirokko::OperatorSettings requested{};
        if (xQueueReceive(g_setting_queue, &requested, 0) == pdTRUE && sirokko::operator_settings_valid(requested, config)) {
            settings = requested; g_settings_revision.fetch_add(1);
        }
        const auto action = menu.tick({u, d, up.pressed(), down.pressed(), stopped || remote_stop}, now, settings, config);
        if (action.settings_changed) g_settings_revision.fetch_add(1);
        if (action.reset_counters) { history.reset_counters(); g_settings_revision.fetch_add(1); }
        sirokko::Inputs in{};
        in.now_ms = now;
        in.heat_enable = heat;
        in.stop_active = stopped || remote_stop;
        in.reset_request = reset.pressed(); // Only the physical RESET can participate in latch qualification.
        in.safety_ok = gpio_get_level(kSafety) != 0;
        in.local_ui_ok = g_display.healthy() && static_cast<uint32_t>(now - g_display_progress.load()) <= 250;
        in.output_ok = g_outputs.healthy();
        in.configuration_ok = !g_config_changed.load() && g_inputs_ok && g_storage_ok.load() && g_settings_ok.load();
        in.setpoint_milli_c = settings.setpoint_milli_c;
        in.hysteresis_milli_c = settings.hysteresis_milli_c;
        in.exhaust = sensors.exhaust;
        in.hot_air = sensors.air;
        in.room = sirokko::calibrated_room(sensors.room, settings.room_offset_milli_c, now, config.sensor_stale_ms);
        if (lease_state == LeaseState::REQUESTED) {
            sirokko::ServiceResult result = sirokko::ServiceResult::BUSY;
            if (!start_event && !heat) {
                if (g_lease_purpose.load() == FlashPurpose::OTA) result = ota_lease.acquire(before, in, config, g_outputs.healthy());
                else if (off(before.outputs) && !before.outputs.fan_request && before.outputs.speed_permille == 0) {
                    local_lease = true;
                    result = sirokko::ServiceResult::OK;
                }
            }
            g_lease_result.store(result);
            LeaseState expected = LeaseState::REQUESTED;
            if (!g_lease_state.compare_exchange_strong(expected, result == sirokko::ServiceResult::OK ? LeaseState::GRANTED : LeaseState::REJECTED)) {
                local_lease = false; ota_lease.release(); // Caller timed out/cancelled; never orphan a lease.
            }
        }
        in.maintenance_inhibit = local_lease || ota_lease.held();
        auto outputs = machine.tick(in);
        if (ota_lease.held()) {
            outputs.fuel_command = outputs.fuel_permit = outputs.glow_command = false;
            outputs.arm_ready = outputs.watchdog_heartbeat = false;
            outputs.sw_trip = true;
        }
        if (!g_outputs.apply(outputs)) {
            in.output_ok = false;
            outputs = machine.tick(in);
        }
        auto status = machine.status(now);
        status.outputs = outputs;
        if (status.state == sirokko::State::WAIT_COOLDOWN || status.state == sirokko::State::POST_PURGE) heat = false;
        previous = outputs;
        history.observe(status, static_cast<uint64_t>(esp_timer_get_time() / 1000ULL),
                        stopped ? sirokko::StopReason::LOCAL_STOP : remote_stop ? sirokko::StopReason::REMOTE_STOP : sirokko::StopReason::NONE);
        if (xSemaphoreTake(g_snapshot_mutex, 0) == pdTRUE) {
            g_frame = {};
            g_frame.status = status; g_frame.config = config; g_frame.settings = settings;
            g_frame.raw_room = sensors.room; g_frame.room = in.room;
            g_frame.exhaust = in.exhaust; g_frame.hot_air = in.hot_air;
            g_frame.counters = history.counters();
            g_frame.last_event = history.event(history.count() ? history.count() - 1 : 0);
            g_frame.now_ms = now; g_frame.boot_id = g_store.boot_count(); g_frame.session_id = history.session_id();
            g_frame.profile_token = g_store.candidate_token(config);
            g_frame.settings_revision = g_settings_revision.load();
            g_frame.exhaust_fault = sensors.exhaust_fault; g_frame.hot_air_fault = sensors.air_fault;
            g_frame.safety_ok = in.safety_ok; g_frame.stop_active = stopped;
            g_frame.settings_ok = g_settings_ok.load(); g_frame.storage_ok = g_storage_ok.load();
            g_frame.network_configured = service_network_configured(); g_frame.network_connected = service_network_connected();
            g_frame.update_active = ota_lease.held(); g_frame.page = menu.page(); g_frame.selection = menu.selection();
            g_history_snapshot = history;
            xSemaphoreGive(g_snapshot_mutex);
        }
        g_passes.fetch_add(1);
        esp_task_wdt_reset();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(sirokko::kControlLoopPeriodMs));
    }
}
void storage_task(void*) {
    uint32_t persisted_faults{};
    g_store.load_latched_faults(&persisted_faults);
    uint32_t persisted_revision = g_settings_revision.load();
    uint32_t last_snapshot = ms(), last_attempt = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
        sirokko::UiFrame f{};
        if (!runtime_snapshot(&f)) continue;
        const uint32_t revision = f.settings_revision;
        const bool fault_change = f.status.faults != persisted_faults;
        const bool rare_snapshot = static_cast<uint32_t>(ms() - last_snapshot) >= 900000;
        if (!fault_change && revision == persisted_revision && !rare_snapshot) continue;
        if (static_cast<uint32_t>(ms() - last_attempt) < (g_storage_ok.load() ? 5000u : 60000u)) continue;
        last_attempt = ms();
        // No flash write while ignition, RUN or required fan purge is active. Hardware watchdog is never serviced by this worker.
        if (runtime_acquire_flash(FlashPurpose::LOCAL_SETTINGS) != sirokko::ServiceResult::OK) continue;
        sirokko::CommandHistory history;
        if (xSemaphoreTake(g_snapshot_mutex, pdMS_TO_TICKS(50)) != pdTRUE) { runtime_release_flash(); continue; }
        f = g_frame; history = g_history_snapshot;
        xSemaphoreGive(g_snapshot_mutex);
        bool ok = true;
        if (fault_change) ok = f.status.faults ? g_store.save_latched_faults(f.status.faults) : g_store.clear_latched_faults();
        if (ok && g_settings_ok.load() && revision != persisted_revision) ok = g_store.save_operator_settings(f.settings, f.config);
        if (ok) ok = g_store.save_counters(f.counters) && g_store.save_history(history);
        if (ok) { persisted_faults = f.status.faults; persisted_revision = revision; last_snapshot = ms(); }
        else g_storage_ok.store(false);
        runtime_release_flash();
    }
}
void print_status() {
    sirokko::UiFrame f{};
    if (!runtime_snapshot(&f)) return;
    std::printf("version=%s state=%s faults=%08lX boot=%lu session=%lu uptime_ms=%lu\n",
        esp_app_get_description()->version, sirokko::state_name(f.status.state), static_cast<unsigned long>(f.status.faults),
        static_cast<unsigned long>(f.boot_id), static_cast<unsigned long>(f.session_id), static_cast<unsigned long>(f.now_ms));
    std::printf("room_raw_mC=%ld room_mC=%ld q=%u age_ms=%lu offset_mC=%ld setpoint_mC=%ld hysteresis_mC=%ld\n",
        static_cast<long>(f.raw_room.milli_c), static_cast<long>(f.room.milli_c), static_cast<unsigned>(f.room.quality),
        static_cast<unsigned long>(f.now_ms - f.raw_room.sample_ms), static_cast<long>(f.settings.room_offset_milli_c),
        static_cast<long>(f.settings.setpoint_milli_c), static_cast<long>(f.settings.hysteresis_milli_c));
    const auto& o = f.status.outputs;
    std::printf("exh_mC=%ld q=%u fault=%02X age_ms=%lu air_mC=%ld q=%u fault=%02X age_ms=%lu\n",
        static_cast<long>(f.exhaust.milli_c), static_cast<unsigned>(f.exhaust.quality), f.exhaust_fault,
        static_cast<unsigned long>(f.now_ms - f.exhaust.sample_ms), static_cast<long>(f.hot_air.milli_c),
        static_cast<unsigned>(f.hot_air.quality), f.hot_air_fault, static_cast<unsigned long>(f.now_ms - f.hot_air.sample_ms));
    std::printf("COMMANDS fan=%u fuel=%u glow=%u permit=%u safety_input=%u storage=%u settings=%u\n",
        o.speed_permille, o.fuel_command, o.glow_command, o.fuel_permit, f.safety_ok, f.storage_ok, f.settings_ok);
}
void help() {
    std::printf("status | history | counters | help | reboot\n"
        "settings setpoint|hysteresis|offset <integer milli-C> (RAM; deferred safe NVS snapshot)\n"
        "config show|validate|token|revoke|set <field> <integer>|accept <8hex>\n"
        "config changes revoke acceptance and require reboot; profile_revision and run_low_qualified need measured engineering approval\n"
        "network import <base64 JSON> (local USB only, stopped outputs, reboot required; NEVER paste credentials into chat)\n"
        "No software START/RESET, direct fuel/glow or safety bypass. GPIO17 0=LOW,1..800=PWM,1000=static HIGH.\n");
}
void cli_task(void*) {
    auto* line = static_cast<char*>(std::calloc(1, 16384));
    if (!line) { vTaskDelete(nullptr); return; }
    help();
    while (std::fgets(line, 16384, stdin)) {
        line[std::strcspn(line, "\r\n")] = 0;
        char* save{};
        const char* command = ::strtok_r(line, " ", &save);
        if (!command) continue;
        if (std::strcmp(command, "status") == 0) print_status();
        else if (std::strcmp(command, "help") == 0) help();
        else if (std::strcmp(command, "history") == 0) {
            sirokko::CommandHistory h;
            if (xSemaphoreTake(g_snapshot_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                h = g_history_snapshot; xSemaphoreGive(g_snapshot_mutex);
                for (size_t i = 0; i < h.count(); ++i) {
                    const auto e = h.event(i);
                    std::printf("boot=%lu session=%lu uptime_ms=%llu %s->%s reason=%u faults=%08lX\n",
                        static_cast<unsigned long>(e.boot_id), static_cast<unsigned long>(e.session_id),
                        static_cast<unsigned long long>(e.uptime_ms), sirokko::state_name(e.from), sirokko::state_name(e.to),
                        static_cast<unsigned>(e.reason), static_cast<unsigned long>(e.faults));
                }
            }
        } else if (std::strcmp(command, "counters") == 0) {
            sirokko::UiFrame f{};
            if (runtime_snapshot(&f)) std::printf("COMMAND_ACCOUNTING starts=%llu failed=%llu run_ms=%llu glow_ms=%llu (not measured relay current)\n",
                static_cast<unsigned long long>(f.counters.starts), static_cast<unsigned long long>(f.counters.failed_ignitions),
                static_cast<unsigned long long>(f.counters.run_ms), static_cast<unsigned long long>(f.counters.glow_ms));
        } else if (std::strcmp(command, "settings") == 0) {
            const auto* key = ::strtok_r(nullptr, " ", &save);
            const auto* value = ::strtok_r(nullptr, " ", &save);
            sirokko::UiFrame f{};
            char* end{};
            const long long n = value ? std::strtoll(value, &end, 10) : INT64_MAX;
            bool ok = key && value && end != value && end && !*end && n >= INT32_MIN && n <= INT32_MAX && runtime_snapshot(&f);
            if (ok) {
                if (std::strcmp(key, "setpoint") == 0) f.settings.setpoint_milli_c = n;
                else if (std::strcmp(key, "hysteresis") == 0) f.settings.hysteresis_milli_c = n;
                else if (std::strcmp(key, "offset") == 0) f.settings.room_offset_milli_c = n;
                else ok = false;
                ok = ok && sirokko::operator_settings_valid(f.settings, f.config) && xQueueSend(g_setting_queue, &f.settings, 0) == pdTRUE;
            }
            std::printf("settings=%s\n", ok ? "QUEUED_NOT_YET_SAVED" : "REJECTED");
        } else if (std::strcmp(command, "config") == 0) {
            const auto* action = ::strtok_r(nullptr, " ", &save);
            if (!action) continue;
            if (std::strcmp(action, "show") == 0) g_store.print_config(g_config);
            else if (std::strcmp(action, "validate") == 0) std::printf("candidate=%08lX accepted=%08lX\n",
                static_cast<unsigned long>(sirokko::validate_config(g_config, false)), static_cast<unsigned long>(sirokko::validate_config(g_config)));
            else if (std::strcmp(action, "token") == 0) std::printf("token=%08lX\n", static_cast<unsigned long>(g_store.candidate_token(g_config)));
            else if (runtime_acquire_flash(FlashPurpose::LOCAL_SETTINGS) == sirokko::ServiceResult::OK) {
                g_config_changed.store(true);
                bool ok{};
                if (std::strcmp(action, "revoke") == 0) { g_config.commissioning_accepted = false; ok = g_store.save_config(g_config); }
                else if (std::strcmp(action, "set") == 0) {
                    const auto* key = ::strtok_r(nullptr, " ", &save); const auto* value = ::strtok_r(nullptr, " ", &save);
                    ok = key && value && g_store.set_field(g_config, key, value) && g_store.save_config(g_config);
                } else if (std::strcmp(action, "accept") == 0) {
                    const auto* token = ::strtok_r(nullptr, " ", &save); char* end{};
                    const unsigned long n = token ? std::strtoul(token, &end, 16) : 0;
                    ok = token && std::strlen(token) == 8 && end && !*end && n <= UINT32_MAX && g_store.accept_config(g_config, n);
                }
                std::printf("config=%s; reboot required\n", ok ? "SAVED" : "REJECTED");
                runtime_release_flash();
            } else std::printf("BUSY: wait for full stop/purge\n");
        } else if (std::strcmp(command, "network") == 0) {
            const auto* action = ::strtok_r(nullptr, " ", &save); const auto* encoded = ::strtok_r(nullptr, " ", &save);
            bool ok{};
            if (action && encoded && std::strcmp(action, "import") == 0 && runtime_acquire_flash(FlashPurpose::LOCAL_SETTINGS) == sirokko::ServiceResult::OK) {
                ok = service_network_import(encoded); runtime_release_flash();
            }
            std::printf("network=%s; reboot required\n", ok ? "SAVED" : "REJECTED");
        } else if (std::strcmp(command, "reboot") == 0) {
            if (runtime_acquire_flash(FlashPurpose::LOCAL_SETTINGS) == sirokko::ServiceResult::OK) esp_restart();
            std::printf("BUSY\n");
        } else std::printf("unknown command\n");
        std::memset(line, 0, 16384);
        std::fflush(stdout);
    }
    std::free(line); vTaskDelete(nullptr);
}
void network_start_task(void*) { service_network_begin(); vTaskDelete(nullptr); }
void health_task(void*) {
    const auto* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) { vTaskDelete(nullptr); return; }
    const uint32_t first_pass = runtime_control_passes(), started = ms();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));
        sirokko::UiFrame f{};
        if (!runtime_snapshot(&f)) continue;
        const bool fresh = f.room.quality == sirokko::SensorQuality::OK && f.exhaust.quality == sirokko::SensorQuality::OK &&
            f.hot_air.quality == sirokko::SensorQuality::OK && ms() - f.room.sample_ms <= f.config.sensor_stale_ms &&
            ms() - f.exhaust.sample_ms <= f.config.sensor_stale_ms && ms() - f.hot_air.sample_ms <= f.config.sensor_stale_ms;
        if (ms() - started >= 5000 && runtime_control_passes() - first_pass >= 200 && g_display.healthy() && fresh &&
            g_storage_ok.load() && g_settings_ok.load() && g_config_integrity && g_outputs.healthy()) {
            esp_ota_mark_app_valid_cancel_rollback(); vTaskDelete(nullptr); return;
        }
        if (ms() - started >= 60000 && off(f.status.outputs) && !f.status.outputs.fan_request) {
            esp_ota_mark_app_invalid_rollback_and_reboot(); // Existing NVS fault/profile records are never erased.
        }
    }
}
} // namespace
bool runtime_snapshot(sirokko::UiFrame* frame) {
    if (!g_snapshot_mutex || xSemaphoreTake(g_snapshot_mutex, 0) != pdTRUE) return false;
    *frame = g_frame; xSemaphoreGive(g_snapshot_mutex); return true;
}
void runtime_remote_stop() { g_remote_stop.store(true); }
uint32_t runtime_control_passes() { return g_passes.load(); }
sirokko::ServiceResult runtime_acquire_flash(FlashPurpose purpose) {
    if (xSemaphoreTake(g_service_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return sirokko::ServiceResult::BUSY;
    if (g_lease_state.load() != LeaseState::IDLE) { xSemaphoreGive(g_service_mutex); return sirokko::ServiceResult::BUSY; }
    g_lease_purpose.store(purpose); g_lease_state.store(LeaseState::REQUESTED);
    const uint32_t started = ms();
    while (g_lease_state.load() == LeaseState::REQUESTED && static_cast<uint32_t>(ms() - started) < 1000) vTaskDelay(pdMS_TO_TICKS(5));
    if (g_lease_state.load() == LeaseState::GRANTED) return sirokko::ServiceResult::OK;
    LeaseState expected = LeaseState::REQUESTED;
    g_lease_state.compare_exchange_strong(expected, LeaseState::IDLE);
    if (g_lease_state.load() == LeaseState::GRANTED) return sirokko::ServiceResult::OK;
    const auto result = g_lease_result.load();
    g_lease_state.store(LeaseState::IDLE); xSemaphoreGive(g_service_mutex); return result;
}
void runtime_release_flash() { g_lease_state.store(LeaseState::RELEASE); xSemaphoreGive(g_service_mutex); }
extern "C" void app_main() {
    g_snapshot_mutex = xSemaphoreCreateMutex(); g_service_mutex = xSemaphoreCreateMutex();
    g_sensor_queue = xQueueCreate(1, sizeof(SensorFrame)); g_setting_queue = xQueueCreate(1, sizeof(sirokko::OperatorSettings));
    if (!g_snapshot_mutex || !g_service_mutex || !g_sensor_queue || !g_setting_queue || !g_store.begin()) {
        ESP_LOGE("sirokko", "startup storage/queue failure; hardware outputs remain inhibited"); return;
    }
    g_config = g_store.load_config(&g_config_integrity);
    bool settings_ok{};
    g_initial_settings = g_store.load_operator_settings(g_config, &settings_ok); g_settings_ok.store(settings_ok);
    const bool counters_ok = g_store.load_counters(&g_initial_counters);
    uint32_t faults{};
    const bool faults_ok = g_store.load_latched_faults(&faults);
    g_startup_faults = faults;
    gpio_config_t input{};
    input.pin_bit_mask = (1ULL << kSafety) | (1ULL << kReset) | (1ULL << kStart) | (1ULL << kStop) | (1ULL << kUp) | (1ULL << kDown);
    input.mode = GPIO_MODE_INPUT; input.intr_type = GPIO_INTR_DISABLE;
    g_inputs_ok = gpio_config(&input) == ESP_OK;
    const bool output_ok = g_outputs.initialize(g_config);
    if (!g_inputs_ok || !g_config_integrity || !settings_ok || !counters_ok || !faults_ok || !output_ok) g_startup_faults |= sirokko::FAULT_CONFIG;
    if (esp_reset_reason() == ESP_RST_BROWNOUT) g_startup_faults |= sirokko::FAULT_BROWNOUT_RESET;
    if (output_ok) {
        sirokko::Outputs recovery{}; recovery.fan_request = true; recovery.speed_permille = g_config.recovery_purge_pwm_permille; recovery.sw_trip = true;
        g_outputs.apply(recovery);
    }
    g_sensors.begin();
    const bool display_ok = g_display.begin();
    g_startup_faults |= local_display_startup_fault(display_ok);
    g_display_progress.store(ms());
    if (xTaskCreate(sensor_task, "sensors", 4096, nullptr, 5, nullptr) != pdPASS ||
        xTaskCreate(display_task, "tft", 6144, nullptr, 2, nullptr) != pdPASS ||
        xTaskCreatePinnedToCore(control_task, "control", 8192, nullptr, 18, nullptr, 1) != pdPASS) {
        g_outputs.apply({}); return;
    }
    xTaskCreate(storage_task, "storage", 6144, nullptr, 2, nullptr);
    xTaskCreate(cli_task, "usb_service", 8192, nullptr, 2, nullptr);
    xTaskCreate(network_start_task, "net_init", 6144, nullptr, 1, nullptr);
    xTaskCreate(health_task, "ota_health", 4096, nullptr, 2, nullptr);
}
