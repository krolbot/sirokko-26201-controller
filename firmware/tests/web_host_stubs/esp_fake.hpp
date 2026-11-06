#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "web_protocol.hpp"

using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;

struct esp_app_desc_t {
    char version[32]{};
    char project_name[32]{};
};

enum esp_chip_id_t : uint16_t {
    ESP_CHIP_ID_ESP32S3 = 0x0009,
};

struct __attribute__((packed)) esp_image_header_t {
    uint8_t magic{};
    uint8_t segment_count{};
    uint8_t spi_mode{};
    uint8_t spi_mode_size{};
    uint32_t entry_addr{};
    uint8_t wp_pin{};
    uint8_t spi_pin_drv[3]{};
    esp_chip_id_t chip_id{};
    uint8_t min_chip_rev{};
    uint16_t min_chip_rev_full{};
    uint16_t max_chip_rev_full{};
    uint8_t reserved[4]{};
    uint8_t hash_appended{};
};
inline constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xE9;

struct esp_chip_info_t { int model{}; };
inline constexpr int CHIP_ESP32S3 = 9;

struct esp_partition_t {
    size_t size{};
};
using esp_ota_handle_t = uint32_t;

struct httpd_req_t;
using httpd_uri_handler_t = esp_err_t (*)(httpd_req_t*);
using httpd_handle_t = void*;

struct httpd_uri_t {
    const char* uri{};
    int method{};
    httpd_uri_handler_t handler{};
    void* user_ctx{};
};

struct httpd_config_t {
    size_t stack_size{};
    size_t max_uri_handlers{};
    size_t max_open_sockets{};
    bool lru_purge_enable{};
    int recv_wait_timeout{};
    int send_wait_timeout{};
};

inline constexpr int HTTP_GET = 0;
inline constexpr int HTTP_POST = 1;
inline constexpr int HTTPD_SOCK_ERR_TIMEOUT = -123;
inline constexpr int HTTPD_RESP_USE_STRLEN = -1;
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}

struct httpd_req_t {
    int content_len{};
    std::string body;
    size_t body_offset{};
    size_t maximum_receive{};
    size_t timeout_after_bytes{std::numeric_limits<size_t>::max()};
    uint64_t timeout_advance_us{};
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> response_headers;
    std::string response_status{"200 OK"};
    std::string response_type;
    std::string response_body;
};

namespace fake_esp {
inline uint64_t now_us{};
inline uint32_t random_state{0x12345678U};
inline esp_app_desc_t app_description{};
inline esp_app_desc_t partition_description{};
inline esp_partition_t update_partition{0x700000};
inline const esp_partition_t* boot_partition{};
inline bool ota_active{};
inline int ota_abort_count{};
inline esp_err_t ota_end_result{ESP_OK};
inline void (*ota_end_hook)(){};
inline std::vector<uint8_t> ota_bytes{};
inline bool restart_requested{};
inline int chip_model{CHIP_ESP32S3};
inline std::vector<httpd_uri_t> registered_handlers{};

inline void reset() {
    now_us = 0;
    random_state = 0x12345678U;
    app_description = {};
    partition_description = {};
    update_partition = {0x700000};
    boot_partition = nullptr;
    ota_active = false;
    ota_abort_count = 0;
    ota_end_result = ESP_OK;
    ota_end_hook = nullptr;
    ota_bytes.clear();
    restart_requested = false;
    chip_model = CHIP_ESP32S3;
    registered_handlers.clear();
}
}  // namespace fake_esp

inline int httpd_req_recv(httpd_req_t* request, char* output, size_t capacity) {
    if (!request || !output || capacity == 0) return 0;
    if (request->body_offset >= request->timeout_after_bytes) {
        fake_esp::now_us += request->timeout_advance_us;
        return HTTPD_SOCK_ERR_TIMEOUT;
    }
    if (request->body_offset >= request->body.size()) return 0;
    size_t amount = std::min(capacity, request->body.size() - request->body_offset);
    if (request->maximum_receive != 0) amount = std::min(amount, request->maximum_receive);
    if (request->timeout_after_bytes != std::numeric_limits<size_t>::max()) {
        amount = std::min(amount, request->timeout_after_bytes - request->body_offset);
    }
    if (amount == 0) return HTTPD_SOCK_ERR_TIMEOUT;
    std::memcpy(output, request->body.data() + request->body_offset, amount);
    request->body_offset += amount;
    return static_cast<int>(amount);
}

inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t* request, const char* name, char* output, size_t capacity) {
    if (!request || !name || !output || capacity == 0) return ESP_FAIL;
    const auto found = request->headers.find(name);
    if (found == request->headers.end() || found->second.size() + 1 > capacity) return ESP_FAIL;
    std::memcpy(output, found->second.c_str(), found->second.size() + 1);
    return ESP_OK;
}

