#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "commissioning.hpp"
#include "driver/gpio.h"
#include "output_driver.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "local_display.hpp"
#include "sensors.hpp"
#include "sirokko/button.hpp"
#include "sirokko/control.hpp"

namespace {
constexpr char kTag[] = "sirokko";
constexpr int32_t kSetpointStepMilliC = 500;

constexpr gpio_num_t kSafetyOk = GPIO_NUM_5;
constexpr gpio_num_t kResetRequest = GPIO_NUM_6;
constexpr gpio_num_t kStartRequest = GPIO_NUM_7;
constexpr gpio_num_t kStopStatus = GPIO_NUM_8;
constexpr gpio_num_t kSetUp = GPIO_NUM_39;
constexpr gpio_num_t kSetDown = GPIO_NUM_40;


struct RuntimeSnapshot {
    sirokko::Status status{};
    sirokko::SensorSample exhaust{};
    sirokko::SensorSample hot_air{};
    sirokko::SensorSample room{};
    int32_t setpoint_milli_c{};
    uint8_t exhaust_status{};
    uint8_t hot_air_status{};
    bool heat_enabled{};
    bool stop_active{};
    bool display_ok{};
    bool output_ok{};
    uint16_t applied_speed{};
};

CommissioningStore g_store;
sirokko::Config g_config{};
SemaphoreHandle_t g_config_mutex{};
SemaphoreHandle_t g_snapshot_mutex{};
RuntimeSnapshot g_snapshot{};
std::atomic<int32_t> g_setpoint_milli_c{};
std::atomic<bool> g_cli_reset{};
OutputDriver g_outputs;
bool g_config_changed{};  // Protected by g_config_mutex; any edit inhibits START until reboot.
bool g_config_edit_safe{};  // Published under the same mutex as each control/output pass.

uint32_t uptime_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool configure_inputs() {
    const uint64_t input_mask = (1ULL << kSafetyOk) | (1ULL << kResetRequest) |
                                (1ULL << kStartRequest) | (1ULL << kStopStatus) |
                                (1ULL << kSetUp) | (1ULL << kSetDown);
    gpio_config_t inputs{};
    inputs.pin_bit_mask = input_mask;
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_DISABLE;
    inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    inputs.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&inputs) == ESP_OK;
}

bool parse_celsius(const char* text, int32_t* milli_c) {
    errno = 0;
    char* end{};
    const double value = std::strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || !std::isfinite(value) ||
        value < static_cast<double>(INT32_MIN) / 1000.0 ||
        value > static_cast<double>(INT32_MAX) / 1000.0) {
        return false;
    }
    *milli_c = static_cast<int32_t>(std::lround(value * 1000.0));
    return true;
}

