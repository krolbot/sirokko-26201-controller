#include "runtime_api.hpp"
#include "command_policy.hpp"
#include "wifi_password.hpp"
#include "bounded_receive.hpp"
#include "ota_verify.hpp"
#include "web_protocol.hpp"
#include "web_sessions.hpp"
#include "web_state.hpp"
#include "thermal_history.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"

static_assert(CONFIG_HTTPD_MAX_REQ_HDR_LEN >= 8192,
              "Set CONFIG_HTTPD_MAX_REQ_HDR_LEN=8192 in the active sdkconfig (defaults do not override it)");
static_assert(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY,
              "Enable CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY for the web buffers");
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"

namespace web_protocol = sirokko::web_protocol;
namespace ota_verify = sirokko::ota_verify;

namespace {

constexpr size_t kCommandBodyLimit = 4096;
constexpr uint64_t kCommandBodyReadBudgetMs = 3000;
constexpr uint64_t kOtaBodyReadBudgetMs = 120000;
constexpr uint64_t kMaintenanceWaitMs = 1000;
constexpr char kProjectName[] = "sirokko_26201_controller";
constexpr char kTargetName[] = "esp32s3";

extern const uint8_t ota_public_key_pem_start[] asm("_binary_ota_public_key_pem_start");
extern const uint8_t ota_public_key_pem_end[] asm("_binary_ota_public_key_pem_end");

char g_password[sirokko::wifi::kPasswordCapacity]{};
bool g_wifi_restart_required{};
bool g_next_password_required{};
web_protocol::Sessions g_sessions{};
uint64_t g_last_login_ms{};
unsigned g_failed_logins{};
// Reserve internal RAM for task stacks and DMA by keeping large web scratch buffers in PSRAM.
EXT_RAM_BSS_ATTR std::array<char, kStateJsonCapacity> g_state_json{};

#include "web_ui_data.inc"

uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}
uint64_t receive_clock(void *) {
    return now_ms();
}
int http_receive(void *context, char *buffer, size_t capacity) {
    return httpd_req_recv(static_cast<httpd_req_t *>(context), buffer, capacity);
}

bool constant_equal(const char *left, const char *right, size_t size) {
    if (!left || !right)
        return false;
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index)
        difference |= static_cast<uint8_t>(left[index] ^ right[index]);
    return difference == 0;
}

web_protocol::Session *authorized(httpd_req_t *request) {
    // Static scratch space: all handlers execute on the single HTTP server task.
    static EXT_RAM_BSS_ATTR std::array<char, CONFIG_HTTPD_MAX_REQ_HDR_LEN + 1> cookie{};
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie.data(), cookie.size()) != ESP_OK)
        return nullptr;
    return g_sessions.find(cookie.data(), now_ms());
}
web_protocol::Session *create_session(httpd_req_t *request) {
    char token[33]{};
    for (size_t index = 0; index < 16; ++index)
        std::snprintf(token + index * 2, 3, "%02x", static_cast<unsigned>(esp_random() & 0xFFU));
    auto *session = g_sessions.create(token, now_ms());
    if (!session)
        return nullptr;
    static char cookie[96]{};
    std::snprintf(cookie, sizeof(cookie), "sirokko_session=%s; HttpOnly; SameSite=Strict; Path=/", token);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    return session;
}

void no_store(httpd_req_t *request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' "
                       "'unsafe-inline'; connect-src 'self'; font-src 'self' data:; img-src 'self' data:; "
                       "object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}

esp_err_t send_json(httpd_req_t *request, const char *body, const char *status = nullptr) {
    no_store(request);
    if (status)
        httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, body);
}

esp_err_t reject(httpd_req_t *request, const char *code, const char *status = "400 Bad Request") {
    char body[96]{};
    const int length =
        std::snprintf(body, sizeof(body), "{\"status\":\"REJECTED\",\"code\":\"%s\"}", code ? code : "ERROR");
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body))
        return ESP_FAIL;
    // IDF otherwise drains unread request bytes without an overall deadline.
    // A rejected upload must not hold the server while the client keeps sending.
    const bool close = request->content_len != 0;
    if (close)
        httpd_resp_set_hdr(request, "Connection", "close");
    const esp_err_t result = send_json(request, body, status);
    return close ? ESP_FAIL : result;
}

struct BodySink {
    char *output{};
    size_t capacity{};
    size_t size{};
};

