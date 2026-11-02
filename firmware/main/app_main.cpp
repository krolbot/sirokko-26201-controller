#include "runtime_api.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <new>

#include "board.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensors.hpp"

namespace {
constexpr char kTag[] = "sirokko";
constexpr uint32_t kSettingsMagic = 0x53495231;

struct PersistedSettings {
    uint32_t magic;
    sirokko::Settings settings;
    uint64_t checksum;
};

struct RuntimeCommand {
    sirokko::Grant grant;
    sirokko::Action action;
    uint64_t delay_ms;
    sirokko::Settings settings;
};

SensorSuite g_sensor_suite;
QueueHandle_t g_sensor_queue{};
QueueHandle_t g_command_queue{};
QueueHandle_t g_settings_queue{};
SemaphoreHandle_t g_view_mutex{};
SemaphoreHandle_t g_gate_mutex{};
RuntimeView g_view{};
sirokko::Controller* g_controller{};
sirokko::CommandGate g_gate;
std::atomic<bool> g_stop{};
std::atomic<bool> g_exit_maintenance{};
std::atomic<bool> g_output_driver_fault{};
std::atomic<uint64_t> g_control_passes{};

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

uint64_t checksum(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    return hash;
}

bool safe_settings(const sirokko::Settings& value) {
    return value.duration_ms > 0 && value.duration_ms <= sirokko::kMaximumTimerMs &&
           value.room_setpoint_milli_c >= 5000 && value.room_setpoint_milli_c <= 40000 &&
           value.room_hysteresis_milli_c >= 100 && value.room_hysteresis_milli_c <= 10000;
}

sirokko::Settings load_settings() {
    sirokko::Settings result{};
    nvs_handle_t nvs{};
    if (nvs_open("sirokko", NVS_READONLY, &nvs) != ESP_OK) return result;
    PersistedSettings persisted{};
    size_t size = sizeof(persisted);
    const esp_err_t error = nvs_get_blob(nvs, "settings_v1", &persisted, &size);
    nvs_close(nvs);
    const uint64_t expected = checksum(&persisted, offsetof(PersistedSettings, checksum));
    if (error == ESP_OK && size == sizeof(persisted) && persisted.magic == kSettingsMagic &&
        persisted.checksum == expected && safe_settings(persisted.settings)) return persisted.settings;
    return result;
}

bool save_settings(const sirokko::Settings& value) {
    if (!safe_settings(value)) return false;
    PersistedSettings persisted{kSettingsMagic, value, 0};
    persisted.checksum = checksum(&persisted, offsetof(PersistedSettings, checksum));
    nvs_handle_t nvs{};
    if (nvs_open("sirokko", NVS_READWRITE, &nvs) != ESP_OK) return false;
    const bool ok = nvs_set_blob(nvs, "settings_v1", &persisted, sizeof(persisted)) == ESP_OK &&
                    nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

bool load_or_create_password(char password[21], bool* created) {
    *created = false;
    nvs_handle_t nvs{};
    if (nvs_open("sirokko", NVS_READWRITE, &nvs) != ESP_OK) return false;
    size_t size = 21;
    esp_err_t error = nvs_get_str(nvs, "ap_password", password, &size);
    if (error == ESP_OK && size == 21 && std::strlen(password) == 20) {
        nvs_close(nvs);
        return true;
    }
    constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t i = 0; i < 20; ++i) password[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    password[20] = 0;
    const bool ok = nvs_set_str(nvs, "ap_password", password) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    *created = ok;
    return ok;
}

bool output_begin() {
    gpio_config_t outputs{};
    outputs.pin_bit_mask = (1ULL << board::kGlow) | (1ULL << board::kFuel) |
                           (1ULL << board::kFanRequest) | (1ULL << board::kExhaustCs) |
                           (1ULL << board::kAirCs);
    outputs.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&outputs) != ESP_OK) return false;
    gpio_set_level(board::kGlow, 0);
    gpio_set_level(board::kFuel, 0);
    gpio_set_level(board::kFanRequest, 0);
    gpio_set_level(board::kExhaustCs, 1);
    gpio_set_level(board::kAirCs, 1);

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_12_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = board::kPwmFrequencyHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK) return false;
    ledc_channel_config_t channel{};
    channel.gpio_num = board::kSpeedPwm;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    channel.hpoint = 0;
    return ledc_channel_config(&channel) == ESP_OK;
}

bool output_apply(const sirokko::Outputs& requested) {
    const uint32_t duty = std::min<uint16_t>(requested.speed_code, sirokko::kPwmMaximum);
    // Dangerous commands are removed first. Fan/PWM are then updated, and only then may relays turn on.
    if (!requested.fuel) gpio_set_level(board::kFuel, 0);
    if (!requested.glow) gpio_set_level(board::kGlow, 0);
    gpio_set_level(board::kFanRequest, requested.fan_request ? 1 : 0);
    bool ok = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) == ESP_OK &&
              ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK;
    if (requested.glow) gpio_set_level(board::kGlow, 1);
    if (requested.fuel) gpio_set_level(board::kFuel, 1);
    return ok;
}

