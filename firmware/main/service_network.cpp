#include "runtime.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include "cJSON.h"
#ifdef SIROKKO_NETWORK_HOST_TEST
#include "network_test_api.hpp"
#else
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include "mqtt_client.h"
#include "nvs.h"
#endif

namespace {
constexpr char kBoard[] = "ESP32-S3-DevKitC-1-N8R8-v1.1";
constexpr size_t kMaxPacket = 2048;
struct NetworkConfig {
    uint32_t schema;
    char ssid[33];
    char wifi_password[65];
    char broker[192];
    char username[65];
    char password[129];
    char ca[4096];
    char update_public_key[1024];
};
NetworkConfig g_network{};
esp_mqtt_client_handle_t g_mqtt{};
std::atomic<bool> g_configured{};
std::atomic<bool> g_connected{};
std::atomic<bool> g_wifi_up{};
std::atomic<bool> g_started{};
char g_base[64]{};
char g_command_topic[96]{};
char g_telemetry_topic[96]{};
char g_reply_topic[96]{};
char g_availability_topic[96]{};
char g_nonce[33]{}; // Accessed by the MQTT event loop; copied into queued jobs.
portMUX_TYPE g_nonce_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_last_sequence{};
QueueHandle_t g_jobs{};
struct UpdateJob {
    char version[32];
    char url[256];
    char hash[65];
    unsigned char signature[384]; // RSA-3072, PKCS#1 v1.5 / SHA256 manifest signature.
    uint32_t size;
    uint32_t sequence;
    uint32_t issued_ms;
    uint32_t ttl_ms;
};
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
bool text(const cJSON* root, const char* key, char* dst, size_t capacity) {
    const auto* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !v->valuestring || std::strlen(v->valuestring) >= capacity) return false;
    std::strcpy(dst, v->valuestring);
    return true;
}
bool integer(const cJSON* root, const char* key, uint32_t* out) {
    const auto* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > UINT32_MAX ||
        static_cast<double>(static_cast<uint32_t>(v->valuedouble)) != v->valuedouble) return false;
    *out = static_cast<uint32_t>(v->valuedouble);
    return true;
}
bool config_valid(const NetworkConfig& c) {
    if (c.schema != 1 || !c.ssid[0] || !c.username[0] || !c.password[0] ||
        std::strncmp(c.broker, "mqtts://", 8) != 0 || std::strchr(c.broker, '@') ||
        !std::strstr(c.ca, "-----BEGIN CERTIFICATE-----")) return false;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    const bool ok = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char*>(c.update_public_key),
                     std::strlen(c.update_public_key) + 1) == 0 &&
                    mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) && mbedtls_pk_get_bitlen(&key) == 3072;
    mbedtls_pk_free(&key);
    return ok;
}
void reply(uint32_t seq, const char* code) {
    char payload[96]{};
    std::snprintf(payload, sizeof(payload), "{\"seq\":%lu,\"code\":\"%s\"}", static_cast<unsigned long>(seq), code);
    if (g_mqtt && g_connected.load()) esp_mqtt_client_enqueue(g_mqtt, g_reply_topic, payload, 0, 1, 0, true);
}
int version_number(const char* s) {
    unsigned major{}, minor{}, patch{};
    int used{};
    if (std::sscanf(s, "%u.%u.%u-DRAFT%n", &major, &minor, &patch, &used) != 3 ||
        used <= 0 || s[used] != 0 || major > 99 || minor > 99 || patch > 99) return -1;
    return static_cast<int>(major * 10000 + minor * 100 + patch);
}
bool hex_hash(const char* s) {
    if (std::strlen(s) != 64) return false;
    for (size_t i = 0; i < 64; ++i) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}