bool consume_body(void *context, const char *buffer, size_t size) {
    auto &sink = *static_cast<BodySink *>(context);
    if (!buffer || sink.size >= sink.capacity || size >= sink.capacity - sink.size)
        return false;
    std::memcpy(sink.output + sink.size, buffer, size);
    sink.size += size;
    return true;
}

bool read_body(httpd_req_t *request, char *output, size_t capacity, size_t *actual_size) {
    if (!actual_size)
        return false;
    *actual_size = 0;
    if (!request || !output || request->content_len <= 0 ||
        static_cast<size_t>(request->content_len) >= capacity)
        return false;
    std::array<char, 256> chunk{};
    BodySink sink{output, capacity, 0};
    const BoundedReceiveResult result = receive_exact_bounded(
        static_cast<size_t>(request->content_len), kCommandBodyReadBudgetMs, HTTPD_SOCK_ERR_TIMEOUT,
        chunk.data(), chunk.size(), request, http_receive, &sink, consume_body, nullptr, receive_clock);
    if (result != BoundedReceiveResult::COMPLETE || sink.size != static_cast<size_t>(request->content_len))
        return false;
    if (std::memchr(output, 0, sink.size))
        return false;
    *actual_size = sink.size;
    output[sink.size] = 0;
    return true;
}

bool parse_hex64(const char *text, uint64_t *value) {
    if (!text || !value || !*text || std::strlen(text) > 16)
        return false;
    uint64_t parsed = 0;
    for (const char *current = text; *current; ++current) {
        const char character = *current;
        int digit = -1;
        if (character >= '0' && character <= '9')
            digit = character - '0';
        else if (character >= 'a' && character <= 'f')
            digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F')
            digit = character - 'A' + 10;
        else
            return false;
        parsed = (parsed << 4U) | static_cast<uint8_t>(digit);
    }
    *value = parsed;
    return true;
}

bool parse_decimal_u64(const char *text, uint64_t *value) {
    if (!text || !value || !*text)
        return false;
    uint64_t parsed = 0;
    const bool leading_zero = text[0] == '0';
    for (size_t index = 0; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9')
            return false;
        if (leading_zero && index != 0)
            return false;
        const uint8_t digit = static_cast<uint8_t>(text[index] - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10U)
            return false;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

bool parse_hex_digest(const char *text, web_protocol::Digest *digest) {
    if (!text || !digest || std::strlen(text) != digest->size() * 2U)
        return false;
    const auto nibble = [](char character) {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        if (character >= 'A' && character <= 'F')
            return character - 'A' + 10;
        return -1;
    };
    for (size_t index = 0; index < digest->size(); ++index) {
        const int high = nibble(text[index * 2]);
        const int low = nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        (*digest)[index] = static_cast<uint8_t>((high << 4U) | low);
    }
    return true;
}

bool read_header(httpd_req_t *request, const char *name, char *output, size_t capacity) {
    return request && name && output && capacity > 1 &&
           httpd_req_get_hdr_value_str(request, name, output, capacity) == ESP_OK;
}

bool semantic_valid(const web_protocol::Command &command) {
    switch (command.action) {
    case sirokko::Action::TIMER:
        return command.delay_ms <= sirokko::kMaximumTimerMs;
    case sirokko::Action::SETTINGS:
        return sirokko::valid_settings(command.settings);
    case sirokko::Action::PROFILE:
        return sirokko::valid_safety_envelope(command.envelope) &&
               command.envelope.exploration_floor_code > 0;
    case sirokko::Action::MODEL_REVISION:
        return command.revision.minimum_speed_code <= command.revision.maximum_speed_code &&
               std::isfinite(command.revision.slew_up_per_s) &&
               std::isfinite(command.revision.slew_down_per_s) && command.revision.slew_up_per_s >= 0.0 &&
               command.revision.slew_down_per_s >= 0.0;
    case sirokko::Action::OTA:
        return command.upload_size >= ota_verify::kManifestSize + ota_verify::kSignatureSize;
    case sirokko::Action::START:
    case sirokko::Action::RESET_FAULT:
    case sirokko::Action::CALIBRATE:
        return true;
    }
    return false;
}

uint64_t runtime_hash(const web_protocol::Command &command) {
    return web_protocol::runtime_parameters_hash(command);
}

bool configuration_is_safe(const web_protocol::Command &command, const RuntimeView &view) {
    if (command.action == sirokko::Action::PROFILE)
        return sirokko::commissioning_configuration_block(command.envelope, view.settings) ==
               sirokko::Block::NONE;
    if (command.action != sirokko::Action::MODEL_REVISION)
        return true;
    return sirokko::valid_model_revision(view.control.learned_model, command.revision);
}

sirokko::Grant to_runtime_grant(const web_protocol::Command &command) {
    sirokko::Grant grant{};
    grant.token = command.grant.token;
    grant.state_version = command.grant.state_version;
    grant.params_hash = runtime_hash(command);
    grant.expires_ms = command.grant.expires_ms;
    grant.action = command.action;
    grant.valid = command.has_grant && grant.token != 0;
    return grant;
}

bool target_is_esp32s3() {
    esp_chip_info_t info{};
    esp_chip_info(&info);
    return info.model == CHIP_ESP32S3;
}

bool app_project_matches(const esp_app_desc_t &description) {
    std::array<char, sizeof(description.project_name)> expected{};
    std::snprintf(expected.data(), expected.size(), "%s", kProjectName);
    return std::memcmp(description.project_name, expected.data(), expected.size()) == 0;
}

struct OtaSink {
    const esp_partition_t *partition{};
    size_t expected_size{};
    web_protocol::Digest expected_upload_digest{};
    std::array<uint8_t, ota_verify::kManifestSize> header{};
    std::array<uint8_t, ota_verify::kSignatureSize> signature{};
    std::array<uint8_t, sizeof(esp_image_header_t)> image_header{};
    ota_verify::Manifest manifest{};
    char verified_version[33]{};
    size_t received{};
    size_t header_size{};
    size_t image_size{};
    size_t signature_size{};
    size_t image_header_size{};
    esp_ota_handle_t handle{};
    bool begun{};
    bool ended{};
    bool hashes_ready{};
    mbedtls_md_context_t package_hash{};
    mbedtls_md_context_t image_hash{};
    ota_verify::Result failure{ota_verify::Result::OK};
};

bool start_hash(mbedtls_md_context_t *context) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!context || !info)
        return false;
    mbedtls_md_init(context);
    return mbedtls_md_setup(context, info, 0) == 0 && mbedtls_md_starts(context) == 0;
}

