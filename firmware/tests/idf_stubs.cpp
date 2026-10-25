#include "idf_stubs.hpp"
#include <cstring>

namespace fake_idf {
void reset() { levels.clear(); nvs.clear(); trace.clear(); failing_call.clear(); persistent_failure=false; connected=running=false; idle=staged=active=frequency=0; }
bool call(const std::string& name) {
    if (observer) observer(name);
    trace.push_back(name);
    if (name != failing_call) return true;
    if (!persistent_failure) failing_call.clear();
    return false;
}
}
using namespace fake_idf;
esp_err_t gpio_config(const gpio_config_t*) { return call("gpio_config") ? ESP_OK : ESP_FAIL; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t value) {
    if (!call("gpio_level:"+std::to_string(pin)+":"+std::to_string(value))) return ESP_FAIL;
    levels[pin]=value; return ESP_OK;
}
esp_err_t gpio_reset_pin(gpio_num_t pin) {
    if (!call("gpio_reset")) return ESP_FAIL;
    if (pin == GPIO_NUM_17) connected=false;
    levels[pin]=0; return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t, int) { return call("gpio_direction") ? ESP_OK : ESP_FAIL; }
esp_err_t ledc_timer_config(const ledc_timer_config_t* c) {
    if (!call("timer")) return ESP_FAIL;
    if(c->speed_mode!=LEDC_LOW_SPEED_MODE || c->duty_resolution!=10 || c->clk_cfg!=LEDC_USE_APB_CLK) return ESP_FAIL;
    frequency=c->freq_hz; return ESP_OK;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t* c) {
    connected=true; running=true; active=staged=c->duty; // Also model partial failure after matrix connection.
    return call("channel") ? ESP_OK : ESP_FAIL;
}
uint32_t ledc_get_freq(int, int) { return call("frequency") ? frequency : 0; }
esp_err_t ledc_stop(int, int, uint32_t level) {
    if (!call("stop:"+std::to_string(level))) return ESP_FAIL;
    running=false; idle=level; return ESP_OK;
}
esp_err_t ledc_set_duty(int, int, uint32_t duty) {
    if (!call("duty:"+std::to_string(duty))) return ESP_FAIL;
    staged=duty; return ESP_OK;
}
esp_err_t ledc_update_duty(int, int) {
    if (!call("update")) return ESP_FAIL;
    active=staged; running=true; return ESP_OK;
}
esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t* h) { *h=1; return ESP_OK; }
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_commit(nvs_handle_t) { return call("commit") ? ESP_OK : ESP_FAIL; }
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* out, size_t* size) {
    auto it=nvs.find(key);
    if(it==nvs.end()) return ESP_ERR_NVS_NOT_FOUND;
    if(*size < it->second.size()) { *size=it->second.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    *size=it->second.size(); std::memcpy(out,it->second.data(),*size); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* data, size_t size) {
    const auto* bytes=static_cast<const uint8_t*>(data); nvs[key]={bytes,bytes+size}; return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h,const char* k,uint32_t* v) {
    uint32_t value{}; size_t s=sizeof(value); const auto e=nvs_get_blob(h,k,&value,&s);
    if(e!=ESP_OK) return e;
    if(s!=sizeof(value)) return ESP_ERR_NVS_INVALID_LENGTH;
    *v=value; return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t h,const char* k,uint32_t v) { return nvs_set_blob(h,k,&v,sizeof(v)); }
esp_err_t nvs_get_i32(nvs_handle_t h,const char* k,int32_t* v) { size_t s=sizeof(*v); return nvs_get_blob(h,k,v,&s); }
esp_err_t nvs_set_i32(nvs_handle_t h,const char* k,int32_t v) { return nvs_set_blob(h,k,&v,sizeof(v)); }
esp_err_t nvs_erase_key(nvs_handle_t,const char* key) { return nvs.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_flash_init() { return ESP_OK; }
esp_err_t nvs_flash_erase() { nvs.clear(); return ESP_OK; }