void print_status() {
    if (xSemaphoreTake(g_snapshot_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    const RuntimeSnapshot snapshot = g_snapshot;
    xSemaphoreGive(g_snapshot_mutex);
    std::printf("state=%s age_ms=%" PRIu32 " faults=%08" PRIX32
                " heat_enabled=%u stop=%u arm=%u trip=%u display=%u recovery_complete=%u\n",
                sirokko::state_name(snapshot.status.state), snapshot.status.state_age_ms,
                snapshot.status.faults, snapshot.heat_enabled, snapshot.stop_active,
                snapshot.status.outputs.arm_ready, snapshot.status.outputs.sw_trip,
                snapshot.display_ok, snapshot.status.recovery_complete);
    std::printf("Tset=%" PRId32 "mC Troom=%" PRId32 "mC(q=%u) Texh=%" PRId32
                "mC(q=%u,sr=%02X) Tair=%" PRId32 "mC(q=%u,sr=%02X)\n",
                snapshot.setpoint_milli_c, snapshot.room.milli_c,
                static_cast<unsigned>(snapshot.room.quality), snapshot.exhaust.milli_c,
                static_cast<unsigned>(snapshot.exhaust.quality), snapshot.exhaust_status,
                snapshot.hot_air.milli_c, static_cast<unsigned>(snapshot.hot_air.quality),
                snapshot.hot_air_status);
    const auto& out = snapshot.status.outputs;
    std::printf("G=%u F=%u M=%u permit=%u requested_speed=%u heartbeat=%u output_ok=%u applied_speed=%u\n",
                out.glow_command, out.fuel_command, out.fan_request, out.fuel_permit,
                out.speed_permille, out.watchdog_heartbeat, snapshot.output_ok, snapshot.applied_speed);
}

void print_help() {
    std::printf(
        "commands:\n"
        "  status | faults | help | reboot\n"
        "  setpoint <celsius>\n"
        "  reset                         (bench only; physical RESET is required for latch)\n"
        "  config show | config validate | config token | config revoke\n"
        "  config set <field> <integer>  (temperatures are milli-C, durations are ms)\n"
        "  GPIO17: 0=LOW, 1..800=PWM, 1000=static HIGH; frequency <=20000Hz and LEDC-achievable\n"
        "  edit/accept/revoke only with fan, fuel and glow OFF; changes require reboot\n"
        "  config accept <8-hex-token>   (then reboot)\n");
}

void cli_task(void*) {
    char line[192]{};
    print_help();
    while (std::fgets(line, sizeof(line), stdin)) {
        line[std::strcspn(line, "\r\n")] = '\0';
        char* save{};
        const char* command = ::strtok_r(line, " ", &save);
        if (!command) continue;
        if (std::strcmp(command, "status") == 0) {
            print_status();
        } else if (std::strcmp(command, "faults") == 0) {
            g_store.print_faults();
        } else if (std::strcmp(command, "help") == 0) {
            print_help();
        } else if (std::strcmp(command, "reset") == 0) {
            g_cli_reset.store(true);
            std::printf("bench reset requested; this cannot reset the physical latch\n");
        } else if (std::strcmp(command, "reboot") == 0) {
            std::printf("rebooting\n");
            std::fflush(stdout);
            esp_restart();
        } else if (std::strcmp(command, "setpoint") == 0) {
            const char* value = ::strtok_r(nullptr, " ", &save);
            int32_t parsed{};
            if (!value || !parse_celsius(value, &parsed)) {
                std::printf("invalid setpoint\n");
                continue;
            }
            if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(200)) != pdTRUE) continue;
            const bool in_range = parsed >= g_config.setpoint_min_milli_c &&
                                  parsed <= g_config.setpoint_max_milli_c;
            xSemaphoreGive(g_config_mutex);
            if (!in_range || !g_store.save_setpoint(parsed)) {
                std::printf("setpoint rejected or NVS write failed\n");
            } else {
                g_setpoint_milli_c.store(parsed);
                std::printf("setpoint=%" PRId32 "mC\n", parsed);
            }
        } else if (std::strcmp(command, "config") == 0) {
            const char* action = ::strtok_r(nullptr, " ", &save);
            if (!action) {
                std::printf("missing config action\n");
                continue;
            }
            if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(500)) != pdTRUE) continue;
            const bool mutation = std::strcmp(action, "set") == 0 ||
                                  std::strcmp(action, "accept") == 0 ||
                                  std::strcmp(action, "revoke") == 0;
            if (mutation && !g_config_edit_safe) {
                std::printf("config rejected: wait for completed purge and safe outputs\n");
                xSemaphoreGive(g_config_mutex);
                continue;
            }
            if (mutation) g_config_changed = true;
            if (std::strcmp(action, "show") == 0) {
                g_store.print_config(g_config);
            } else if (std::strcmp(action, "validate") == 0) {
                std::printf("candidate_errors=%08" PRIX32 " accepted_errors=%08" PRIX32 "\n",
                            sirokko::validate_config(g_config, false),
                            sirokko::validate_config(g_config, true));
            } else if (std::strcmp(action, "token") == 0) {
                std::printf("candidate_token=%08" PRIX32 "\n", g_store.candidate_token(g_config));
            } else if (std::strcmp(action, "revoke") == 0) {
                g_config.commissioning_accepted = false;
                std::printf("revoke=%s; reboot required\n",
                            g_store.save_config(g_config) ? "saved" : "failed");
            } else if (std::strcmp(action, "set") == 0) {
                const char* key = ::strtok_r(nullptr, " ", &save);
                const char* value = ::strtok_r(nullptr, " ", &save);
                if (!key || !value || !g_store.set_field(g_config, key, value) ||
                    !g_store.save_config(g_config)) {
                    std::printf("config set failed\n");
                } else {
                    std::printf("saved; acceptance revoked; token=%08" PRIX32 "; reboot required\n",
                                g_store.candidate_token(g_config));
                }
            } else if (std::strcmp(action, "accept") == 0) {
                const char* token_text = ::strtok_r(nullptr, " ", &save);
                char* end{};
                const unsigned long token = token_text ? std::strtoul(token_text, &end, 16) : 0;
                const bool token_valid = token_text && end && *end == '\0' && token <= UINT32_MAX &&
                                         static_cast<uint32_t>(token) == g_store.candidate_token(g_config);
                if (!token_valid || !g_store.accept_config(g_config, static_cast<uint32_t>(token))) {
                    std::printf("accept rejected: token mismatch or invalid candidate\n");
                } else std::printf("accepted; reboot required\n");
            } else {
                std::printf("unknown config action\n");
            }
            xSemaphoreGive(g_config_mutex);
        } else {
            std::printf("unknown command\n");
        }
        std::printf("> ");
        std::fflush(stdout);
    }
    vTaskDelete(nullptr);
}
}  // namespace