bool init_ota_sink(OtaSink *sink) {
    if (!sink || !start_hash(&sink->package_hash))
        return false;
    if (!start_hash(&sink->image_hash)) {
        mbedtls_md_free(&sink->package_hash);
        return false;
    }
    sink->hashes_ready = true;
    return true;
}

void cleanup_ota_sink(OtaSink *sink) {
    if (!sink)
        return;
    if (sink->begun && !sink->ended)
        esp_ota_abort(sink->handle);
    sink->begun = false;
    if (sink->hashes_ready) {
        mbedtls_md_free(&sink->package_hash);
        mbedtls_md_free(&sink->image_hash);
        sink->hashes_ready = false;
    }
}

bool begin_manifest_ota(OtaSink *sink) {
    if (!sink || sink->header_size != sink->header.size())
        return false;
    ota_verify::Result result =
        ota_verify::parse_manifest(sink->header.data(), sink->header.size(), &sink->manifest);
    if (result == ota_verify::Result::OK) {
        result = ota_verify::validate_manifest(sink->manifest, sink->expected_size, kProjectName, kTargetName,
                                               sink->partition ? sink->partition->size : 0);
    }
    if (result != ota_verify::Result::OK || !target_is_esp32s3()) {
        sink->failure = result == ota_verify::Result::OK ? ota_verify::Result::TARGET : result;
        return false;
    }
    if (esp_ota_begin(sink->partition, sink->manifest.image_size, &sink->handle) != ESP_OK) {
        sink->failure = ota_verify::Result::IMAGE;
        return false;
    }
    sink->begun = true;
    return true;
}

