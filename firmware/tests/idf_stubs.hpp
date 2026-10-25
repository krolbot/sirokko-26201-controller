#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <functional>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 258, ESP_ERR_INVALID_STATE = 259;
constexpr int ESP_ERR_NVS_NO_FREE_PAGES = 1001, ESP_ERR_NVS_NEW_VERSION_FOUND = 1002, ESP_ERR_NVS_NOT_FOUND = 1003, ESP_ERR_NVS_INVALID_LENGTH = 1004;
#define ESP_LOGE(tag, ...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
enum gpio_num_t { GPIO_NUM_5=5, GPIO_NUM_6=6, GPIO_NUM_7=7, GPIO_NUM_8=8, GPIO_NUM_39=39, GPIO_NUM_40=40, GPIO_NUM_14=14, GPIO_NUM_15=15, GPIO_NUM_16=16, GPIO_NUM_17=17, GPIO_NUM_18=18, GPIO_NUM_21=21, GPIO_NUM_41=41, GPIO_NUM_42=42 };
constexpr int GPIO_MODE_INPUT=0, GPIO_MODE_OUTPUT=1, GPIO_PULLUP_DISABLE=0, GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
struct gpio_config_t { uint64_t pin_bit_mask{}; int mode{}, pull_up_en{}, pull_down_en{}, intr_type{}; };
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_set_level(gpio_num_t, uint32_t);
esp_err_t gpio_reset_pin(gpio_num_t);
esp_err_t gpio_set_direction(gpio_num_t, int);
constexpr int LEDC_LOW_SPEED_MODE=0, LEDC_TIMER_10_BIT=10, LEDC_TIMER_0=0, LEDC_CHANNEL_0=0, LEDC_INTR_DISABLE=0, LEDC_USE_APB_CLK=1;
struct ledc_timer_config_t { int speed_mode{}, duty_resolution{}, timer_num{}; uint32_t freq_hz{}; int clk_cfg{}; };
struct ledc_channel_config_t { int gpio_num{}, speed_mode{}, channel{}, intr_type{}, timer_sel{}; uint32_t duty{}, hpoint{}; };
esp_err_t ledc_timer_config(const ledc_timer_config_t*);
esp_err_t ledc_channel_config(const ledc_channel_config_t*);
uint32_t ledc_get_freq(int, int);
esp_err_t ledc_stop(int, int, uint32_t);
esp_err_t ledc_set_duty(int, int, uint32_t);
esp_err_t ledc_update_duty(int, int);
using nvs_handle_t = uint32_t;
using nvs_open_mode_t = int;
constexpr int NVS_READWRITE=1, NVS_READONLY=0;
esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
esp_err_t nvs_get_u32(nvs_handle_t, const char*, uint32_t*);
esp_err_t nvs_set_u32(nvs_handle_t, const char*, uint32_t);
esp_err_t nvs_get_i32(nvs_handle_t, const char*, int32_t*);
esp_err_t nvs_set_i32(nvs_handle_t, const char*, int32_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char*);
esp_err_t nvs_flash_init();
esp_err_t nvs_flash_erase();

namespace fake_idf {
inline std::map<int, uint32_t> levels;
inline std::map<std::string, std::vector<uint8_t>> nvs;
inline std::vector<std::string> trace;
inline std::string failing_call;
inline std::function<void(const std::string&)> observer;
inline bool persistent_failure{}, connected{}, running{};
inline uint32_t idle{}, staged{}, active{}, frequency{};
void reset();
bool call(const std::string& name);
}
