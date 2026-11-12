#include "runtime_api.hpp"
#include "wifi_password.hpp"
#include "bounded_receive.hpp"
#include "ota_verify.hpp"
#include "web_protocol.hpp"

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
#include "esp_chip_info.h"
#include "esp_http_server.h"
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
constexpr size_t kStateJsonCapacity = 8192;
constexpr uint64_t kCommandBodyReadBudgetMs = 3000;
constexpr uint64_t kOtaBodyReadBudgetMs = 120000;
constexpr uint64_t kMaintenanceWaitMs = 1000;
constexpr uint64_t kSessionLifetimeMs = 15ULL * 60ULL * 1000ULL;
constexpr char kProjectName[] = "sirokko_26201_controller";
constexpr char kTargetName[] = "esp32s3";

extern const uint8_t ota_public_key_pem_start[] asm("_binary_ota_public_key_pem_start");
extern const uint8_t ota_public_key_pem_end[] asm("_binary_ota_public_key_pem_end");

char g_password[sirokko::wifi::kPasswordCapacity]{};
bool g_wifi_restart_required{};
bool g_next_password_required{};
char g_session[33]{};
uint64_t g_session_expires_ms{};
uint64_t g_last_login_ms{};
unsigned g_failed_logins{};
web_protocol::ChallengeBinding g_challenge{};
web_protocol::ReplayCache g_replays{};
std::array<char, kStateJsonCapacity> g_state_json{};

#include "web_ui_data.inc"

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }
uint64_t receive_clock(void*) { return now_ms(); }
int http_receive(void* context, char* buffer, size_t capacity) {
    return httpd_req_recv(static_cast<httpd_req_t*>(context), buffer, capacity);
}

bool constant_equal(const char* left, const char* right, size_t size) {
    if (!left || !right) return false;
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) difference |= static_cast<uint8_t>(left[index] ^ right[index]);
    return difference == 0;
}

class JsonBuilder {
public:
    JsonBuilder(char* output, size_t capacity) : output_(output), capacity_(capacity) {
        if (!output_ || capacity_ == 0) ok_ = false;
        else output_[0] = 0;
    }

    bool ok() const { return ok_; }
    const char* data() const { return output_; }
    size_t size() const { return size_; }

    void raw(const char* text) {
        if (!text) { ok_ = false; return; }
        while (*text) append(*text++);
    }

    void character(char value) { append(value); }

    void quoted(const char* text) {
        character('"');
        if (!text) text = "";
        for (; *text; ++text) {
            const unsigned char value = static_cast<unsigned char>(*text);
            if (value == '"' || value == '\\') { character('\\'); character(static_cast<char>(value)); }
            else if (value == '\n') raw("\\n");
            else if (value == '\r') raw("\\r");
            else if (value == '\t') raw("\\t");
            else if (value < 0x20U) raw("?");
            else character(static_cast<char>(value));
        }
        character('"');
    }

    void boolean(bool value) { raw(value ? "true" : "false"); }
    void integer(int32_t value) { formatted("%ld", static_cast<long>(value)); }
    void unsigned_integer(uint64_t value) { formatted("%llu", static_cast<unsigned long long>(value)); }
    void hexadecimal(uint64_t value) { formatted("%llx", static_cast<unsigned long long>(value)); }
    void real(double value) { formatted("%.8g", value); }

private:
    void append(char value) {
        if (!ok_ || size_ + 1 >= capacity_) { ok_ = false; return; }
        output_[size_++] = value;
        output_[size_] = 0;
    }

    void formatted(const char* format, ...) {
        if (!ok_ || size_ >= capacity_) { ok_ = false; return; }
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(output_ + size_, capacity_ - size_, format, arguments);
        va_end(arguments);
        if (written < 0 || static_cast<size_t>(written) >= capacity_ - size_) { ok_ = false; return; }
        size_ += static_cast<size_t>(written);
    }

    char* output_{};
    size_t capacity_{};
    size_t size_{};
    bool ok_{true};
};

const char* russian_state(sirokko::State state) {
    switch (state) {
        case sirokko::State::RECOVERY_PURGE: return "Восстановительная продувка";
        case sirokko::State::IDLE: return "Ожидание";
        case sirokko::State::PREPURGE: return "Предпродувка";
        case sirokko::State::PREHEAT: return "Накал";
        case sirokko::State::IGNITION: return "Розжиг";
        case sirokko::State::VERIFY: return "Проверка запуска";
        case sirokko::State::RUN: return "Работа";
        case sirokko::State::STOP_PURGE: return "Продувка после останова";
        case sirokko::State::FAULT_PURGE: return "Аварийная продувка";
        case sirokko::State::FAULT: return "Авария";
        case sirokko::State::MAINTENANCE: return "Обслуживание";
    }
    return "Неизвестное состояние";
}

const char* russian_block(sirokko::Block block) {
    switch (block) {
        case sirokko::Block::NONE: return "Нет";
        case sirokko::Block::ENVELOPE_MISSING: return "Не задана защитная оболочка";
        case sirokko::Block::ENVELOPE_INVALID: return "Защитная оболочка недействительна";
        case sirokko::Block::ENVELOPE_HARDWARE_MISMATCH: return "Защитная оболочка не соответствует оборудованию";
        case sirokko::Block::RECOVERY_PURGE: return "Требуется восстановительная продувка";
        case sirokko::Block::SENSOR_INVALID: return "Датчики недостоверны";
        case sirokko::Block::SENSOR_STALE: return "Данные датчиков устарели";
        case sirokko::Block::SENSOR_SKEW: return "Метки времени датчиков не согласованы";
        case sirokko::Block::INITIAL_THERMAL_STATE: return "Начальное тепловое состояние вне допуска";
        case sirokko::Block::FAULT_LATCHED: return "Авария зафиксирована";
        case sirokko::Block::BUSY: return "Автомат занят";
        case sirokko::Block::MAINTENANCE: return "Обслуживание";
        case sirokko::Block::STORAGE_UNCONFIRMED: return "Хранилище модели не подтверждено";
        case sirokko::Block::MODEL_UNAVAILABLE: return "Сохранённая модель недоступна";
        case sirokko::Block::STALE_COMMAND: return "Команда устарела";
    }
    return "Неизвестный запрет";
}

