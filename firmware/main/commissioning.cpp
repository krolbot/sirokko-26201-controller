#include "commissioning.hpp"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char kNamespace[] = "sirokko";
constexpr uint32_t kConfigMagic = 0x5349524Fu;  // SIRO
constexpr uint32_t kJournalMagic = 0x464C4F47u; // FLOG

struct StoredConfig {
    uint32_t magic;
    sirokko::Config config;
    uint32_t digest;
};

struct FaultRecord {
    uint32_t boot_count;
    uint32_t uptime_ms;
    uint32_t state;
    uint32_t faults;
};

struct FaultJournal {
    uint32_t magic;
    uint32_t next;
    uint32_t count;
    FaultRecord records[8];
};

void hash_bytes(uint32_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
}

template <typename T>
void hash_value(uint32_t& hash, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    hash_bytes(hash, &value, sizeof(value));
}

uint32_t config_digest(const sirokko::Config& c, bool include_acceptance) {
    uint32_t hash = 2166136261u;
#define HASH_FIELD(field) hash_value(hash, c.field)
    HASH_FIELD(schema_version);
    if (include_acceptance) HASH_FIELD(commissioning_accepted);
    HASH_FIELD(boot_self_test_timeout_ms);
    HASH_FIELD(recovery_purge_min_ms);
    HASH_FIELD(prepurge_ms);
    HASH_FIELD(preheat_ms);
    HASH_FIELD(spinup_ms);
    HASH_FIELD(ignition_window_ms);
    HASH_FIELD(flame_confirm_ms);
    HASH_FIELD(postpurge_min_ms);
    HASH_FIELD(stage_min_ms);
    HASH_FIELD(restart_inhibit_ms);
    HASH_FIELD(glow_max_ms);
    HASH_FIELD(sensor_stale_ms);
    HASH_FIELD(heartbeat_toggle_ms);
    HASH_FIELD(sensor_min_milli_c);
    HASH_FIELD(sensor_max_milli_c);
    HASH_FIELD(exhaust_overtemp_milli_c);
    HASH_FIELD(air_overtemp_milli_c);
    HASH_FIELD(cool_exhaust_milli_c);
    HASH_FIELD(cool_air_milli_c);
    HASH_FIELD(hot_start_exhaust_milli_c);
    HASH_FIELD(hot_start_air_milli_c);
    HASH_FIELD(flame_rise_milli_c);
    HASH_FIELD(flame_min_exhaust_milli_c);
    HASH_FIELD(flame_hold_exhaust_milli_c);
    HASH_FIELD(setpoint_min_milli_c);
    HASH_FIELD(setpoint_max_milli_c);
    HASH_FIELD(setpoint_default_milli_c);
    HASH_FIELD(start_delta_milli_c);
    HASH_FIELD(high_to_low_delta_milli_c);
    HASH_FIELD(low_to_high_delta_milli_c);
    HASH_FIELD(stop_delta_milli_c);
    HASH_FIELD(pwm_frequency_hz);
    HASH_FIELD(recovery_purge_pwm_permille);
    HASH_FIELD(low_pwm_permille);
    HASH_FIELD(profile_revision);
    HASH_FIELD(run_low_qualified);
#undef HASH_FIELD
    return hash;
}

bool parse_unsigned(const char* text, uint32_t* output) {
    errno = 0;
    char* end{};
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0' || text[0] == '-' || parsed > UINT32_MAX) return false;
    *output = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_signed(const char* text, int32_t* output) {
    errno = 0;
    char* end{};
    const long long parsed = std::strtoll(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) return false;
    *output = static_cast<int32_t>(parsed);
    return true;
}

bool open_nvs(nvs_handle_t* handle, nvs_open_mode_t mode = NVS_READWRITE) {
    return nvs_open(kNamespace, mode, handle) == ESP_OK;
}
}  // namespace

bool CommissioningStore::begin() {
    // Do not erase critical records to hide full, corrupt or newer-schema NVS.
    const esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) return false;

    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return false;
    nvs_get_u32(handle, "boot_count", &boot_count_);
    if (boot_count_ == UINT32_MAX) { nvs_close(handle); return false; }
    ++boot_count_;
    const bool ok = nvs_set_u32(handle, "boot_count", boot_count_) == ESP_OK &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

