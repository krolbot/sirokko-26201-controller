#include "output_driver.hpp"

#include <initializer_list>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

namespace {
constexpr gpio_num_t kGlow = GPIO_NUM_14;
constexpr gpio_num_t kFuel = GPIO_NUM_15;
constexpr gpio_num_t kFan = GPIO_NUM_16;
constexpr gpio_num_t kSpeed = GPIO_NUM_17;
constexpr gpio_num_t kWatchdog = GPIO_NUM_18;
constexpr gpio_num_t kFuelPermit = GPIO_NUM_21;
constexpr gpio_num_t kArmReady = GPIO_NUM_41;
constexpr gpio_num_t kSoftwareTrip = GPIO_NUM_42;
}

bool OutputDriver::fail(esp_err_t error) {
    ready_ = false;
    applied_speed_ = 0;
    // Do not short-circuit shutdown: attempt every independent inhibit on every failed pass.
    esp_err_t shutdown = ESP_OK;
    for (auto pin : {kFuel, kFuelPermit, kGlow, kArmReady, kWatchdog, kFan}) {
        shutdown |= gpio_set_level(pin, 0);
    }
    shutdown |= gpio_set_level(kSoftwareTrip, 1);
    if (channel_configured_) shutdown |= ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    // Disconnect GPIO17 from the LEDC matrix even if ledc_stop itself failed.
    shutdown |= gpio_reset_pin(kSpeed);
    shutdown |= gpio_set_level(kSpeed, 0);
    shutdown |= gpio_set_direction(kSpeed, GPIO_MODE_OUTPUT);
    shutdown_applied_ = shutdown == ESP_OK;
    if (!failure_reported_) {
        ESP_LOGE("outputs", "latched failure=%d shutdown=%d; reboot required", error, shutdown);
        failure_reported_ = true;
    }
    return false;
}

bool OutputDriver::initialize(const sirokko::Config& config) {
    if (started_) return fail(ESP_ERR_INVALID_STATE);
    started_ = true;
    gpio_config_t pins{};
    for (auto pin : {kGlow, kFuel, kFan, kSpeed, kWatchdog, kFuelPermit, kArmReady, kSoftwareTrip}) {
        pins.pin_bit_mask |= 1ULL << pin;
    }
    pins.mode = GPIO_MODE_OUTPUT;
    pins.pull_up_en = GPIO_PULLUP_DISABLE;
    pins.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pins.intr_type = GPIO_INTR_DISABLE;
    esp_err_t error = gpio_config(&pins);
    if (error != ESP_OK) return fail(error);
    for (auto pin : {kFuel, kFuelPermit, kGlow, kFan, kSpeed, kWatchdog, kArmReady, kSoftwareTrip}) {
        error = gpio_set_level(pin, 0);
        if (error != ESP_OK) return fail(error);
    }
    if (sirokko::validate_config(config) != sirokko::CONFIG_OK) return fail(ESP_ERR_INVALID_ARG);
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = config.pwm_frequency_hz;
    timer.clk_cfg = LEDC_USE_APB_CLK;
    error = ledc_timer_config(&timer);
    if (error != ESP_OK) return fail(error);
    if (ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0) == 0 ||
        ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0) > sirokko::kPwmMaximumFrequencyHz) {
        return fail(ESP_ERR_INVALID_STATE);
    }
    ledc_channel_config_t channel{};
    channel.gpio_num = kSpeed;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    channel.hpoint = 0;
    // A failing channel_config may already have connected the matrix; fail() also detaches it.
    channel_configured_ = true;
    error = ledc_channel_config(&channel);
    if (error != ESP_OK) return fail(error);
    error = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (error != ESP_OK) return fail(error);
    ready_ = true;
    return true;
}

bool OutputDriver::set_speed(uint16_t permille) {
    if (!sirokko::speed_supported(permille)) return fail(ESP_ERR_INVALID_ARG);
    if (permille == applied_speed_) return true;
    esp_err_t error;
    if (permille == 0 || permille == 1000) {
        // ESP32-S3 / IDF 5.5.2 ledc_stop disables sig_out_en and selects constant idle level.
        error = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, permille == 1000 ? 1 : 0);
    } else {
        const uint32_t duty = (static_cast<uint32_t>(permille) *
                               (1u << sirokko::kPwmResolutionBits) + 500u) / 1000u;
        error = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        // IDF update_duty enables sig_out_en again, only after the new duty was staged.
        if (error == ESP_OK) error = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    if (error != ESP_OK) return fail(error);
    applied_speed_ = permille;
    return true;
}

bool OutputDriver::apply(const sirokko::Outputs& out) {
    if (!ready_) return fail(ESP_ERR_INVALID_STATE);
    esp_err_t error = ESP_OK;
    if (!out.fuel_command || !out.fuel_permit) {
        error |= gpio_set_level(kFuel, 0);
        error |= gpio_set_level(kFuelPermit, 0);
    }
    if (error != ESP_OK) return fail(error);
    if (!set_speed(out.speed_permille)) return false;
    error |= gpio_set_level(kGlow, out.glow_command ? 1 : 0);
    error |= gpio_set_level(kFan, out.fan_request ? 1 : 0);
    error |= gpio_set_level(kArmReady, out.arm_ready ? 1 : 0);
    error |= gpio_set_level(kSoftwareTrip, out.sw_trip ? 1 : 0);
    error |= gpio_set_level(kWatchdog, out.watchdog_heartbeat ? 1 : 0);
    if (error != ESP_OK) return fail(error);
    if (out.fuel_command && out.fuel_permit && out.fan_request && out.arm_ready) {
        error = gpio_set_level(kFuelPermit, 1);
        if (error == ESP_OK) error = gpio_set_level(kFuel, 1);
    }
    return error == ESP_OK ? true : fail(error);
}