void sensor_task(void*) {
    while (true) {
        g_sensor_suite.poll(now_ms());
        const sirokko::Sensors readings = g_sensor_suite.readings();
        xQueueOverwrite(g_sensor_queue, &readings);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void storage_task(void*) {
    sirokko::Settings settings{};
    while (true) {
        if (xQueueReceive(g_settings_queue, &settings, portMAX_DELAY) == pdTRUE && !save_settings(settings))
            ESP_LOGE(kTag, "settings persistence failed");
    }
}

void control_task(void*) {
    sirokko::Sensors sensors{};
    sirokko::Settings settings = load_settings();
    TickType_t wake = xTaskGetTickCount();
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    while (true) {
        xQueueReceive(g_sensor_queue, &sensors, 0);
        RuntimeCommand command{};
        bool have_command = xQueueReceive(g_command_queue, &command, 0) == pdTRUE;
        sirokko::TickInput input{};
        input.now_ms = now_ms();
        input.sensors = sensors;
        input.external_faults = g_output_driver_fault.load()
            ? static_cast<uint32_t>(sirokko::FAULT_CONTROL_WATCHDOG) : 0U;
        input.stop = g_stop.exchange(false);

        bool gate_locked = false;
        sirokko::Snapshot before = g_controller->snapshot(input.now_ms);
        if (have_command && xSemaphoreTake(g_gate_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            gate_locked = true;
            have_command = g_gate.consume(command.grant, input.now_ms, before.state_version);
        } else if (have_command) {
            have_command = false;
        }
        if (have_command && command.action == sirokko::Action::SETTINGS && safe_settings(command.settings)) {
            settings = command.settings;
            g_controller->update_settings(settings);
            xQueueOverwrite(g_settings_queue, &settings);
        }

        input.stop = input.stop || g_stop.exchange(false);
        input.exit_maintenance = g_exit_maintenance.exchange(false);
        if (have_command) {
            input.start = command.action == sirokko::Action::START;
            input.arm_delayed = command.action == sirokko::Action::ARM;
            input.delay_ms = command.delay_ms;
            input.reset_fault = command.action == sirokko::Action::RESET_FAULT;
            input.enter_maintenance = command.action == sirokko::Action::OTA;
        }
        const sirokko::Snapshot snapshot = g_controller->tick(input);
        if (gate_locked) {
            if (have_command && command.action == sirokko::Action::RESET_FAULT &&
                before.state == sirokko::State::FAULT && snapshot.state == sirokko::State::IDLE)
                g_gate.invalidate();
            xSemaphoreGive(g_gate_mutex);
        }
        if (!output_apply(snapshot.outputs)) {
            ESP_LOGE(kTag, "output driver update failed; forcing relay commands LOW");
            gpio_set_level(board::kFuel, 0);
            gpio_set_level(board::kGlow, 0);
            g_output_driver_fault.store(true);
        }

        if (xSemaphoreTake(g_view_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            g_view = {snapshot, sensors, settings, input.now_ms};
            xSemaphoreGive(g_view_mutex);
        }
        g_control_passes.fetch_add(1);
        esp_task_wdt_reset();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(sirokko::kControlPeriodMs));
    }
}

bool start_access_point(const char* password) {
    if (esp_netif_init() != ESP_OK) return false;
    const esp_err_t loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_ap()) return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) return false;

    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) return false;
    char ssid[33]{};
    std::snprintf(ssid, sizeof(ssid), "Sirokko-%02X%02X%02X", mac[3], mac[4], mac[5]);
    wifi_config_t config{};
    std::memcpy(config.ap.ssid, ssid, std::strlen(ssid));
    config.ap.ssid_len = std::strlen(ssid);
    std::memcpy(config.ap.password, password, std::strlen(password));
    config.ap.channel = 6;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.capable = true;
    config.ap.pmf_cfg.required = true;
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK || esp_wifi_start() != ESP_OK) return false;
    ESP_LOGI(kTag, "AP %s ready at http://192.168.4.1", ssid);
    return true;
}

void ota_health_task(void*) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        vTaskDelete(nullptr);
        return;
    }
    const uint64_t initial = g_control_passes.load();
    vTaskDelay(pdMS_TO_TICKS(5000));
    RuntimeView view{};
    if (g_control_passes.load() - initial >= 200 && runtime_view(&view) &&
        !view.control.outputs.fuel && !view.control.outputs.glow) {
        esp_ota_mark_app_valid_cancel_rollback();
    }
    vTaskDelete(nullptr);
}

}  // namespace