sirokko::Config CommissioningStore::load_config(bool* integrity_ok) {
    if (integrity_ok) *integrity_ok = false;
    StoredConfig stored{};
    size_t size = sizeof(stored);
    nvs_handle_t handle{};
    if (open_nvs(&handle, NVS_READONLY)) {
        const esp_err_t error = nvs_get_blob(handle, "config", &stored, &size);
        nvs_close(handle);
        if (error == ESP_OK && size == sizeof(stored) && stored.magic == kConfigMagic &&
            stored.digest == config_digest(stored.config, true)) {
            if (integrity_ok) *integrity_ok = true;
            if (sirokko::validate_config(stored.config, false) != sirokko::CONFIG_OK) {
                stored.config.commissioning_accepted = false;
                std::printf("NVS profile rejected; CONFIG_PWM=%u. Correct parameters, validate, accept and reboot. No clamping.\n",
                            (sirokko::validate_config(stored.config, false) & sirokko::CONFIG_PWM) != 0);
            }
            return stored.config;
        }
    }
    sirokko::Config blank{};
    blank.schema_version = sirokko::kConfigSchemaVersion;
    blank.commissioning_accepted = false;
    return blank;
}

bool CommissioningStore::save_config(const sirokko::Config& config) {
    if (config.commissioning_accepted && sirokko::validate_config(config) != sirokko::CONFIG_OK) return false;
    StoredConfig stored{};
    stored.magic = kConfigMagic;
    stored.config = config;
    stored.digest = config_digest(config, true);
    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return false;
    const bool ok = nvs_set_blob(handle, "config", &stored, sizeof(stored)) == ESP_OK &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

uint32_t CommissioningStore::candidate_token(const sirokko::Config& config) const {
    return config_digest(config, false);
}

bool CommissioningStore::accept_config(sirokko::Config& config, uint32_t token) {
    if (token != candidate_token(config) ||
        sirokko::validate_config(config, false) != sirokko::CONFIG_OK) return false;
    sirokko::Config accepted = config;
    accepted.commissioning_accepted = true;
    if (!save_config(accepted)) return false;
    config = accepted;
    return true;
}

bool CommissioningStore::set_field(sirokko::Config& c, const char* key, const char* value) const {
#define SET_U32(field) \
    if (std::strcmp(key, #field) == 0) { \
        uint32_t parsed{}; \
        if (!parse_unsigned(value, &parsed)) return false; \
        c.field = parsed; \
        c.commissioning_accepted = false; \
        return true; \
    }
#define SET_I32(field) \
    if (std::strcmp(key, #field) == 0) { \
        int32_t parsed{}; \
        if (!parse_signed(value, &parsed)) return false; \
        c.field = parsed; \
        c.commissioning_accepted = false; \
        return true; \
    }
    SET_U32(boot_self_test_timeout_ms)
    SET_U32(profile_revision)
    if (std::strcmp(key, "run_low_qualified") == 0) {
        uint32_t parsed{};
        if (!parse_unsigned(value, &parsed) || parsed > 1) return false;
        c.run_low_qualified = parsed != 0;
        c.commissioning_accepted = false;
        return true;
    }
    SET_U32(recovery_purge_min_ms)
    SET_U32(prepurge_ms)
    SET_U32(preheat_ms)
    SET_U32(spinup_ms)
    SET_U32(ignition_window_ms)
    SET_U32(flame_confirm_ms)
    SET_U32(postpurge_min_ms)
    SET_U32(stage_min_ms)
    SET_U32(restart_inhibit_ms)
    SET_U32(glow_max_ms)
    SET_U32(sensor_stale_ms)
    SET_U32(heartbeat_toggle_ms)
    SET_I32(sensor_min_milli_c)
    SET_I32(sensor_max_milli_c)
    SET_I32(exhaust_overtemp_milli_c)
    SET_I32(air_overtemp_milli_c)
    SET_I32(cool_exhaust_milli_c)
    SET_I32(cool_air_milli_c)
    SET_I32(hot_start_exhaust_milli_c)
    SET_I32(hot_start_air_milli_c)
    SET_I32(flame_rise_milli_c)
    SET_I32(flame_min_exhaust_milli_c)
    SET_I32(flame_hold_exhaust_milli_c)
    SET_I32(setpoint_min_milli_c)
    SET_I32(setpoint_max_milli_c)
    SET_I32(setpoint_default_milli_c)
    SET_I32(start_delta_milli_c)
    SET_I32(high_to_low_delta_milli_c)
    SET_I32(low_to_high_delta_milli_c)
    SET_I32(stop_delta_milli_c)
    if (std::strcmp(key, "pwm_frequency_hz") == 0) {
        uint32_t parsed{};
        if (!parse_unsigned(value, &parsed) || !sirokko::pwm_frequency_supported(parsed)) return false;
        c.pwm_frequency_hz = parsed;
        c.commissioning_accepted = false;
        return true;
    }
    if (std::strcmp(key, "recovery_purge_pwm_permille") == 0 ||
        std::strcmp(key, "low_pwm_permille") == 0) {
        uint32_t parsed{};
        if (!parse_unsigned(value, &parsed) || parsed == 0 || parsed > 1000 ||
            !sirokko::speed_supported(static_cast<uint16_t>(parsed)) ||
            (std::strcmp(key, "low_pwm_permille") == 0 && parsed > sirokko::kPwmMaximumPermille)) return false;
        if (std::strcmp(key, "recovery_purge_pwm_permille") == 0) {
            c.recovery_purge_pwm_permille = static_cast<uint16_t>(parsed);
        } else {
            c.low_pwm_permille = static_cast<uint16_t>(parsed);
        }
        c.commissioning_accepted = false;
        return true;
    }
#undef SET_U32
#undef SET_I32
    return false;
}

void CommissioningStore::print_config(const sirokko::Config& c) const {
    std::printf("schema_version=%" PRIu32 " accepted=%u drive=PWM token=%08" PRIX32 "\n",
                c.schema_version, c.commissioning_accepted, candidate_token(c));
#define PRINT_U32(field) std::printf(#field "=%" PRIu32 "\n", c.field)
#define PRINT_I32(field) std::printf(#field "=%" PRId32 "\n", c.field)
    PRINT_U32(boot_self_test_timeout_ms);
    PRINT_U32(profile_revision);
    std::printf("run_low_qualified=%u\n", c.run_low_qualified);
    PRINT_U32(recovery_purge_min_ms);
    PRINT_U32(prepurge_ms);
    PRINT_U32(preheat_ms);
    PRINT_U32(spinup_ms);
    PRINT_U32(ignition_window_ms);
    PRINT_U32(flame_confirm_ms);
    PRINT_U32(postpurge_min_ms);
    PRINT_U32(stage_min_ms);
    PRINT_U32(restart_inhibit_ms);
    PRINT_U32(glow_max_ms);
    PRINT_U32(sensor_stale_ms);
    PRINT_U32(heartbeat_toggle_ms);
    PRINT_I32(sensor_min_milli_c);
    PRINT_I32(sensor_max_milli_c);
    PRINT_I32(exhaust_overtemp_milli_c);
    PRINT_I32(air_overtemp_milli_c);
    PRINT_I32(cool_exhaust_milli_c);
    PRINT_I32(cool_air_milli_c);
    PRINT_I32(hot_start_exhaust_milli_c);
    PRINT_I32(hot_start_air_milli_c);
    PRINT_I32(flame_rise_milli_c);
    PRINT_I32(flame_min_exhaust_milli_c);
    PRINT_I32(flame_hold_exhaust_milli_c);
    PRINT_I32(setpoint_min_milli_c);
    PRINT_I32(setpoint_max_milli_c);
    PRINT_I32(setpoint_default_milli_c);
    PRINT_I32(start_delta_milli_c);
    PRINT_I32(high_to_low_delta_milli_c);
    PRINT_I32(low_to_high_delta_milli_c);
    PRINT_I32(stop_delta_milli_c);
    PRINT_U32(pwm_frequency_hz);
    std::printf("recovery_purge_pwm_permille=%u\n", c.recovery_purge_pwm_permille);
    std::printf("low_pwm_permille=%u\n", c.low_pwm_permille);
#undef PRINT_U32
#undef PRINT_I32
}

namespace {
template <typename T> struct StoredRecord {
    uint32_t schema;
    T data;
    uint32_t digest;
};
template <typename T> bool write_record(const char* key, const T& data) {
    StoredRecord<T> record{};
    record.schema = 1;
    record.data = data;
    uint32_t hash = 2166136261u;
    hash_bytes(hash, &record, offsetof(StoredRecord<T>, digest));
    record.digest = hash;
    nvs_handle_t h{};
    if (!open_nvs(&h)) return false;
    const bool ok = nvs_set_blob(h, key, &record, sizeof(record)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
// NOT_FOUND is distinct from a corrupt/unknown-schema record; neither is automatically erased.
template <typename T> esp_err_t read_record(const char* key, T* data) {
    StoredRecord<T> record{};
    nvs_handle_t h{};
    if (!open_nvs(&h, NVS_READONLY)) return ESP_FAIL;
    size_t size = sizeof(record);
    const esp_err_t error = nvs_get_blob(h, key, &record, &size);
    nvs_close(h);
    if (error != ESP_OK) return error;
    uint32_t hash = 2166136261u;
    hash_bytes(hash, &record, offsetof(StoredRecord<T>, digest));
    if (size != sizeof(record) || record.schema != 1 || hash != record.digest) return ESP_FAIL;
    *data = record.data;
    return ESP_OK;
}
struct SavedHistory {
    uint32_t count;
    sirokko::EventRecord events[sirokko::kEventCapacity];
};
}

sirokko::OperatorSettings CommissioningStore::load_operator_settings(const sirokko::Config& c, bool* ok) const {
    sirokko::OperatorSettings settings{};
    settings.setpoint_milli_c = c.setpoint_default_milli_c;
    const auto error = read_record("operator_v1", &settings);
    *ok = (error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND) && sirokko::operator_settings_valid(settings, c);
    return settings;
}
bool CommissioningStore::save_operator_settings(const sirokko::OperatorSettings& s, const sirokko::Config& c) const {
    return sirokko::operator_settings_valid(s, c) && write_record("operator_v1", s);
}
bool CommissioningStore::load_counters(sirokko::CommandCounters* counters) const {
    const auto error = read_record("counters_v1", counters);
    return error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND;
}
bool CommissioningStore::save_counters(const sirokko::CommandCounters& counters) const {
    return write_record("counters_v1", counters);
}
bool CommissioningStore::save_history(const sirokko::CommandHistory& history) const {
    SavedHistory saved{};
    saved.count = history.count();
    for (size_t i = 0; i < history.count(); ++i) saved.events[i] = history.event(i);
    return write_record("history_v1", saved);
}
void CommissioningStore::print_history() const {
    SavedHistory saved{};
    if (read_record("history_v1", &saved) != ESP_OK || saved.count > sirokko::kEventCapacity) {
        std::printf("history unavailable or invalid\n");
        return;
    }
    for (uint32_t i = 0; i < saved.count; ++i) {
        const auto& e = saved.events[i];
        if (static_cast<unsigned>(e.from) > static_cast<unsigned>(sirokko::State::LOCKOUT) ||
            static_cast<unsigned>(e.to) > static_cast<unsigned>(sirokko::State::LOCKOUT)) {
            std::printf("history invalid state\n"); return;
        }
        std::printf("boot=%lu session=%lu uptime_ms=%llu %s->%s reason=%u faults=%08lX\n",
                    static_cast<unsigned long>(e.boot_id), static_cast<unsigned long>(e.session_id),
                    static_cast<unsigned long long>(e.uptime_ms), sirokko::state_name(e.from),
                    sirokko::state_name(e.to), static_cast<unsigned>(e.reason), static_cast<unsigned long>(e.faults));
    }
}

void CommissioningStore::append_fault(uint32_t uptime_ms, sirokko::State state, uint32_t faults) {
    FaultJournal journal{};
    size_t size = sizeof(journal);
    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return;
    if (nvs_get_blob(handle, "fault_log", &journal, &size) != ESP_OK ||
        size != sizeof(journal) || journal.magic != kJournalMagic || journal.next >= 8 || journal.count > 8) {
        journal = {};
        journal.magic = kJournalMagic;
    }
    journal.records[journal.next] = {boot_count_, uptime_ms, static_cast<uint32_t>(state), faults};
    journal.next = (journal.next + 1) % 8;
    if (journal.count < 8) ++journal.count;
    nvs_set_blob(handle, "fault_log", &journal, sizeof(journal));
    nvs_commit(handle);
    nvs_close(handle);
}

void CommissioningStore::print_faults() const {
    FaultJournal journal{};
    size_t size = sizeof(journal);
    nvs_handle_t handle{};
    if (!open_nvs(&handle, NVS_READONLY)) {
        std::printf("fault journal unavailable\n");
        return;
    }
    const esp_err_t error = nvs_get_blob(handle, "fault_log", &journal, &size);
    nvs_close(handle);
    if (error != ESP_OK || size != sizeof(journal) || journal.magic != kJournalMagic || journal.next >= 8 || journal.count > 8) {
        std::printf("fault journal empty\n");
        return;
    }
    for (uint32_t i = 0; i < journal.count; ++i) {
        const uint32_t index = (journal.next + 8 - journal.count + i) % 8;
        const auto& record = journal.records[index];
        std::printf("boot=%" PRIu32 " uptime_ms=%" PRIu32 " state=%s faults=%08" PRIX32 "\n",
                    record.boot_count, record.uptime_ms,
                    sirokko::state_name(static_cast<sirokko::State>(record.state)), record.faults);
    }
}

bool CommissioningStore::load_latched_faults(uint32_t* faults) const {
    if (!faults) return false;
    *faults = 0;
    nvs_handle_t handle{};
    if (!open_nvs(&handle, NVS_READONLY)) return false;
    const esp_err_t error = nvs_get_u32(handle, "latched", faults);
    nvs_close(handle);
    return error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND;
}

bool CommissioningStore::save_latched_faults(uint32_t faults) const {
    if (faults == 0) return false;
    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return false;
    const bool ok = nvs_set_u32(handle, "latched", faults) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool CommissioningStore::clear_latched_faults() const {
    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return false;
    const esp_err_t erase = nvs_erase_key(handle, "latched");
    const bool ok = (erase == ESP_OK || erase == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}