extern "C" void app_main() {
    g_config_mutex = xSemaphoreCreateMutex();
    g_snapshot_mutex = xSemaphoreCreateMutex();
    if (!g_config_mutex || !g_snapshot_mutex || !g_store.begin()) {
        ESP_LOGE(kTag, "NVS/mutex initialization failed; outputs remain off");
        return;
    }

    bool config_integrity{};
    g_config = g_store.load_config(&config_integrity);
    g_setpoint_milli_c.store(g_store.load_setpoint(g_config));
    const bool inputs_initialized = configure_inputs();
    const bool pwm_initialized = g_outputs.initialize(g_config);
    if (!pwm_initialized && sirokko::validate_config(g_config) == sirokko::CONFIG_OK) {
        g_config.commissioning_accepted = false;
    }
    if (pwm_initialized) {
        sirokko::Outputs recovery_outputs{};
        recovery_outputs.fan_request = true;
        recovery_outputs.speed_permille = g_config.recovery_purge_pwm_permille;
        recovery_outputs.sw_trip = true;
        g_outputs.apply(recovery_outputs);
    }
    const sirokko::Config runtime_config = g_config;

    SensorSuite sensors;
    const bool sensors_initialized = sensors.begin();
    LocalDisplay display;
    const bool display_initialized = display.begin();

    uint32_t persisted_faults{};
    const bool persisted_faults_integrity = g_store.load_latched_faults(&persisted_faults);
    uint32_t startup_faults = persisted_faults;
    if (esp_reset_reason() == ESP_RST_BROWNOUT) startup_faults |= sirokko::FAULT_BROWNOUT_RESET;
    if (!config_integrity || !persisted_faults_integrity || !pwm_initialized || !inputs_initialized) {
        startup_faults |= sirokko::FAULT_CONFIG;
    }
    startup_faults |= local_display_startup_fault(display_initialized);
    sirokko::Machine machine(runtime_config, startup_faults);

    sirokko::DebouncedActiveLowButton start_button;
    sirokko::DebouncedActiveLowButton reset_button;
    sirokko::DebouncedActiveLowButton up_button;
    sirokko::DebouncedActiveLowButton down_button;
    bool heat_enabled = false;
    uint32_t last_display_ms = uptime_ms();
    uint32_t last_logged_faults = 0;
    uint32_t last_persisted_faults = persisted_faults;
    sirokko::Outputs last_outputs{};

    xTaskCreate(cli_task, "usb_cli", 6144, nullptr, 3, nullptr);
    ESP_LOGI(kTag, "boot=%" PRIu32 " config_integrity=%d config_errors=%08" PRIX32
                     " sensors_init=%d display_init=%d",
             g_store.boot_count(), config_integrity, sirokko::validate_config(g_config),
             sensors_initialized, display_initialized);

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        const uint32_t now = uptime_ms();
        sensors.poll(now);
        const bool stop_active = gpio_get_level(kStopStatus) != 0;
        const bool start_event = start_button.update(gpio_get_level(kStartRequest) != 0, now);
        reset_button.update(gpio_get_level(kResetRequest) != 0, now);
        const bool up_event = up_button.update(gpio_get_level(kSetUp) != 0, now);
        const bool down_event = down_button.update(gpio_get_level(kSetDown) != 0, now);

        const sirokko::Status before = machine.status(now);
        if (stop_active || before.state == sirokko::State::LOCKOUT) heat_enabled = false;
        if (start_event && last_outputs.arm_ready && !stop_active &&
            before.state == sirokko::State::WAIT_COOLDOWN) {
            heat_enabled = true;
        }

        int32_t setpoint = g_setpoint_milli_c.load();
        if (up_event || down_event) {
            const int32_t delta = up_event ? kSetpointStepMilliC : -kSetpointStepMilliC;
            const int64_t candidate = static_cast<int64_t>(setpoint) + delta;
            if (candidate >= runtime_config.setpoint_min_milli_c && candidate <= runtime_config.setpoint_max_milli_c &&
                g_store.save_setpoint(static_cast<int32_t>(candidate))) {
                setpoint = static_cast<int32_t>(candidate);
                g_setpoint_milli_c.store(setpoint);
            }
        }

        sirokko::Inputs inputs{};
        inputs.now_ms = now;
        inputs.heat_enable = heat_enabled;
        inputs.stop_active = stop_active;
        inputs.reset_request = reset_button.pressed() || g_cli_reset.exchange(false);
        inputs.safety_ok = gpio_get_level(kSafetyOk) != 0;
        inputs.local_ui_ok = display.healthy();
        inputs.setpoint_milli_c = setpoint;
        inputs.exhaust = sensors.exhaust();
        inputs.hot_air = sensors.hot_air();
        inputs.room = sensors.room();

        // Serialize CLI edits with the decision and physical output pass: no START/edit race.
        if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            // No heartbeat service on a stalled pass; independent hardware watchdog remains authoritative.
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(sirokko::kControlLoopPeriodMs));
            continue;
        }
        inputs.configuration_ok = !g_config_changed && inputs_initialized;
        inputs.output_ok = g_outputs.healthy();
        sirokko::Outputs outputs = machine.tick(inputs);
        if (!g_outputs.apply(outputs)) {
            inputs.output_ok = false;
            outputs = machine.tick(inputs);  // Latch and persist FAULT_OUTPUT in this same pass.
        }
        const sirokko::Status status = machine.status(now);
        g_config_edit_safe = !outputs.fan_request && !outputs.fuel_command &&
                             !outputs.fuel_permit && !outputs.glow_command && outputs.speed_permille == 0;
        xSemaphoreGive(g_config_mutex);
        last_outputs = outputs;

        if (status.faults != 0 && status.faults != last_persisted_faults) {
            if (g_store.save_latched_faults(status.faults)) {
                last_persisted_faults = status.faults;
            } else {
                ESP_LOGE(kTag, "failed to persist latched faults");
            }
        }
        if (status.state == sirokko::State::LOCKOUT && status.faults != 0 &&
            status.faults != last_logged_faults) {
            g_store.append_fault(now, status.state, status.faults);
            last_logged_faults = status.faults;
        }
        if (status.faults == 0 && last_persisted_faults != 0) {
            if (g_store.clear_latched_faults()) {
                last_persisted_faults = 0;
            } else {
                ESP_LOGE(kTag, "failed to clear persisted faults after qualified RESET");
            }
        }
        if (status.state != sirokko::State::LOCKOUT) last_logged_faults = 0;

        if (static_cast<uint32_t>(now - last_display_ms) >= 100) {
            display.render(setpoint, inputs.room, inputs.exhaust, inputs.hot_air, status);
            last_display_ms = now;
        }

        if (xSemaphoreTake(g_snapshot_mutex, 0) == pdTRUE) {
            g_snapshot = {status, inputs.exhaust, inputs.hot_air, inputs.room, setpoint,
                          sensors.exhaust_status(), sensors.hot_air_status(), heat_enabled,
                          stop_active, display.healthy(), g_outputs.healthy(), g_outputs.applied_speed()};
            xSemaphoreGive(g_snapshot_mutex);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(sirokko::kControlLoopPeriodMs));
    }
}