bool signed_manifest(const UpdateJob& job) {
    char canonical[512]{};
    const int n = std::snprintf(canonical, sizeof(canonical), "SIROKKO-OTA-1\nesp32s3\n%s\n3\n%s\n%lu\n%s\n%s\n",
                               kBoard, job.version, static_cast<unsigned long>(job.size), job.hash, job.url);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(canonical)) return false;
    unsigned char digest[32]{};
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(canonical), n, digest, 0) != 0) return false;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool ok = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char*>(g_network.update_public_key),
                                        std::strlen(g_network.update_public_key) + 1) == 0;
    if (ok) ok = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest), job.signature, sizeof(job.signature)) == 0;
    mbedtls_pk_free(&key);
    return ok;
}
bool partition_hash(const esp_partition_t* partition, uint32_t size, const char* expected) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    unsigned char buffer[1024]{};
    for (uint32_t offset = 0; ok && offset < size;) {
        const uint32_t n = size - offset < sizeof(buffer) ? size - offset : sizeof(buffer);
        ok = esp_partition_read(partition, offset, buffer, n) == ESP_OK && mbedtls_sha256_update(&ctx, buffer, n) == 0;
        offset += n;
    }
    unsigned char digest[32]{};
    ok = ok && mbedtls_sha256_finish(&ctx, digest) == 0;
    mbedtls_sha256_free(&ctx);
    char hex[65]{};
    for (size_t i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return ok && std::strcmp(hex, expected) == 0;
}
void update(const UpdateJob& job) {
    using sirokko::ServiceResult;
    if (static_cast<uint32_t>(now_ms() - job.issued_ms) > job.ttl_ms) { reply(job.sequence, "EXPIRED"); return; }
    const auto* next = esp_ota_get_next_update_partition(nullptr);
    if (!next || job.size > next->size) { reply(job.sequence, "OVERSIZE"); return; }
    if (!signed_manifest(job)) { reply(job.sequence, "SIGNATURE"); return; }
    const auto result = runtime_acquire_flash(FlashPurpose::OTA);
    if (result != ServiceResult::OK) { reply(job.sequence, sirokko::service_result_name(result)); return; }
    // The control task has applied and checked OFF GPIO commands, dropped ARM and excludes all STARTs.
    esp_http_client_config_t http{};
    http.url = job.url;
    http.cert_pem = g_network.ca;
    http.timeout_ms = 5000;
    http.keep_alive_enable = false;
    http.disable_auto_redirect = true;
    http.skip_cert_common_name_check = false;
    esp_https_ota_config_t ota{};
    ota.http_config = &http;
    esp_https_ota_handle_t handle{};
    esp_err_t error = esp_https_ota_begin(&ota, &handle);
    const char* code = "IO";
    if (error == ESP_OK) {
        esp_app_desc_t desc{};
        if (esp_https_ota_get_status_code(handle) != 200 ||
            esp_https_ota_get_image_size(handle) != static_cast<int>(job.size)) code = "SIZE";
        else if (esp_https_ota_get_img_desc(handle, &desc) != ESP_OK ||
                 std::strcmp(desc.project_name, esp_app_get_description()->project_name) != 0 ||
                 std::strcmp(desc.version, job.version) != 0) code = "TARGET";
        else {
            const int64_t started = esp_timer_get_time();
            do {
                error = esp_https_ota_perform(handle);
                vTaskDelay(pdMS_TO_TICKS(1));
            } while (error == ESP_ERR_HTTPS_OTA_IN_PROGRESS &&
                     esp_timer_get_time() - started < 300'000'000 && g_wifi_up.load());
            if (error == ESP_OK && esp_https_ota_is_complete_data_received(handle) &&
                esp_https_ota_get_image_len_read(handle) == static_cast<int>(job.size)) {
                if (!partition_hash(next, job.size, job.hash)) code = "HASH";
                else {
                    error = esp_https_ota_finish(handle); // IDF validates image/chip then selects next boot slot.
                    handle = nullptr;
                    if (error == ESP_OK) {
                        reply(job.sequence, "REBOOTING");
                        vTaskDelay(pdMS_TO_TICKS(200));
                        esp_restart(); // No lease release; all outputs remain inhibited through reset.
                    }
                }
            }
        }
        if (handle) esp_https_ota_abort(handle);
    }
    reply(job.sequence, code);
    runtime_release_flash();
}
void accept_packet(const esp_mqtt_event_t& e) {
    if (e.retain || e.data_len <= 0 || e.data_len != e.total_data_len || e.current_data_offset != 0 ||
        static_cast<size_t>(e.data_len) >= kMaxPacket || e.topic_len != static_cast<int>(std::strlen(g_command_topic)) ||
        std::memcmp(e.topic, g_command_topic, e.topic_len) != 0) return;
    cJSON* root = cJSON_ParseWithLength(e.data, e.data_len);
    if (!root) return;
    char nonce[33]{}, op[16]{};
    uint32_t seq{}, issued{}, ttl{};
    bool ok = text(root, "nonce", nonce, sizeof(nonce)) && std::strcmp(nonce, g_nonce) == 0 &&
              integer(root, "seq", &seq) && seq > g_last_sequence &&
              integer(root, "uptime_ms", &issued) && integer(root, "ttl_ms", &ttl) && ttl > 0 && ttl <= 30000 &&
              static_cast<uint32_t>(now_ms() - issued) <= ttl && text(root, "op", op, sizeof(op));
    if (!ok) { cJSON_Delete(root); return; }
    g_last_sequence = seq;
    if (std::strcmp(op, "stop") == 0) {
        runtime_remote_stop();
        reply(seq, "STOP_REQUESTED");
    } else if (std::strcmp(op, "read") == 0) reply(seq, "OK");
    else if (std::strcmp(op, "update") == 0) {
        UpdateJob job{};
        char target[16]{}, board[64]{}, signature[520]{};
        uint32_t schema{};
        size_t decoded{};
        ok = integer(root, "schema", &schema) && schema == sirokko::kConfigSchemaVersion &&
             text(root, "target", target, sizeof(target)) && std::strcmp(target, "esp32s3") == 0 &&
             text(root, "board", board, sizeof(board)) && std::strcmp(board, kBoard) == 0 &&
             text(root, "version", job.version, sizeof(job.version)) &&
             version_number(job.version) > version_number(esp_app_get_description()->version) &&
             text(root, "url", job.url, sizeof(job.url)) && std::strncmp(job.url, "https://", 8) == 0 &&
             !std::strchr(job.url, '\n') && !std::strchr(job.url, '\r') && !std::strchr(job.url, '@') &&
             text(root, "sha256", job.hash, sizeof(job.hash)) && hex_hash(job.hash) &&
             integer(root, "size", &job.size) && job.size >= 1024 &&
             text(root, "signature", signature, sizeof(signature)) &&
             mbedtls_base64_decode(job.signature, sizeof(job.signature), &decoded,
                reinterpret_cast<const unsigned char*>(signature), std::strlen(signature)) == 0 && decoded == sizeof(job.signature);
        job.sequence = seq; job.issued_ms = issued; job.ttl_ms = ttl;
        if (!ok) reply(seq, "INVALID");
        else if (xQueueSend(g_jobs, &job, 0) != pdTRUE) reply(seq, "BUSY");
        else reply(seq, "VALIDATING");
    } else reply(seq, "FORBIDDEN"); // No START/RESET/settings/direct-output operation exists.
    cJSON_Delete(root);
}
void mqtt_event(void*, esp_event_base_t, int32_t id, void* data) {
    auto* e = static_cast<esp_mqtt_event_t*>(data);
    if (id == MQTT_EVENT_CONNECTED) {
        unsigned char nonce[16]{};
        esp_fill_random(nonce, sizeof(nonce));
        portENTER_CRITICAL(&g_nonce_lock);
        for (size_t i = 0; i < sizeof(nonce); ++i) std::snprintf(g_nonce + i * 2, 3, "%02x", nonce[i]);
        portEXIT_CRITICAL(&g_nonce_lock);
        g_last_sequence = 0;
        g_connected.store(true);
        esp_mqtt_client_subscribe(g_mqtt, g_command_topic, 1);
        esp_mqtt_client_enqueue(g_mqtt, g_availability_topic, "online", 0, 1, 1, true);
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        g_connected.store(false);
        portENTER_CRITICAL(&g_nonce_lock);
        std::memset(g_nonce, 0, sizeof(g_nonce));
        portEXIT_CRITICAL(&g_nonce_lock);
    } else if (id == MQTT_EVENT_DATA && g_connected.load()) accept_packet(*e);
}
#ifndef SIROKKO_NETWORK_HOST_TEST
void wifi_event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_up.store(false);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) g_wifi_up.store(true);
}
void network_task(void*) {
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_config.start = true;
    const bool time_started = esp_netif_sntp_init(&time_config) == ESP_OK;
    // Network time only gates certificate validity, never local combustion or safety timing.
    while (!g_wifi_up.load() || !time_started || esp_netif_sntp_sync_wait(pdMS_TO_TICKS(1000)) != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_mqtt_client_config_t config{};
    config.broker.address.uri = g_network.broker;
    config.broker.verification.certificate = g_network.ca;
    config.broker.verification.skip_cert_common_name_check = false;
    config.credentials.username = g_network.username;
    config.credentials.authentication.password = g_network.password;
    config.credentials.client_id = g_base;
    config.session.last_will.topic = g_availability_topic;
    config.session.last_will.msg = "offline";
    config.session.last_will.qos = 1;
    config.session.last_will.retain = 1;
    config.session.keepalive = 30;
    config.session.disable_clean_session = false;
    config.network.timeout_ms = 5000;
    config.network.reconnect_timeout_ms = 5000;
    config.task.priority = 2;
    config.task.stack_size = 6144;
    config.buffer.size = kMaxPacket;
    config.outbox.limit = 8192;
    g_mqtt = esp_mqtt_client_init(&config);
    if (!g_mqtt || esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, mqtt_event, nullptr) != ESP_OK ||
        esp_mqtt_client_start(g_mqtt) != ESP_OK) { vTaskDelete(nullptr); return; }
    uint32_t last{};
    while (true) {
        UpdateJob job{};
        if (xQueueReceive(g_jobs, &job, pdMS_TO_TICKS(100)) == pdTRUE) update(job);
        if (g_connected.load() && static_cast<uint32_t>(now_ms() - last) >= 2000) {
            last = now_ms();
            sirokko::UiFrame frame{};
            if (!runtime_snapshot(&frame)) continue;
            char nonce[33]{};
            portENTER_CRITICAL(&g_nonce_lock);
            std::memcpy(nonce, g_nonce, sizeof(nonce));
            portEXIT_CRITICAL(&g_nonce_lock);
            char payload[1024]{};
            const auto& s = frame.status;
            std::snprintf(payload, sizeof(payload),
                "{\"version\":\"%s\",\"board\":\"%s\",\"nonce\":\"%s\",\"uptime_ms\":%lu,\"boot\":%lu,\"session\":%lu,"
                "\"state\":\"%s\",\"faults\":%lu,\"room_raw_mC\":%ld,\"room_mC\":%ld,\"room_q\":%u,\"room_age_ms\":%lu,"
                "\"exh_mC\":%ld,\"exh_q\":%u,\"exh_fault\":%u,\"air_mC\":%ld,\"air_q\":%u,\"air_fault\":%u,"
                "\"fan_command_permille\":%u,\"fuel_command\":%u,\"glow_command\":%u,\"permit_command\":%u,\"safety_ok\":%u,\"storage_ok\":%u}",
                esp_app_get_description()->version, kBoard, nonce, static_cast<unsigned long>(frame.now_ms),
                static_cast<unsigned long>(frame.boot_id), static_cast<unsigned long>(frame.session_id),
                sirokko::state_name(s.state), static_cast<unsigned long>(s.faults), static_cast<long>(frame.raw_room.milli_c),
                static_cast<long>(frame.room.milli_c), static_cast<unsigned>(frame.room.quality),
                static_cast<unsigned long>(frame.now_ms - frame.raw_room.sample_ms), static_cast<long>(frame.exhaust.milli_c),
                static_cast<unsigned>(frame.exhaust.quality), frame.exhaust_fault, static_cast<long>(frame.hot_air.milli_c),
                static_cast<unsigned>(frame.hot_air.quality), frame.hot_air_fault, s.outputs.speed_permille,
                s.outputs.fuel_command, s.outputs.glow_command, s.outputs.fuel_permit, frame.safety_ok, frame.storage_ok);
            esp_mqtt_client_enqueue(g_mqtt, g_telemetry_topic, payload, 0, 0, 0, false);
        }
    }
}
#endif
} // namespace