bool runtime_view(RuntimeView* view) {
    if (!view || !g_view_mutex || xSemaphoreTake(g_view_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    *view = g_view;
    xSemaphoreGive(g_view_mutex);
    return true;
}

bool runtime_issue(sirokko::Action action, uint64_t params_hash, sirokko::Grant* grant) {
    RuntimeView view{};
    if (!grant || !runtime_view(&view) || xSemaphoreTake(g_gate_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
    uint64_t token = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    if (!token) token = 1;
    *grant = g_gate.issue(action, params_hash, view.now_ms, view.control.state_version, token);
    xSemaphoreGive(g_gate_mutex);
    return grant->valid;
}

bool runtime_submit(const sirokko::Grant& grant, sirokko::Action action,
                    uint64_t delay_ms, const sirokko::Settings* settings) {
    RuntimeView view{};
    if (!grant.valid || grant.action != action || !runtime_view(&view)) return false;

    if ((action == sirokko::Action::START || action == sirokko::Action::ARM) &&
        view.control.start_block != sirokko::Block::NONE) return false;
    if (action == sirokko::Action::OTA &&
        (view.control.state != sirokko::State::IDLE || view.control.outputs.fan_request ||
         view.control.outputs.fuel || view.control.outputs.glow)) return false;
    if (action == sirokko::Action::SETTINGS && (!settings || !safe_settings(*settings) ||
        (view.control.state != sirokko::State::IDLE && view.control.state != sirokko::State::RECOVERY_PURGE &&
         view.control.state != sirokko::State::FAULT))) return false;

    RuntimeCommand command{grant, action, delay_ms, settings ? *settings : view.settings};
    return xQueueSend(g_command_queue, &command, 0) == pdTRUE;
}

void runtime_stop() {
    g_stop.store(true);
    if (g_gate_mutex && xSemaphoreTake(g_gate_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        g_gate.invalidate();
        xSemaphoreGive(g_gate_mutex);
    }
}

bool runtime_maintenance_safe() {
    RuntimeView view{};
    return runtime_view(&view) && view.control.state == sirokko::State::MAINTENANCE &&
           !view.control.outputs.fan_request && !view.control.outputs.fuel && !view.control.outputs.glow;
}

void runtime_exit_maintenance() {
    g_exit_maintenance.store(true);
}

extern "C" void app_main() {
    // First executable action: dangerous commands LOW and both MAX31856 CS HIGH.
    if (!output_begin()) {
        ESP_LOGE(kTag, "cannot establish safe GPIO state");
        return;
    }

    esp_err_t nvs_error = nvs_flash_init();
    if (nvs_error == ESP_ERR_NVS_NO_FREE_PAGES || nvs_error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_error);

    char password[21]{};
    bool password_created{};
    if (!load_or_create_password(password, &password_created)) {
        ESP_LOGE(kTag, "cannot load AP credential; network remains disabled");
        return;
    }
    if (password_created) ESP_LOGW(kTag, "FIRST BOOT AP PASSWORD (USB serial only): %s", password);

    g_view_mutex = xSemaphoreCreateMutex();
    g_gate_mutex = xSemaphoreCreateMutex();
    g_sensor_queue = xQueueCreate(1, sizeof(sirokko::Sensors));
    g_command_queue = xQueueCreate(8, sizeof(RuntimeCommand));
    g_settings_queue = xQueueCreate(1, sizeof(sirokko::Settings));
    if (!g_view_mutex || !g_gate_mutex || !g_sensor_queue || !g_command_queue || !g_settings_queue) return;

    // No tested fire profile exists. This immutable production profile deliberately cannot enable fuel.
    const sirokko::ProcessProfile safe_closed_profile{};
    const sirokko::Settings settings = load_settings();
    uint64_t boot_id = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    g_controller = new (std::nothrow) sirokko::Controller(safe_closed_profile, settings, boot_id);
    if (!g_controller) return;

    const bool sensors_ok = g_sensor_suite.begin();
    ESP_LOGI(kTag, "sensor initialization=%s", sensors_ok ? "OK" : "FAULT (start remains blocked)");
    if (xTaskCreatePinnedToCore(sensor_task, "sensors", 6144, nullptr, 8, nullptr, 0) != pdPASS ||
        xTaskCreatePinnedToCore(control_task, "control", 8192, nullptr, 20, nullptr, 1) != pdPASS ||
        xTaskCreate(storage_task, "storage", 3072, nullptr, 2, nullptr) != pdPASS) {
        gpio_set_level(board::kFuel, 0);
        gpio_set_level(board::kGlow, 0);
        return;
    }

    if (!start_access_point(password) || !web_start(password)) {
        ESP_LOGE(kTag, "AP/browser startup failed; control loop remains independent");
    }
    std::memset(password, 0, sizeof(password));
    xTaskCreate(ota_health_task, "ota_health", 3072, nullptr, 3, nullptr);
    ESP_LOGW(kTag, "%s: DRAFT / PROFILE NOT QUALIFIED / NO POWER / NO FLASH / NO FIRE",
             esp_app_get_description()->version);
}