bool consume_ota(void *context, const char *data, size_t size) {
    auto &sink = *static_cast<OtaSink *>(context);
    if (!data || !sink.hashes_ready || !runtime_maintenance_safe()) {
        sink.failure = ota_verify::Result::INPUT;
        return false;
    }
    if (sink.received > sink.expected_size || size > sink.expected_size - sink.received) {
        sink.failure = ota_verify::Result::SIZE;
        return false;
    }
    if (mbedtls_md_update(&sink.package_hash, reinterpret_cast<const uint8_t *>(data), size) != 0) {
        sink.failure = ota_verify::Result::INPUT;
        return false;
    }
    size_t offset = 0;
    while (offset < size) {
        if (sink.header_size < sink.header.size()) {
            const size_t copied = std::min(sink.header.size() - sink.header_size, size - offset);
            std::memcpy(sink.header.data() + sink.header_size, data + offset, copied);
            sink.header_size += copied;
            offset += copied;
            if (sink.header_size == sink.header.size() && !begin_manifest_ota(&sink))
                return false;
            continue;
        }
        if (sink.image_size < sink.manifest.image_size) {
            const size_t copied =
                std::min(static_cast<size_t>(sink.manifest.image_size) - sink.image_size, size - offset);
            const uint8_t *image = reinterpret_cast<const uint8_t *>(data + offset);
            if (sink.image_header_size < sink.image_header.size()) {
                const size_t header_bytes =
                    std::min(sink.image_header.size() - sink.image_header_size, copied);
                std::memcpy(sink.image_header.data() + sink.image_header_size, image, header_bytes);
                sink.image_header_size += header_bytes;
                if (sink.image_header_size == sink.image_header.size()) {
                    esp_image_header_t image_header{};
                    std::memcpy(&image_header, sink.image_header.data(), sizeof(image_header));
                    if (image_header.magic != ESP_IMAGE_HEADER_MAGIC) {
                        sink.failure = ota_verify::Result::IMAGE;
                        return false;
                    }
                    if (image_header.chip_id != ESP_CHIP_ID_ESP32S3) {
                        sink.failure = ota_verify::Result::TARGET;
                        return false;
                    }
                }
            }
            if (mbedtls_md_update(&sink.image_hash, image, copied) != 0 ||
                esp_ota_write(sink.handle, image, copied) != ESP_OK) {
                sink.failure = ota_verify::Result::IMAGE;
                return false;
            }
            sink.image_size += copied;
            offset += copied;
            continue;
        }
        if (sink.signature_size < sink.signature.size()) {
            const size_t copied = std::min(sink.signature.size() - sink.signature_size, size - offset);
            std::memcpy(sink.signature.data() + sink.signature_size, data + offset, copied);
            sink.signature_size += copied;
            offset += copied;
            continue;
        }
        sink.failure = ota_verify::Result::SIZE;
        return false;
    }
    sink.received += size;
    return true;
}

ota_verify::Result finish_ota_sink(OtaSink *sink) {
    if (!sink || !sink->hashes_ready || sink->received != sink->expected_size ||
        sink->header_size != sink->header.size() || sink->image_size != sink->manifest.image_size ||
        sink->signature_size != sink->signature.size())
        return ota_verify::Result::TRUNCATED;
    web_protocol::Digest package_digest{};
    web_protocol::Digest image_digest{};
    if (mbedtls_md_finish(&sink->package_hash, package_digest.data()) != 0 ||
        mbedtls_md_finish(&sink->image_hash, image_digest.data()) != 0)
        return ota_verify::Result::INPUT;
    if (!web_protocol::digest_equal(package_digest, sink->expected_upload_digest))
        return ota_verify::Result::DIGEST;
    if (!web_protocol::digest_equal(image_digest, sink->manifest.image_sha256))
        return ota_verify::Result::DIGEST;
    const ota_verify::Result signature_result = ota_verify::verify_signature(
        sink->manifest, sink->signature.data(), sink->signature.size(), ota_public_key_pem_start,
        static_cast<size_t>(ota_public_key_pem_end - ota_public_key_pem_start));
    if (signature_result != ota_verify::Result::OK)
        return signature_result;
    if (esp_ota_end(sink->handle) != ESP_OK)
        return ota_verify::Result::IMAGE;
    sink->ended = true;
    esp_app_desc_t description{};
    if (esp_ota_get_partition_description(sink->partition, &description) != ESP_OK ||
        !app_project_matches(description)) {
        return ota_verify::Result::PROJECT;
    }
    std::memcpy(sink->verified_version, description.version, sizeof(description.version));
    return ota_verify::Result::OK;
}

esp_err_t missing_page(httpd_req_t *request, httpd_err_code_t) {
    return reject(request, "NOT_FOUND", "404 Not Found");
}