inline esp_err_t httpd_resp_set_hdr(httpd_req_t* request, const char* name, const char* value) {
    if (!request || !name || !value) return ESP_FAIL;
    request->response_headers[name] = value;
    return ESP_OK;
}

inline esp_err_t httpd_resp_set_status(httpd_req_t* request, const char* status) {
    if (!request || !status) return ESP_FAIL;
    request->response_status = status;
    return ESP_OK;
}

inline esp_err_t httpd_resp_set_type(httpd_req_t* request, const char* type) {
    if (!request || !type) return ESP_FAIL;
    request->response_type = type;
    return ESP_OK;
}

inline esp_err_t httpd_resp_send(httpd_req_t* request, const char* text, int length) {
    if (!request || !text) return ESP_FAIL;
    request->response_body.assign(text, length == HTTPD_RESP_USE_STRLEN ? std::strlen(text) : static_cast<size_t>(length));
    return ESP_OK;
}

inline esp_err_t httpd_resp_sendstr(httpd_req_t* request, const char* text) {
    return httpd_resp_send(request, text, HTTPD_RESP_USE_STRLEN);
}

inline esp_err_t httpd_start(httpd_handle_t* server, const httpd_config_t*) {
    if (!server) return ESP_FAIL;
    *server = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    return ESP_OK;
}

inline esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t* handler) {
    if (!handler) return ESP_FAIL;
    fake_esp::registered_handlers.push_back(*handler);
    return ESP_OK;
}

inline const esp_app_desc_t* esp_app_get_description() { return &fake_esp::app_description; }
inline void esp_chip_info(esp_chip_info_t* info) { if (info) info->model = fake_esp::chip_model; }
inline uint32_t esp_random() {
    fake_esp::random_state = fake_esp::random_state * 1664525U + 1013904223U;
    return fake_esp::random_state;
}
inline void esp_restart() { fake_esp::restart_requested = true; }
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(fake_esp::now_us); }

inline esp_err_t esp_ota_begin(const esp_partition_t* partition, size_t image_size, esp_ota_handle_t* handle) {
    if (!partition || !handle || image_size == 0 || image_size > partition->size) return ESP_FAIL;
    fake_esp::ota_active = true;
    fake_esp::ota_bytes.clear();
    *handle = 1;
    return ESP_OK;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size) {
    if (!fake_esp::ota_active || handle != 1 || !data) return ESP_FAIL;
    const auto* bytes = static_cast<const uint8_t*>(data);
    fake_esp::ota_bytes.insert(fake_esp::ota_bytes.end(), bytes, bytes + size);
    return ESP_OK;
}

inline esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    if (!fake_esp::ota_active || handle != 1) return ESP_FAIL;
    if (fake_esp::ota_end_result == ESP_OK) fake_esp::ota_active = false;
    if(fake_esp::ota_end_hook) fake_esp::ota_end_hook();
    return fake_esp::ota_end_result;
}

inline esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    if (handle != 1) return ESP_FAIL;
    fake_esp::ota_active = false;
    ++fake_esp::ota_abort_count;
    return ESP_OK;
}

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
    return &fake_esp::update_partition;
}

inline esp_err_t esp_ota_get_partition_description(const esp_partition_t*, esp_app_desc_t* description) {
    if (!description) return ESP_FAIL;
    *description = fake_esp::partition_description;
    return ESP_OK;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
    if (!partition) return ESP_FAIL;
    fake_esp::boot_partition = partition;
    return ESP_OK;
}

inline void vTaskDelay(unsigned ticks) { fake_esp::now_us += static_cast<uint64_t>(ticks) * 1000U; }
#define pdMS_TO_TICKS(ms) (ms)

struct mbedtls_md_info_t {};
struct mbedtls_md_context_t { std::vector<uint8_t> bytes; };
inline constexpr int MBEDTLS_MD_SHA256 = 6;
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int) { static const mbedtls_md_info_t info{}; return &info; }
inline void mbedtls_md_init(mbedtls_md_context_t* context) { if (context) context->bytes.clear(); }
inline int mbedtls_md_setup(mbedtls_md_context_t*, const mbedtls_md_info_t*, int) { return 0; }
inline int mbedtls_md_starts(mbedtls_md_context_t* context) { if (!context) return -1; context->bytes.clear(); return 0; }
inline int mbedtls_md_update(mbedtls_md_context_t* context, const uint8_t* bytes, size_t size) {
    if (!context || (!bytes && size != 0)) return -1;
    context->bytes.insert(context->bytes.end(), bytes, bytes + size);
    return 0;
}
inline int mbedtls_md_finish(mbedtls_md_context_t* context, uint8_t* output) {
    if (!context || !output) return -1;
    const auto digest = sirokko::web_protocol::sha256(context->bytes.data(), context->bytes.size());
    std::memcpy(output, digest.data(), digest.size());
    return 0;
}
inline void mbedtls_md_free(mbedtls_md_context_t* context) { if (context) context->bytes.clear(); }