const char* russian_event(const char* kind) {
    if (!kind) return "Неизвестное событие";
    if (std::strcmp(kind, "START") == 0) return "Запуск принят автоматом";
    if (std::strcmp(kind, "STOP") == 0) return "Останов принят автоматом";
    if (std::strcmp(kind, "FAULT") == 0) return "Авария";
    if (std::strcmp(kind, "TIMER_EXPIRED") == 0) return "Истёк таймер останова";
    if (std::strcmp(kind, "TIMER_ARMED") == 0) return "Таймер останова установлен";
    if (std::strcmp(kind, "TIMER_CANCELLED") == 0) return "Таймер останова отменён";
    if (std::strstr(kind, "KEY0") || std::strstr(kind, "OPERATOR"))
        return "Оператор сообщил отсутствие/неустойчивость горения (KEY0)";
    return kind;
}

bool authorized(httpd_req_t* request) {
    if (!g_password[0]) return true;
    char cookie[160]{};
    if (!g_session[0] || now_ms() >= g_session_expires_ms ||
        httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        if (g_session[0] && now_ms() >= g_session_expires_ms) {
            g_session[0] = 0;
            g_session_expires_ms = 0;
            g_challenge.clear();
        }
        return false;
    }
    const char* value = std::strstr(cookie, "sirokko_session=");
    if (!value) return false;
    value += std::strlen("sirokko_session=");
    return std::strlen(value) >= 32 && constant_equal(value, g_session, 32) && (value[32] == 0 || value[32] == ';');
}

void no_store(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; font-src 'self' data:; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}

esp_err_t send_json(httpd_req_t* request, const char* body, const char* status = nullptr) {
    no_store(request);
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, body);
}

esp_err_t reject(httpd_req_t* request, const char* code, const char* status = "400 Bad Request") {
    char body[96]{};
    const int length = std::snprintf(body, sizeof(body), "{\"status\":\"REJECTED\",\"code\":\"%s\"}", code ? code : "ERROR");
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body)) return ESP_FAIL;
    return send_json(request, body, status);
}

struct BodySink {
    char* output{};
    size_t capacity{};
    size_t size{};
};

bool consume_body(void* context, const char* buffer, size_t size) {
    auto& sink = *static_cast<BodySink*>(context);
    if (!buffer || sink.size >= sink.capacity || size >= sink.capacity - sink.size) return false;
    std::memcpy(sink.output + sink.size, buffer, size);
    sink.size += size;
    return true;
}

bool read_body(httpd_req_t* request, char* output, size_t capacity, size_t* actual_size) {
    if(!actual_size) return false;
    *actual_size=0;
    if (!request || !output || request->content_len <= 0 || static_cast<size_t>(request->content_len) >= capacity) return false;
    std::array<char, 256> chunk{};
    BodySink sink{output, capacity, 0};
    const BoundedReceiveResult result = receive_exact_bounded(
        static_cast<size_t>(request->content_len), kCommandBodyReadBudgetMs, HTTPD_SOCK_ERR_TIMEOUT,
        chunk.data(), chunk.size(), request, http_receive, &sink, consume_body, nullptr, receive_clock);
    if (result != BoundedReceiveResult::COMPLETE || sink.size != static_cast<size_t>(request->content_len)) return false;
    if(std::memchr(output,0,sink.size)) return false;
    *actual_size=sink.size;
    output[sink.size] = 0;
    return true;
}

bool parse_hex64(const char* text, uint64_t* value) {
    if (!text || !value || !*text || std::strlen(text) > 16) return false;
    uint64_t parsed = 0;
    for (const char* current = text; *current; ++current) {
        const char character = *current;
        int digit = -1;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        else return false;
        parsed = (parsed << 4U) | static_cast<uint8_t>(digit);
    }
    *value = parsed;
    return true;
}