esp_err_t page_handler(httpd_req_t *request) {
    if (request->content_len != 0)
        return reject(request, "BODY");
    no_store(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    return httpd_resp_send(request, reinterpret_cast<const char *>(kPageGzip), sizeof(kPageGzip));
}

esp_err_t login_handler(httpd_req_t *request) {
    if (request->content_len != 0)
        return reject(request, "BODY");
    const uint64_t current = now_ms();
    if ((g_failed_logins >= 5 && current - g_last_login_ms < 30000) || current - g_last_login_ms < 500) {
        return reject(request, "RATE_LIMIT", "429 Too Many Requests");
    }
    g_last_login_ms = current;
    char supplied[sirokko::wifi::kPasswordCapacity]{};
    const bool header = read_header(request, "X-Sirokko-Password", supplied, sizeof(supplied));
    const bool accepted = (!g_password[0]) || (header && std::strlen(supplied) == std::strlen(g_password) &&
                                               constant_equal(supplied, g_password, std::strlen(g_password)));
    std::memset(supplied, 0, sizeof(supplied));
    if (!accepted) {
        ++g_failed_logins;
        return reject(request, "AUTH", "401 Unauthorized");
    }
    g_failed_logins = 0;
    if (!authorized(request) && !create_session(request))
        return reject(request, "SESSIONS_FULL", "503 Service Unavailable");
    no_store(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, "{\"status\":\"OK\"}");
}

esp_err_t wifi_handler(httpd_req_t *request) {
    if (!authorized(request))
        return reject(request, "AUTH", "401 Unauthorized");
    // A custom header prevents cross-origin HTML forms from changing the password.
    char intent[16]{};
    if (!read_header(request, "X-Sirokko-Config", intent, sizeof(intent)) || std::strcmp(intent, "wifi") != 0)
        return reject(request, "CONFIG_HEADER");
    char password[sirokko::wifi::kPasswordCapacity]{};
    size_t size{};
    if (request->content_len > 0 && !read_body(request, password, sizeof(password), &size)) {
        std::memset(password, 0, sizeof(password));
        return reject(request, "BODY");
    }
    if (!sirokko::wifi::valid_password(password)) {
        std::memset(password, 0, sizeof(password));
        return reject(request, "PASSWORD_LENGTH_OR_CHARACTERS");
    }
    const auto blocked = runtime_save_wifi_password(password);
    if (blocked == sirokko::Block::NONE) {
        g_wifi_restart_required = std::strcmp(password, g_password) != 0;
        g_next_password_required = password[0] != 0;
    }
    std::memset(password, 0, sizeof(password));
    if (blocked == sirokko::Block::STORAGE_UNAVAILABLE)
        return reject(request, "STORAGE", "503 Service Unavailable");
    if (blocked != sirokko::Block::NONE)
        return reject(request, "WIFI_BUSY", "409 Conflict");
    return send_json(request, "{\"status\":\"SAVED\"}");
}

esp_err_t state_handler(httpd_req_t *request) {
    if (request->content_len != 0)
        return reject(request, "BODY");
    if (!authorized(request)) {
        if (g_password[0])
            return reject(request, "AUTH", "401 Unauthorized");
        if (!create_session(request))
            return reject(request, "SESSIONS_FULL", "503 Service Unavailable");
    }
    RuntimeView view{};
    if (!runtime_view(&view))
        return reject(request, "BUSY", "503 Service Unavailable");
    JsonBuilder json(g_state_json.data(), g_state_json.size());
    const auto *description = esp_app_get_description();
    const WebStateInfo info{description ? description->version : "unknown", g_password[0] != 0,
                            g_next_password_required, g_wifi_restart_required};
    if (!build_state_json(view, info, &json))
        return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, json.data());
}