#ifndef SIROKKO_NETWORK_HOST_TEST
bool service_network_import(const char* encoded) {
    // The caller owns a safe flash lease. Nothing in this API prints payloads or credentials.
    const size_t n = std::strlen(encoded);
    if (n == 0 || n > 16000) return false;
    auto* decoded = static_cast<unsigned char*>(std::calloc(1, 12001));
    if (!decoded) return false;
    size_t size{};
    cJSON* root{};
    bool ok = mbedtls_base64_decode(decoded, 12000, &size,
                reinterpret_cast<const unsigned char*>(encoded), n) == 0;
    if (ok) root = cJSON_ParseWithLength(reinterpret_cast<const char*>(decoded), size);
    NetworkConfig candidate{};
    candidate.schema = 1;
    ok = root && text(root, "ssid", candidate.ssid, sizeof(candidate.ssid)) &&
         text(root, "wifi_password", candidate.wifi_password, sizeof(candidate.wifi_password)) &&
         text(root, "broker", candidate.broker, sizeof(candidate.broker)) &&
         text(root, "username", candidate.username, sizeof(candidate.username)) &&
         text(root, "password", candidate.password, sizeof(candidate.password)) &&
         text(root, "ca", candidate.ca, sizeof(candidate.ca)) &&
         text(root, "update_public_key", candidate.update_public_key, sizeof(candidate.update_public_key)) && config_valid(candidate);
    if (root) cJSON_Delete(root);
    mbedtls_platform_zeroize(decoded, 12001);
    std::free(decoded);
    nvs_handle_t h{};
    if (ok) {
        ok = nvs_open("sirokko_net", NVS_READWRITE, &h) == ESP_OK;
        if (ok) {
            ok = nvs_set_blob(h, "config_v1", &candidate, sizeof(candidate)) == ESP_OK && nvs_commit(h) == ESP_OK;
            nvs_close(h);
        }
    }
    mbedtls_platform_zeroize(&candidate, sizeof(candidate));
    return ok;
}
bool service_network_begin() {
    nvs_handle_t h{};
    if (nvs_open("sirokko_net", NVS_READONLY, &h) != ESP_OK) return true; // Absent service config is an offline local device.
    size_t size = sizeof(g_network);
    const auto e = nvs_get_blob(h, "config_v1", &g_network, &size);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return true;
    // Termination checks precede every strlen/JSON/TLS use of persisted strings.
    if (e != ESP_OK || size != sizeof(g_network) ||
        !std::memchr(g_network.ssid, 0, sizeof(g_network.ssid)) ||
        !std::memchr(g_network.wifi_password, 0, sizeof(g_network.wifi_password)) ||
        !std::memchr(g_network.broker, 0, sizeof(g_network.broker)) ||
        !std::memchr(g_network.username, 0, sizeof(g_network.username)) ||
        !std::memchr(g_network.password, 0, sizeof(g_network.password)) ||
        !std::memchr(g_network.ca, 0, sizeof(g_network.ca)) ||
        !std::memchr(g_network.update_public_key, 0, sizeof(g_network.update_public_key)) || !config_valid(g_network)) return false;
    g_configured.store(true);
    uint8_t mac[6]{};
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK || !esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK || esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) return false;
    std::snprintf(g_base, sizeof(g_base), "sirokko-%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    std::snprintf(g_command_topic, sizeof(g_command_topic), "%s/service/request", g_base);
    std::snprintf(g_telemetry_topic, sizeof(g_telemetry_topic), "%s/telemetry", g_base);
    std::snprintf(g_reply_topic, sizeof(g_reply_topic), "%s/service/reply", g_base);
    std::snprintf(g_availability_topic, sizeof(g_availability_topic), "%s/availability", g_base);
    wifi_config_t wifi{};
    std::memcpy(wifi.sta.ssid, g_network.ssid, std::strlen(g_network.ssid));
    std::memcpy(wifi.sta.password, g_network.wifi_password, std::strlen(g_network.wifi_password));
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi.sta.pmf_cfg.capable = true;
    wifi.sta.pmf_cfg.required = true;
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &wifi) != ESP_OK) return false;
    g_jobs = xQueueCreate(1, sizeof(UpdateJob));
    if (!g_jobs || esp_wifi_start() != ESP_OK) return false;
    g_started.store(true);
    return xTaskCreate(network_task, "service_net", 12288, nullptr, 2, nullptr) == pdPASS;
}
#endif
bool service_network_configured() { return g_configured.load(); }
bool service_network_connected() { return g_connected.load(); }