bool parse_decimal_u64(const char* text, uint64_t* value) {
    if (!text || !value || !*text) return false;
    uint64_t parsed = 0;
    const bool leading_zero = text[0] == '0';
    for (size_t index = 0; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        if (leading_zero && index != 0) return false;
        const uint8_t digit = static_cast<uint8_t>(text[index] - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

bool parse_hex_digest(const char* text, web_protocol::Digest* digest) {
    if (!text || !digest || std::strlen(text) != digest->size() * 2U) return false;
    const auto nibble = [](char character) {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    for (size_t index = 0; index < digest->size(); ++index) {
        const int high = nibble(text[index * 2]);
        const int low = nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        (*digest)[index] = static_cast<uint8_t>((high << 4U) | low);
    }
    return true;
}

bool read_header(httpd_req_t* request, const char* name, char* output, size_t capacity) {
    return request && name && output && capacity > 1 &&
           httpd_req_get_hdr_value_str(request, name, output, capacity) == ESP_OK;
}

bool semantic_valid(const web_protocol::Command& command) {
    switch (command.action) {
        case sirokko::Action::ARM:
        case sirokko::Action::CANCEL_ARM:
            return false; // Public product contract: one shutdown timer, no delayed start.
        case sirokko::Action::TIMER:
            return command.delay_ms <= sirokko::kMaximumTimerMs;
        case sirokko::Action::SETTINGS:
            return sirokko::valid_settings(command.settings);
        case sirokko::Action::MODEL_REVISION:
            return command.revision.minimum_speed_code <= command.revision.maximum_speed_code &&
                   std::isfinite(command.revision.slew_up_per_s) && std::isfinite(command.revision.slew_down_per_s) &&
                   command.revision.slew_up_per_s >= 0.0 && command.revision.slew_down_per_s >= 0.0;
        case sirokko::Action::OTA:
            return command.upload_size >= ota_verify::kManifestSize + ota_verify::kSignatureSize;
        case sirokko::Action::START:
        case sirokko::Action::RESET_FAULT:
        case sirokko::Action::CALIBRATE:
            return true;
    }
    return false;
}

uint64_t runtime_hash(const web_protocol::Command& command) {
    return web_protocol::runtime_parameters_hash(command);
}

bool revision_is_safe(const web_protocol::Command& command, const RuntimeView& view) {
    if (command.action != sirokko::Action::MODEL_REVISION) return true;
    return sirokko::valid_model_revision(view.control.learned_model, command.revision);
}

sirokko::Grant to_runtime_grant(const web_protocol::Command& command) {
    sirokko::Grant grant{};
    grant.token = command.grant.token;
    grant.state_version = command.grant.state_version;
    grant.params_hash = runtime_hash(command);
    grant.expires_ms = command.grant.expires_ms;
    grant.action = command.action;
    grant.valid = command.has_grant && grant.token != 0;
    return grant;
}

void append_sensor(JsonBuilder* json, const sirokko::SensorSample& sample, uint64_t now, uint64_t maximum_age) {
    json->raw("{\"milli_c\":"); json->integer(sample.milli_c);
    json->raw(",\"sample_ms\":"); json->unsigned_integer(sample.sample_ms);
    json->raw(",\"status\":"); json->unsigned_integer(static_cast<uint32_t>(sample.status));
    json->raw(",\"valid\":"); json->boolean(sample.valid);
    json->raw(",\"fresh\":"); json->boolean(sample.valid && sample.status==0 && now>=sample.sample_ms && now-sample.sample_ms<=maximum_age);
    json->character('}');
}

struct CommissioningText { const char* code; const char* ru; };
CommissioningText commissioning_phase_text(sirokko::CommissioningPhase value) {
    switch (value) {
        case sirokko::CommissioningPhase::IDLE: return {"IDLE", "Не запущена"};
        case sirokko::CommissioningPhase::STARTING: return {"STARTING", "Подготовка запуска"};
        case sirokko::CommissioningPhase::WAIT_REFERENCE_COMMAND: return {"WAIT_REFERENCE_COMMAND", "Выход на опорную команду"};
        case sirokko::CommissioningPhase::SETTLE_REFERENCE: return {"SETTLE_REFERENCE", "Установление опорного режима"};
        case sirokko::CommissioningPhase::WAIT_DESCENT_COMMAND: return {"WAIT_DESCENT_COMMAND", "Снижение команды"};
        case sirokko::CommissioningPhase::SETTLE_POINT: return {"SETTLE_POINT", "Установление рабочей точки"};
        case sirokko::CommissioningPhase::WAIT_RETURN_COMMAND: return {"WAIT_RETURN_COMMAND", "Возврат к опорной команде"};
        case sirokko::CommissioningPhase::SETTLE_RETURN_REFERENCE: return {"SETTLE_RETURN_REFERENCE", "Проверка опорного режима"};
        case sirokko::CommissioningPhase::WAIT_REPEAT_COMMAND: return {"WAIT_REPEAT_COMMAND", "Повтор рабочей точки"};
        case sirokko::CommissioningPhase::SETTLE_REPEAT_POINT: return {"SETTLE_REPEAT_POINT", "Проверка повторяемости точки"};
        case sirokko::CommissioningPhase::WAIT_POST_REPEAT_REFERENCE_COMMAND: return {"WAIT_POST_REPEAT_REFERENCE_COMMAND", "Повторный возврат к опоре"};
        case sirokko::CommissioningPhase::SETTLE_POST_REPEAT_REFERENCE: return {"SETTLE_POST_REPEAT_REFERENCE", "Проверка опоры после повтора"};
        case sirokko::CommissioningPhase::COMPLETE_PENDING_STOP: return {"COMPLETE_PENDING_STOP", "Измерения завершены, останов и охлаждение"};
        case sirokko::CommissioningPhase::FAILED: return {"FAILED", "Не выполнена"};
        case sirokko::CommissioningPhase::CANCELLED: return {"CANCELLED", "Отменена"};
        case sirokko::CommissioningPhase::PERSIST_STAGING: return {"PERSIST_STAGING", "Подготовка сохранения"};
        case sirokko::CommissioningPhase::PERSIST_COMMITTING: return {"PERSIST_COMMITTING", "Запись и проверка модели"};
        case sirokko::CommissioningPhase::SAVED: return {"SAVED", "Модель сохранена"};
        case sirokko::CommissioningPhase::PERSIST_FAILED: return {"PERSIST_FAILED", "Ошибка сохранения"};
    }
    return {"UNKNOWN", "Неизвестное состояние"};
}
CommissioningText commissioning_reason_text(sirokko::CommissioningReason value) {
    switch (value) {
        case sirokko::CommissioningReason::NONE: return {"NONE", "Нет"};
        case sirokko::CommissioningReason::COMPLETE: return {"COMPLETE", "Измерения завершены"};
        case sirokko::CommissioningReason::INVALID_CONFIG: return {"INVALID_CONFIG", "Недопустимые исходные ограничения"};
        case sirokko::CommissioningReason::ALREADY_ACTIVE: return {"ALREADY_ACTIVE", "Калибровка уже выполняется"};
        case sirokko::CommissioningReason::BACKDATED_TIME: return {"BACKDATED_TIME", "Нарушен порядок времени"};
        case sirokko::CommissioningReason::OVERALL_TIMEOUT: return {"OVERALL_TIMEOUT", "Истекло время калибровки"};
        case sirokko::CommissioningReason::RUN_CONTEXT_LOST: return {"RUN_CONTEXT_LOST", "Потерян рабочий режим"};
        case sirokko::CommissioningReason::COMMAND_OUT_OF_RANGE: return {"COMMAND_OUT_OF_RANGE", "Команда вне разрешённого диапазона"};
        case sirokko::CommissioningReason::COMMAND_SLEW_VIOLATION: return {"COMMAND_SLEW_VIOLATION", "Превышена скорость изменения команды"};
        case sirokko::CommissioningReason::COMMAND_NOT_REACHED: return {"COMMAND_NOT_REACHED", "Заданная команда не достигнута"};
        case sirokko::CommissioningReason::SENSOR_INVALID: return {"SENSOR_INVALID", "Недостоверные показания датчика"};
        case sirokko::CommissioningReason::STALE_SAMPLE: return {"STALE_SAMPLE", "Устаревшие показания"};
        case sirokko::CommissioningReason::SAMPLE_TIMESTAMP_REGRESSION: return {"SAMPLE_TIMESTAMP_REGRESSION", "Нарушен порядок отсчётов"};
        case sirokko::CommissioningReason::SAMPLE_SKEW: return {"SAMPLE_SKEW", "Рассогласованы моменты измерения"};
        case sirokko::CommissioningReason::REPEATED_SAMPLE: return {"REPEATED_SAMPLE", "Повторный отсчёт"};
        case sirokko::CommissioningReason::SAMPLE_GAP: return {"SAMPLE_GAP", "Пропуск отсчётов"};
        case sirokko::CommissioningReason::REFERENCE_TIMEOUT: return {"REFERENCE_TIMEOUT", "Опорный режим не установился"};
        case sirokko::CommissioningReason::POINT_TIMEOUT: return {"POINT_TIMEOUT", "Рабочая точка не установилась"};
        case sirokko::CommissioningReason::REFERENCE_NOT_REPRODUCIBLE: return {"REFERENCE_NOT_REPRODUCIBLE", "Опорный режим не повторяется"};
        case sirokko::CommissioningReason::NO_COMMAND_RESPONSE: return {"NO_COMMAND_RESPONSE", "Нет подтверждённого теплового отклика"};
        case sirokko::CommissioningReason::POINT_NOT_REPRODUCIBLE: return {"POINT_NOT_REPRODUCIBLE", "Рабочая точка не повторяется"};
        case sirokko::CommissioningReason::DYNAMIC_NOT_REPRODUCIBLE: return {"DYNAMIC_NOT_REPRODUCIBLE", "Динамика не повторяется"};
        case sirokko::CommissioningReason::INSUFFICIENT_MARGIN: return {"INSUFFICIENT_MARGIN", "Недостаточный запас"};
        case sirokko::CommissioningReason::OPERATOR_NEGATIVE: return {"OPERATOR_NEGATIVE", "Оператор отклонил результат"};
        case sirokko::CommissioningReason::EXTERNAL_CANCEL: return {"EXTERNAL_CANCEL", "Отменена внешней командой"};
        case sirokko::CommissioningReason::STORAGE_UNCONFIRMED: return {"STORAGE_UNCONFIRMED", "Сохранение не подтверждено"};
    }
    return {"UNKNOWN", "Неизвестное состояние"};
}
void append_model(JsonBuilder* json, const sirokko::LearnedOperatingModel& model) {
    json->raw("{\"valid\":"); json->boolean(model.valid);
    json->raw(",\"minimum_speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(model.minimum_speed_code));
    json->raw(",\"maximum_speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(model.maximum_speed_code));
    json->raw(",\"slew_up_per_s\":"); json->real(model.slew_up_per_s);
    json->raw(",\"slew_down_per_s\":"); json->real(model.slew_down_per_s);
    json->raw(",\"fingerprint\":\""); json->hexadecimal(model.fingerprint); json->raw("\"");
    json->raw(",\"provenance\":{\"safety_envelope_fingerprint\":\""); json->hexadecimal(model.provenance.safety_envelope_fingerprint);
    json->raw("\",\"commissioning_started_ms\":"); json->unsigned_integer(model.provenance.commissioning_started_ms);
    json->raw(",\"commissioning_completed_ms\":"); json->unsigned_integer(model.provenance.commissioning_completed_ms);
    json->raw(",\"revision\":"); json->unsigned_integer(model.provenance.revision); json->raw("}");
    json->raw(",\"measurement\":{\"elapsed_ms\":"); json->unsigned_integer(model.measurements.elapsed_ms);
    json->raw(",\"tested_point_count\":"); json->unsigned_integer(model.measurements.tested_point_count);
    json->raw(",\"observed_slew_up_codes_per_s\":"); json->real(model.measurements.observed_slew_up_codes_per_s);
    json->raw(",\"observed_slew_down_codes_per_s\":"); json->real(model.measurements.observed_slew_down_codes_per_s); json->raw("}}");
}

bool build_state_json(const RuntimeView& view, JsonBuilder* json) {
    const sirokko::Snapshot& control = view.control;
    const esp_app_desc_t* description = esp_app_get_description();
    json->raw("{\"version\":"); json->quoted(description ? description->version : "unknown");
    json->raw(",\"uptime_ms\":"); json->unsigned_integer(view.now_ms);
    json->raw(",\"state_version\":\""); json->hexadecimal(control.state_version); json->raw("\"");
    json->raw(",\"boot_id\":\""); json->hexadecimal(control.boot_id); json->raw("\"");
    json->raw(",\"run_id\":\""); json->hexadecimal(control.run_id); json->raw("\"");
    json->raw(",\"state\":{\"code\":"); json->quoted(sirokko::state_name(control.state));
    json->raw(",\"ru\":"); json->quoted(russian_state(control.state)); json->raw("}");
    json->raw(",\"start_block\":{\"code\":"); json->quoted(sirokko::block_name(control.start_block));
    json->raw(",\"ru\":"); json->quoted(russian_block(control.start_block)); json->raw("}");
    json->raw(",\"faults\":"); json->unsigned_integer(control.faults);
    json->raw(",\"storage_ok\":"); json->boolean(view.storage_ok);
    json->raw(",\"sensors\":{\"exhaust\":"); append_sensor(json, view.sensors.exhaust, view.now_ms, sirokko::kSensorStaleMs);
    json->raw(",\"air\":"); append_sensor(json, view.sensors.air, view.now_ms, sirokko::kSensorStaleMs);
    json->raw(",\"room\":"); append_sensor(json, view.sensors.room, view.now_ms, sirokko::kRoomSensorStaleMs); json->raw("}");
    json->raw(",\"outputs\":{\"fan_request\":"); json->boolean(control.outputs.fan_request);
    json->raw(",\"speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(control.outputs.speed_code));
    json->raw(",\"glow\":"); json->boolean(control.outputs.glow);
    json->raw(",\"fuel\":"); json->boolean(control.outputs.fuel);
    json->raw(",\"command_percent\":"); json->real(control.command_percent); json->raw("}");
    json->raw(",\"wifi\":{\"password_required\":"); json->boolean(g_password[0] != 0);
    json->raw(",\"next_boot_password_required\":"); json->boolean(g_next_password_required);
    json->raw(",\"restart_required\":"); json->boolean(g_wifi_restart_required); json->raw("}");
    json->raw(",\"settings\":{\"room_setpoint_milli_c\":"); json->integer(view.settings.room_setpoint_milli_c);
    json->raw(",\"duration_ms\":"); json->unsigned_integer(view.settings.duration_ms);
    json->raw(",\"mode\":"); json->quoted(view.settings.mode == sirokko::Mode::TEMPERATURE ? "TEMPERATURE" : "MANUAL");
    json->raw(",\"manual_percent\":"); json->unsigned_integer(static_cast<uint32_t>(view.settings.manual_percent));
    json->raw(",\"slew_up_per_s\":"); json->real(view.settings.slew_up_per_s);
    json->raw(",\"slew_down_per_s\":"); json->real(view.settings.slew_down_per_s); json->raw("}");
    json->raw(",\"timer\":{\"active\":"); json->boolean(control.duration_active);
    json->raw(",\"remaining_ms\":"); json->unsigned_integer(control.duration_remaining_ms);
    const uint64_t deadline=control.duration_active && control.duration_remaining_ms<=std::numeric_limits<uint64_t>::max()-view.now_ms
        ? view.now_ms+control.duration_remaining_ms : 0;
    json->raw(",\"deadline_ms\":"); json->unsigned_integer(deadline);
    json->raw(",\"saved_duration_ms\":"); json->unsigned_integer(view.settings.duration_ms); json->raw("}");
    json->raw(",\"delayed\":{\"armed\":"); json->boolean(control.delayed_armed);
    json->raw(",\"remaining_ms\":"); json->unsigned_integer(control.delayed_remaining_ms); json->raw("}");
    json->raw(",\"purge\":{\"active\":"); json->boolean(control.purge_active);
    json->raw(",\"minimum_remaining_ms\":"); json->unsigned_integer(control.purge_minimum_remaining_ms);
    json->raw(",\"sensors_fresh\":"); json->boolean(control.purge_sensors_fresh);
    json->raw(",\"temperature_ready\":"); json->boolean(control.purge_temperature_ready); json->raw("}");
    json->raw(",\"commissioning\":{\"phase\":{\"code\":"); json->quoted(commissioning_phase_text(control.commissioning.phase).code);
    json->raw(",\"ru\":"); json->quoted(commissioning_phase_text(control.commissioning.phase).ru); json->raw("}");
    json->raw(",\"reason\":{\"code\":"); json->quoted(commissioning_reason_text(control.commissioning.reason).code);
    json->raw(",\"ru\":"); json->quoted(commissioning_reason_text(control.commissioning.reason).ru); json->raw("}");
    json->raw(",\"block\":{\"code\":"); json->quoted(sirokko::block_name(control.commissioning.block));
    json->raw(",\"ru\":"); json->quoted(russian_block(control.commissioning.block)); json->raw("}");
    json->raw(",\"active\":"); json->boolean(control.commissioning.active);
    json->raw(",\"target_speed_code\":"); json->unsigned_integer(control.commissioning.target_speed_code);
    json->raw(",\"accepted_point_count\":"); json->unsigned_integer(control.commissioning.accepted_point_count);
    json->raw(",\"elapsed_ms\":"); json->unsigned_integer(control.commissioning.elapsed_ms);
    json->raw(",\"source_envelope_fingerprint\":\""); json->hexadecimal(control.commissioning.source_envelope_fingerprint);
    json->raw("\",\"installed_model_fingerprint\":\""); json->hexadecimal(control.commissioning.installed_model_fingerprint);
    json->raw("\",\"installed_model_revision\":"); json->unsigned_integer(control.commissioning.installed_model_revision); json->raw("}");
    json->raw(",\"learned_model\":"); append_model(json, control.learned_model);
    json->raw(",\"envelope\":{\"provisioned\":"); json->boolean(control.envelope_provisioned);
    json->raw(",\"fingerprint\":\""); json->hexadecimal(sirokko::envelope_fingerprint(view.envelope)); json->raw("\"}");
    json->raw(",\"events\":[");
    const size_t event_count = std::min(view.event_count, view.events.size());
    for (size_t index = 0; index < event_count; ++index) {
        const sirokko::ControlEvent& event = view.events[index];
        if (index) json->character(',');
        json->raw("{\"at_ms\":"); json->unsigned_integer(event.at_ms);
        json->raw(",\"kind\":"); json->quoted(event.kind);
        json->raw(",\"kind_ru\":"); json->quoted(russian_event(event.kind));
        json->raw(",\"state\":"); json->quoted(sirokko::state_name(event.state));
        json->raw(",\"state_ru\":"); json->quoted(russian_state(event.state));
        json->raw(",\"speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(event.speed_code));
        json->raw(",\"exhaust_milli_c\":"); json->integer(event.exhaust_milli_c);
        json->raw(",\"air_milli_c\":"); json->integer(event.air_milli_c);
        json->raw(",\"faults\":"); json->unsigned_integer(event.faults); json->character('}');
    }
    json->raw("]}");
    return json->ok();
}

bool target_is_esp32s3() {
    esp_chip_info_t info{};
    esp_chip_info(&info);
    return info.model == CHIP_ESP32S3;
}

bool app_project_matches(const esp_app_desc_t& description) {
    std::array<char, sizeof(description.project_name)> expected{};
    std::snprintf(expected.data(), expected.size(), "%s", kProjectName);
    return std::memcmp(description.project_name, expected.data(), expected.size()) == 0;
}

struct OtaSink {
    const esp_partition_t* partition{};
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

bool start_hash(mbedtls_md_context_t* context) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!context || !info) return false;
    mbedtls_md_init(context);
    return mbedtls_md_setup(context, info, 0) == 0 && mbedtls_md_starts(context) == 0;
}

bool init_ota_sink(OtaSink* sink) {
    if (!sink || !start_hash(&sink->package_hash)) return false;
    if (!start_hash(&sink->image_hash)) {
        mbedtls_md_free(&sink->package_hash);
        return false;
    }
    sink->hashes_ready = true;
    return true;
}

void cleanup_ota_sink(OtaSink* sink) {
    if (!sink) return;
    if (sink->begun && !sink->ended) esp_ota_abort(sink->handle);
    sink->begun = false;
    if (sink->hashes_ready) {
        mbedtls_md_free(&sink->package_hash);
        mbedtls_md_free(&sink->image_hash);
        sink->hashes_ready = false;
    }
}

bool begin_manifest_ota(OtaSink* sink) {
    if (!sink || sink->header_size != sink->header.size()) return false;
    ota_verify::Result result = ota_verify::parse_manifest(sink->header.data(), sink->header.size(), &sink->manifest);
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

bool consume_ota(void* context, const char* data, size_t size) {
    auto& sink = *static_cast<OtaSink*>(context);
    if (!data || !sink.hashes_ready || !runtime_maintenance_safe()) {
        sink.failure = ota_verify::Result::INPUT;
        return false;
    }
    if (sink.received > sink.expected_size || size > sink.expected_size - sink.received) {
        sink.failure = ota_verify::Result::SIZE;
        return false;
    }
    if (mbedtls_md_update(&sink.package_hash, reinterpret_cast<const uint8_t*>(data), size) != 0) {
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
            if (sink.header_size == sink.header.size() && !begin_manifest_ota(&sink)) return false;
            continue;
        }
        if (sink.image_size < sink.manifest.image_size) {
            const size_t copied = std::min(static_cast<size_t>(sink.manifest.image_size) - sink.image_size, size - offset);
            const uint8_t* image = reinterpret_cast<const uint8_t*>(data + offset);
            if (sink.image_header_size < sink.image_header.size()) {
                const size_t header_bytes = std::min(sink.image_header.size() - sink.image_header_size, copied);
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
            if (mbedtls_md_update(&sink.image_hash, image, copied) != 0 || esp_ota_write(sink.handle, image, copied) != ESP_OK) {
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

ota_verify::Result finish_ota_sink(OtaSink* sink) {
    if (!sink || !sink->hashes_ready || sink->received != sink->expected_size ||
        sink->header_size != sink->header.size() || sink->image_size != sink->manifest.image_size ||
        sink->signature_size != sink->signature.size()) return ota_verify::Result::TRUNCATED;
    web_protocol::Digest package_digest{};
    web_protocol::Digest image_digest{};
    if (mbedtls_md_finish(&sink->package_hash, package_digest.data()) != 0 ||
        mbedtls_md_finish(&sink->image_hash, image_digest.data()) != 0) return ota_verify::Result::INPUT;
    if (!web_protocol::digest_equal(package_digest, sink->expected_upload_digest)) return ota_verify::Result::DIGEST;
    if (!web_protocol::digest_equal(image_digest, sink->manifest.image_sha256)) return ota_verify::Result::DIGEST;
    const ota_verify::Result signature_result = ota_verify::verify_signature(
        sink->manifest, sink->signature.data(), sink->signature.size(), ota_public_key_pem_start,
        static_cast<size_t>(ota_public_key_pem_end - ota_public_key_pem_start));
    if (signature_result != ota_verify::Result::OK) return signature_result;
    if (esp_ota_end(sink->handle) != ESP_OK) return ota_verify::Result::IMAGE;
    sink->ended = true;
    esp_app_desc_t description{};
    if (esp_ota_get_partition_description(sink->partition, &description) != ESP_OK || !app_project_matches(description)) {
        return ota_verify::Result::PROJECT;
    }
    std::memcpy(sink->verified_version, description.version, sizeof(description.version));
    return ota_verify::Result::OK;
}

esp_err_t page_handler(httpd_req_t* request) {
    no_store(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    return httpd_resp_send(request, reinterpret_cast<const char*>(kPageGzip), sizeof(kPageGzip));
}

esp_err_t login_handler(httpd_req_t* request) {
    const uint64_t current = now_ms();
    if ((g_failed_logins >= 5 && current - g_last_login_ms < 30000) || current - g_last_login_ms < 500) {
        return reject(request, "RATE_LIMIT", "429 Too Many Requests");
    }
    g_last_login_ms = current;
    char supplied[sirokko::wifi::kPasswordCapacity]{};
    const bool header = read_header(request, "X-Sirokko-Password", supplied, sizeof(supplied));
    const bool accepted = (!g_password[0]) || (header && std::strlen(supplied) == std::strlen(g_password) && constant_equal(supplied, g_password, std::strlen(g_password)));
    std::memset(supplied, 0, sizeof(supplied));
    if (!accepted) {
        ++g_failed_logins;
        return reject(request, "AUTH", "401 Unauthorized");
    }
    g_failed_logins = 0;
    for (size_t index = 0; index < 16; ++index) {
        std::snprintf(g_session + index * 2, 3, "%02x", static_cast<unsigned>(esp_random() & 0xFFU));
    }
    g_session_expires_ms = current + kSessionLifetimeMs;
    g_challenge.clear();
    char cookie[96]{};
    std::snprintf(cookie, sizeof(cookie), "sirokko_session=%s; HttpOnly; SameSite=Strict; Path=/", g_session);
    no_store(request);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, "{\"status\":\"OK\"}");
}

esp_err_t wifi_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
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
    const bool saved = runtime_save_wifi_password(password);
    if (saved) {
        g_wifi_restart_required = std::strcmp(password, g_password) != 0;
        g_next_password_required = password[0] != 0;
    }
    std::memset(password, 0, sizeof(password));
    if (!saved) return reject(request, "STORAGE", "503 Service Unavailable");
    return send_json(request, "{\"status\":\"SAVED\"}");
}

esp_err_t state_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", "503 Service Unavailable");
    JsonBuilder json(g_state_json.data(), g_state_json.size());
    if (!build_state_json(view, &json)) return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, json.data());
}

esp_err_t challenge_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    std::array<char, kCommandBodyLimit> body{};
    size_t body_size{};
    if (!read_body(request, body.data(), body.size(), &body_size)) return reject(request, "BODY");
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", "503 Service Unavailable");
    web_protocol::Command command{};
    const web_protocol::ParseError parsed = web_protocol::parse_challenge_request(body.data(), body_size, view.settings, &command);
    std::fill(body.begin(), body.end(), 0);
    if (parsed != web_protocol::ParseError::OK) return reject(request, web_protocol::error_name(parsed));
    if (!semantic_valid(command)) return reject(request, "VALUE");
    if (command.action == sirokko::Action::CALIBRATE && view.control.commissioning.block != sirokko::Block::NONE) {
        return reject(request, "COMMISSIONING_BLOCK", "409 Conflict");
    }
    if (!revision_is_safe(command, view)) return reject(request, "MODEL_REVISION", "409 Conflict");
    sirokko::Grant issued{};
    const uint64_t parameters = runtime_hash(command);
    if (!runtime_issue(command.action, parameters, &issued)) return reject(request, "BUSY", "503 Service Unavailable");
    const web_protocol::GrantFields fields{issued.token, issued.state_version, issued.expires_ms};
    g_challenge.bind(command.action, fields, command.payload_digest);
    char response[192]{};
    const int length = std::snprintf(response, sizeof(response),
        "{\"status\":\"CHALLENGE\",\"token\":\"%llx\",\"state_version\":\"%llx\",\"expires_ms\":\"%llx\",\"boot_id\":\"%llx\"}",
        static_cast<unsigned long long>(issued.token), static_cast<unsigned long long>(issued.state_version),
        static_cast<unsigned long long>(issued.expires_ms), static_cast<unsigned long long>(view.control.boot_id));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(response)) return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, response);
}

esp_err_t command_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    std::array<char, kCommandBodyLimit> body{};
    size_t body_size{};
    if (!read_body(request, body.data(), body.size(), &body_size)) return reject(request, "BODY");

    web_protocol::Stop stop{};
    const web_protocol::ParseError stop_parsed = web_protocol::parse_stop_request(body.data(), body_size, &stop);
    if (stop_parsed == web_protocol::ParseError::OK) {
        bool previous = false;
        const web_protocol::ReplayResult replay = g_replays.lookup(stop.command_id, stop.payload_digest, &previous);
        std::fill(body.begin(), body.end(), 0);
        if (replay == web_protocol::ReplayResult::CONFLICT) return reject(request, "REPLAY", "409 Conflict");
        if (replay == web_protocol::ReplayResult::MATCH) return send_json(request, "{\"status\":\"STOP_REQUESTED\",\"replay\":true}");
        runtime_stop(false);
        g_replays.remember(stop.command_id, stop.payload_digest, true);
        return send_json(request, "{\"status\":\"STOP_REQUESTED\"}");
    }

    RuntimeView view{};
    if (!runtime_view(&view)) { std::fill(body.begin(), body.end(), 0); return reject(request, "BUSY", "503 Service Unavailable"); }
    web_protocol::Command command{};
    const web_protocol::ParseError parsed = web_protocol::parse_command_request(body.data(), body_size, view.settings, &command);
    std::fill(body.begin(), body.end(), 0);
    if (parsed != web_protocol::ParseError::OK) return reject(request, web_protocol::error_name(parsed));
    // Defense in depth for the parser-level OTA_UPLOAD_ONLY rejection above:
    // only /api/ota owns a signed upload and maintenance lifecycle.
    if (command.action == sirokko::Action::OTA) return reject(request, "OTA_UPLOAD_ONLY", "405 Method Not Allowed");
    if (command.action == sirokko::Action::CALIBRATE && view.control.commissioning.block != sirokko::Block::NONE) {
        return reject(request, "COMMISSIONING_BLOCK", "409 Conflict");
    }
    if (!revision_is_safe(command, view)) return reject(request, "MODEL_REVISION", "409 Conflict");
    bool previous = false;
    const web_protocol::ReplayResult replay = g_replays.lookup(command.command_id, command.payload_digest, &previous);
    if (replay == web_protocol::ReplayResult::CONFLICT) return reject(request, "REPLAY", "409 Conflict");
    if (replay == web_protocol::ReplayResult::MATCH) {
        return send_json(request, previous ? "{\"status\":\"QUEUED\",\"replay\":true}" : "{\"status\":\"REJECTED\",\"replay\":true}");
    }
    if (!semantic_valid(command)) return reject(request, "VALUE");
    const web_protocol::BindingResult binding = g_challenge.consume(command, view.now_ms, view.control.state_version);
    if (binding != web_protocol::BindingResult::ACCEPTED) return reject(request, "CHALLENGE", "409 Conflict");
    const sirokko::Grant grant = to_runtime_grant(command);
    const bool accepted = command.action == sirokko::Action::MODEL_REVISION
        ? runtime_revise_model(grant, command.revision)
        : runtime_submit(grant, command.action, command.delay_ms,
                         command.action == sirokko::Action::SETTINGS ? &command.settings : nullptr);
    g_replays.remember(command.command_id, command.payload_digest, accepted);
    if (!accepted) return reject(request, "NOT_ACCEPTED", "409 Conflict");
    return send_json(request, "{\"status\":\"QUEUED\"}");
}

esp_err_t ota_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    char token_text[17]{}, version_text[17]{}, expires_text[17]{}, id_text[17]{};
    char upload_size_text[24]{}, upload_digest_text[65]{};
    if (!read_header(request, "X-Sirokko-Token", token_text, sizeof(token_text)) ||
        !read_header(request, "X-Sirokko-State-Version", version_text, sizeof(version_text)) ||
        !read_header(request, "X-Sirokko-Expires", expires_text, sizeof(expires_text)) ||
        !read_header(request, "X-Sirokko-Command-Id", id_text, sizeof(id_text)) ||
        !read_header(request, "X-Sirokko-Upload-Size", upload_size_text, sizeof(upload_size_text)) ||
        !read_header(request, "X-Sirokko-Upload-SHA256", upload_digest_text, sizeof(upload_digest_text))) return reject(request, "GRANT");

    web_protocol::Command command{};
    command.action = sirokko::Action::OTA;
    command.has_grant = parse_hex64(token_text, &command.grant.token) &&
                        parse_hex64(version_text, &command.grant.state_version) &&
                        parse_hex64(expires_text, &command.grant.expires_ms);
    command.has_grant = command.has_grant && parse_hex64(id_text, &command.command_id) && command.command_id != 0 &&
                        parse_decimal_u64(upload_size_text, &command.upload_size) &&
                        parse_hex_digest(upload_digest_text, &command.upload_sha256);
    command.payload_digest = web_protocol::compute_payload_digest(command);
    if (!command.has_grant || request->content_len <= 0 || command.upload_size != static_cast<uint64_t>(request->content_len) ||
        !semantic_valid(command)) return reject(request, "UPLOAD");

    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", "503 Service Unavailable");
    bool previous = false;
    const web_protocol::ReplayResult replay = g_replays.lookup(command.command_id, command.payload_digest, &previous);
    if (replay == web_protocol::ReplayResult::CONFLICT) return reject(request, "REPLAY", "409 Conflict");
    if (replay == web_protocol::ReplayResult::MATCH) return reject(request, "REPLAY", "409 Conflict");
    if (g_challenge.consume(command, view.now_ms, view.control.state_version) != web_protocol::BindingResult::ACCEPTED) {
        return reject(request, "CHALLENGE", "409 Conflict");
    }

    const sirokko::Grant grant = to_runtime_grant(command);
    if (!runtime_submit(grant, sirokko::Action::OTA, 0, nullptr)) {
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "NOT_SAFE", "409 Conflict");
    }
    const uint64_t waiting_started = now_ms();
    while (!runtime_maintenance_safe() && now_ms() - waiting_started < kMaintenanceWaitMs) vTaskDelay(pdMS_TO_TICKS(10));
    if (!runtime_maintenance_safe()) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "NOT_SAFE", "409 Conflict");
    }

    const uint32_t maintenance_lease=runtime_maintenance_lease();
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    const size_t minimum_size = ota_verify::kManifestSize + ota_verify::kSignatureSize;
    if (!partition || static_cast<size_t>(request->content_len) < minimum_size ||
        static_cast<size_t>(request->content_len) > partition->size + minimum_size) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "SIZE");
    }

    OtaSink sink{};
    sink.partition = partition;
    sink.expected_size = static_cast<size_t>(request->content_len);
    sink.expected_upload_digest = command.upload_sha256;
    if (!init_ota_sink(&sink)) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "HASH", "500 Internal Server Error");
    }
    std::array<char, 4096> buffer{};
    const BoundedReceiveResult received = receive_exact_bounded(sink.expected_size, kOtaBodyReadBudgetMs,
        HTTPD_SOCK_ERR_TIMEOUT, buffer.data(), buffer.size(), request, http_receive, &sink, consume_ota,
        nullptr, receive_clock);
    std::fill(buffer.begin(), buffer.end(), 0);
    ota_verify::Result result = received == BoundedReceiveResult::COMPLETE
        ? finish_ota_sink(&sink)
        : (sink.failure == ota_verify::Result::OK ? ota_verify::Result::TRUNCATED : sink.failure);
    if (received == BoundedReceiveResult::DEADLINE_EXCEEDED) result = ota_verify::Result::TRUNCATED;
    cleanup_ota_sink(&sink);
    if (result != ota_verify::Result::OK) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, received == BoundedReceiveResult::DEADLINE_EXCEEDED ? "OTA_TIMEOUT" : ota_verify::result_name(result),
                      "409 Conflict");
    }
    if (!runtime_activate_ota(maintenance_lease,partition)) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "MAINTENANCE_LEASE", "409 Conflict");
    }
    g_replays.remember(command.command_id, command.payload_digest, true);
    char response[384]{};
    JsonBuilder json(response, sizeof(response));
    json.raw("{\"status\":\"VERIFIED_ACTIVATION_QUEUED\",\"expected_version\":");
    json.quoted(sink.verified_version);
    json.raw("}");
    if (!json.ok()) return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, json.data());
}

}  // namespace

bool web_start(const char* ap_password) {
    if (!sirokko::wifi::valid_password(ap_password)) return false;
    std::snprintf(g_password, sizeof(g_password), "%s", ap_password);
    g_next_password_required = g_password[0] != 0;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 5;
    httpd_handle_t server{};
    if (httpd_start(&server, &config) != ESP_OK) return false;
    const httpd_uri_t handlers[] = {
        {"/", HTTP_GET, page_handler, nullptr},
        {"/api/login", HTTP_POST, login_handler, nullptr},
        {"/api/wifi", HTTP_POST, wifi_handler, nullptr},
        {"/api/state", HTTP_GET, state_handler, nullptr},
        {"/api/challenge", HTTP_POST, challenge_handler, nullptr},
        {"/api/command", HTTP_POST, command_handler, nullptr},
        {"/api/ota", HTTP_POST, ota_handler, nullptr},
    };
    for (const httpd_uri_t& handler : handlers) {
        if (httpd_register_uri_handler(server, &handler) != ESP_OK) return false;
    }
    return true;
}