esp_err_t history_handler(httpd_req_t *request) {
    if (request->content_len != 0)
        return reject(request, "BODY");
    if (!authorized(request))
        return reject(request, "AUTH", "401 Unauthorized");
    // HTTP handlers run serially; the control task has a separate ring and only a bounded copy lock.
    static EXT_RAM_BSS_ATTR HistorySnapshot history;
    if (!runtime_history(&history))
        return reject(request, "BUSY", "503 Service Unavailable");
    no_store(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    JsonBuilder header(g_state_json.data(), g_state_json.size());
    header.raw("{\"boot_id\":\"");
    header.hexadecimal(history.boot_id);
    header.raw("\",\"period_ms\":");
    header.unsigned_integer(kHistoryPeriodMs);
    header.raw(",\"capacity\":");
    header.unsigned_integer(kHistoryCapacity);
    header.raw(",\"columns\":[\"at_ms\",\"exhaust\",\"air\",\"room\","
               "\"speed\",\"state\",\"faults\"],\"points\":[");
    if (!header.ok() || httpd_resp_send_chunk(request, header.data(), header.size()) != ESP_OK)
        return ESP_FAIL;
    for (size_t first = 0; first < history.count; first += 32) {
        JsonBuilder chunk(g_state_json.data(), g_state_json.size());
        for (size_t i = first; i < std::min(history.count, first + 32); ++i) {
            if (i)
                chunk.character(',');
            append_history_point(&chunk, history.points[i]);
        }
        if (!chunk.ok() || httpd_resp_send_chunk(request, chunk.data(), chunk.size()) != ESP_OK)
            return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(request, "]}", 2) != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t challenge_handler(httpd_req_t *request) {
    auto *session = authorized(request);
    if (!session)
        return reject(request, "AUTH", "401 Unauthorized");
    std::array<char, kCommandBodyLimit> body{};
    size_t body_size{};
    if (!read_body(request, body.data(), body.size(), &body_size))
        return reject(request, "BODY");
    RuntimeView view{};
    if (!runtime_view(&view))
        return reject(request, "BUSY", "503 Service Unavailable");
    web_protocol::Command command{};
    const web_protocol::ParseError parsed =
        web_protocol::parse_challenge_request(body.data(), body_size, &command);
    std::fill(body.begin(), body.end(), 0);
    if (parsed != web_protocol::ParseError::OK)
        return reject(request, web_protocol::error_name(parsed));
    if (!semantic_valid(command))
        return reject(request, "VALUE");
    const auto blocked = command_policy::block(view, command.action);
    if (blocked != sirokko::Block::NONE)
        return reject(request, sirokko::block_name(blocked), "409 Conflict");
    if (!configuration_is_safe(command, view))
        return reject(request,
                      command.action == sirokko::Action::PROFILE ? "PROFILE_TIMING" : "MODEL_REVISION",
                      "409 Conflict");
    sirokko::Grant issued{};
    const uint64_t parameters = runtime_hash(command);
    if (!runtime_issue(command.action, parameters, &issued))
        return reject(request, "BUSY", "503 Service Unavailable");
    const web_protocol::GrantFields fields{issued.token, issued.state_version, issued.expires_ms};
    session->challenge.bind(command.action, fields, command.payload_digest);
    char response[192]{};
    const int length = std::snprintf(response, sizeof(response),
                                     "{\"status\":\"CHALLENGE\",\"token\":\"%llx\",\"state_version\":\"%"
                                     "llx\",\"expires_ms\":\"%llx\",\"boot_id\":\"%llx\"}",
                                     static_cast<unsigned long long>(issued.token),
                                     static_cast<unsigned long long>(issued.state_version),
                                     static_cast<unsigned long long>(issued.expires_ms),
                                     static_cast<unsigned long long>(view.control.boot_id));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(response))
        return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, response);
}

esp_err_t command_handler(httpd_req_t *request) {
    auto *session = authorized(request);
    if (!session)
        return reject(request, "AUTH", "401 Unauthorized");
    std::array<char, kCommandBodyLimit> body{};
    size_t body_size{};
    if (!read_body(request, body.data(), body.size(), &body_size))
        return reject(request, "BODY");

    web_protocol::Stop stop{};
    const web_protocol::ParseError stop_parsed =
        web_protocol::parse_stop_request(body.data(), body_size, &stop);
    if (stop_parsed == web_protocol::ParseError::OK) {
        bool previous = false;
        const web_protocol::ReplayResult replay =
            session->replays.lookup(stop.command_id, stop.payload_digest, &previous);
        std::fill(body.begin(), body.end(), 0);
        if (replay == web_protocol::ReplayResult::CONFLICT)
            return reject(request, "REPLAY", "409 Conflict");
        if (replay == web_protocol::ReplayResult::MATCH)
            return send_json(request, "{\"status\":\"STOP_REQUESTED\",\"replay\":true}");
        runtime_stop(false);
        session->replays.remember(stop.command_id, stop.payload_digest, true);
        return send_json(request, "{\"status\":\"STOP_REQUESTED\"}");
    }

    RuntimeView view{};
    if (!runtime_view(&view)) {
        std::fill(body.begin(), body.end(), 0);
        return reject(request, "BUSY", "503 Service Unavailable");
    }
    web_protocol::Command command{};
    const web_protocol::ParseError parsed =
        web_protocol::parse_command_request(body.data(), body_size, &command);
    std::fill(body.begin(), body.end(), 0);
    if (parsed != web_protocol::ParseError::OK)
        return reject(request, web_protocol::error_name(parsed));
    // Defense in depth for the parser-level OTA_UPLOAD_ONLY rejection above:
    // only /api/ota owns a signed upload and maintenance lifecycle.
    if (command.action == sirokko::Action::OTA)
        return reject(request, "OTA_UPLOAD_ONLY", "405 Method Not Allowed");
    if (command.action == sirokko::Action::CALIBRATE &&
        view.control.commissioning.block != sirokko::Block::NONE) {
        return reject(request, "COMMISSIONING_BLOCK", "409 Conflict");
    }
    if (!configuration_is_safe(command, view))
        return reject(request,
                      command.action == sirokko::Action::PROFILE ? "PROFILE_TIMING" : "MODEL_REVISION",
                      "409 Conflict");
    bool previous = false;
    const web_protocol::ReplayResult replay =
        session->replays.lookup(command.command_id, command.payload_digest, &previous);
    if (replay == web_protocol::ReplayResult::CONFLICT)
        return reject(request, "REPLAY", "409 Conflict");
    if (replay == web_protocol::ReplayResult::MATCH) {
        return send_json(request, previous ? "{\"status\":\"QUEUED\",\"replay\":true}"
                                           : "{\"status\":\"REJECTED\",\"replay\":true}");
    }
    if (!semantic_valid(command))
        return reject(request, "VALUE");
    const web_protocol::BindingResult binding =
        session->challenge.consume(command, view.now_ms, view.control.state_version);
    if (binding != web_protocol::BindingResult::ACCEPTED)
        return reject(request, "CHALLENGE", "409 Conflict");
    const sirokko::Grant grant = to_runtime_grant(command);
    const bool accepted =
        command.action == sirokko::Action::PROFILE ? runtime_save_profile(grant, command.envelope)
        : command.action == sirokko::Action::MODEL_REVISION
            ? runtime_revise_model(grant, command.revision)
            : runtime_submit(grant, command.action, command.delay_ms,
                             command.action == sirokko::Action::SETTINGS ? &command.settings : nullptr);
    session->replays.remember(command.command_id, command.payload_digest, accepted);
    if (!accepted)
        return reject(request, "NOT_ACCEPTED", "409 Conflict");
    return send_json(request, "{\"status\":\"QUEUED\"}");
}

esp_err_t ota_handler(httpd_req_t *request) {
    auto *session = authorized(request);
    if (!session)
        return reject(request, "AUTH", "401 Unauthorized");
    char token_text[17]{}, version_text[17]{}, expires_text[17]{}, id_text[17]{};
    char upload_size_text[24]{}, upload_digest_text[65]{};
    if (!read_header(request, "X-Sirokko-Token", token_text, sizeof(token_text)) ||
        !read_header(request, "X-Sirokko-State-Version", version_text, sizeof(version_text)) ||
        !read_header(request, "X-Sirokko-Expires", expires_text, sizeof(expires_text)) ||
        !read_header(request, "X-Sirokko-Command-Id", id_text, sizeof(id_text)) ||
        !read_header(request, "X-Sirokko-Upload-Size", upload_size_text, sizeof(upload_size_text)) ||
        !read_header(request, "X-Sirokko-Upload-SHA256", upload_digest_text, sizeof(upload_digest_text)))
        return reject(request, "GRANT");

    web_protocol::Command command{};
    command.action = sirokko::Action::OTA;
    command.has_grant = parse_hex64(token_text, &command.grant.token) &&
                        parse_hex64(version_text, &command.grant.state_version) &&
                        parse_hex64(expires_text, &command.grant.expires_ms);
    command.has_grant = command.has_grant && parse_hex64(id_text, &command.command_id) &&
                        command.command_id != 0 &&
                        parse_decimal_u64(upload_size_text, &command.upload_size) &&
                        parse_hex_digest(upload_digest_text, &command.upload_sha256);
    command.payload_digest = web_protocol::compute_payload_digest(command);
    if (!command.has_grant || request->content_len <= 0 ||
        command.upload_size != static_cast<uint64_t>(request->content_len) || !semantic_valid(command))
        return reject(request, "UPLOAD");

    RuntimeView view{};
    if (!runtime_view(&view))
        return reject(request, "BUSY", "503 Service Unavailable");
    bool previous = false;
    const web_protocol::ReplayResult replay =
        session->replays.lookup(command.command_id, command.payload_digest, &previous);
    if (replay == web_protocol::ReplayResult::CONFLICT)
        return reject(request, "REPLAY", "409 Conflict");
    if (replay == web_protocol::ReplayResult::MATCH)
        return reject(request, "REPLAY", "409 Conflict");
    if (session->challenge.consume(command, view.now_ms, view.control.state_version) !=
        web_protocol::BindingResult::ACCEPTED) {
        return reject(request, "CHALLENGE", "409 Conflict");
    }

    const sirokko::Grant grant = to_runtime_grant(command);
    if (!runtime_submit(grant, sirokko::Action::OTA, 0, nullptr)) {
        session->replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "NOT_SAFE", "409 Conflict");
    }
    const uint64_t waiting_started = now_ms();
    while (!runtime_maintenance_safe() && now_ms() - waiting_started < kMaintenanceWaitMs)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (!runtime_maintenance_safe()) {
        runtime_exit_maintenance();
        session->replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "NOT_SAFE", "409 Conflict");
    }

    const uint32_t maintenance_lease = runtime_maintenance_lease();
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    const size_t minimum_size = ota_verify::kManifestSize + ota_verify::kSignatureSize;
    if (!partition || static_cast<size_t>(request->content_len) < minimum_size ||
        static_cast<size_t>(request->content_len) > partition->size + minimum_size) {
        runtime_exit_maintenance();
        session->replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "SIZE");
    }

    OtaSink sink{};
    sink.partition = partition;
    sink.expected_size = static_cast<size_t>(request->content_len);
    sink.expected_upload_digest = command.upload_sha256;
    if (!init_ota_sink(&sink)) {
        runtime_exit_maintenance();
        session->replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "HASH", "500 Internal Server Error");
    }
    std::array<char, 4096> buffer{};
    const BoundedReceiveResult received = receive_exact_bounded(
        sink.expected_size, kOtaBodyReadBudgetMs, HTTPD_SOCK_ERR_TIMEOUT, buffer.data(), buffer.size(),
        request, http_receive, &sink, consume_ota, nullptr, receive_clock);
    std::fill(buffer.begin(), buffer.end(), 0);
    ota_verify::Result result =
        received == BoundedReceiveResult::COMPLETE
            ? finish_ota_sink(&sink)
            : (sink.failure == ota_verify::Result::OK ? ota_verify::Result::TRUNCATED : sink.failure);
    if (received == BoundedReceiveResult::DEADLINE_EXCEEDED)
        result = ota_verify::Result::TRUNCATED;
    cleanup_ota_sink(&sink);
    if (result != ota_verify::Result::OK) {
        runtime_exit_maintenance();
        session->replays.remember(command.command_id, command.payload_digest, false);
        return reject(request,
                      received == BoundedReceiveResult::DEADLINE_EXCEEDED ? "OTA_TIMEOUT"
                                                                          : ota_verify::result_name(result),
                      "409 Conflict");
    }
    if (!runtime_activate_ota(maintenance_lease, partition)) {
        runtime_exit_maintenance();
        session->replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "MAINTENANCE_LEASE", "409 Conflict");
    }
    session->replays.remember(command.command_id, command.payload_digest, true);
    char response[384]{};
    JsonBuilder json(response, sizeof(response));
    json.raw("{\"status\":\"VERIFIED_ACTIVATION_QUEUED\",\"expected_version\":");
    json.quoted(sink.verified_version);
    json.raw("}");
    if (!json.ok())
        return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, json.data());
}

} // namespace

bool web_start(const char *ap_password) {
    if (!sirokko::wifi::valid_password(ap_password))
        return false;
    std::snprintf(g_password, sizeof(g_password), "%s", ap_password);
    g_next_password_required = g_password[0] != 0;
    const httpd_uri_t handlers[] = {
        {"/", HTTP_GET, page_handler, nullptr},
        {"/api/login", HTTP_POST, login_handler, nullptr},
        {"/api/wifi", HTTP_POST, wifi_handler, nullptr},
        {"/api/state", HTTP_GET, state_handler, nullptr},
        {"/api/history", HTTP_GET, history_handler, nullptr},
        {"/api/challenge", HTTP_POST, challenge_handler, nullptr},
        {"/api/command", HTTP_POST, command_handler, nullptr},
        {"/api/ota", HTTP_POST, ota_handler, nullptr},
    };
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.max_uri_handlers = sizeof(handlers) / sizeof(handlers[0]);
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 5;
    httpd_handle_t server{};
    const esp_err_t started = httpd_start(&server, &config);
    if (started != ESP_OK) {
        constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        ESP_LOGE("web", "HTTP server start failed: %s; internal heap free=%u, largest=%u",
                 esp_err_to_name(started), static_cast<unsigned>(heap_caps_get_free_size(caps)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
        return false;
    }
    ESP_LOGI("web", "HTTP request header limit: %d bytes", CONFIG_HTTPD_MAX_REQ_HDR_LEN);
    esp_err_t result = httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, missing_page);
    for (const httpd_uri_t &handler : handlers) {
        if (result != ESP_OK)
            break;
        result = httpd_register_uri_handler(server, &handler);
    }
    if (result != ESP_OK) {
        ESP_LOGE("web", "HTTP handler registration failed: %s", esp_err_to_name(result));
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_stop(server));
        return false;
    }
    return true;
}
