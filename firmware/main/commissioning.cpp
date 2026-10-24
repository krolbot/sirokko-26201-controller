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
    HASH_FIELD(revision);
    HASH_FIELD(boot_self_test_timeout_ms);
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
    HASH_FIELD(low_pwm_permille);
#undef HASH_FIELD
    return hash;
}

bool parse_unsigned(const char* text, uint32_t* output) {
    errno = 0;
    char* end{};
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX) return false;
    *output = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_signed(const char* text, int32_t* output) {
    errno = 0;
    char* end{};
    const long long parsed = std::strtoll(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) return false;
    *output = static_cast<int32_t>(parsed);
    return true;
}

bool open_nvs(nvs_handle_t* handle, nvs_open_mode_t mode = NVS_READWRITE) {
    return nvs_open(kNamespace, mode, handle) == ESP_OK;
}
}  // namespace

bool CommissioningStore::begin() {
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        error = nvs_flash_init();
    }
    if (error != ESP_OK) return false;

    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return false;
    nvs_get_u32(handle, "boot_count", &boot_count_);
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
            return stored.config;
        }
    }
    sirokko::Config blank{};
    blank.schema_version = 1;
    blank.revision = sirokko::Revision::A;
    blank.commissioning_accepted = false;
    return blank;
}

bool CommissioningStore::save_config(const sirokko::Config& config) {
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

bool CommissioningStore::set_field(sirokko::Config& c, const char* key, const char* value) const {
    if (std::strcmp(key, "revision") == 0) {
        if (std::strcmp(value, "a") == 0 || std::strcmp(value, "A") == 0) c.revision = sirokko::Revision::A;
        else if (std::strcmp(value, "b") == 0 || std::strcmp(value, "B") == 0) c.revision = sirokko::Revision::B;
        else return false;
        c.commissioning_accepted = false;
        return true;
    }
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
    if (std::strcmp(key, "low_pwm_permille") == 0) {
        uint32_t parsed{};
        if (!parse_unsigned(value, &parsed) || parsed > UINT16_MAX) return false;
        c.low_pwm_permille = static_cast<uint16_t>(parsed);
        c.commissioning_accepted = false;
        return true;
    }
#undef SET_U32
#undef SET_I32
    return false;
}

void CommissioningStore::print_config(const sirokko::Config& c) const {
    std::printf("schema_version=%" PRIu32 " accepted=%u revision=%c token=%08" PRIX32 "\n",
                c.schema_version, c.commissioning_accepted,
                c.revision == sirokko::Revision::A ? 'A' : 'B', candidate_token(c));
#define PRINT_U32(field) std::printf(#field "=%" PRIu32 "\n", c.field)
#define PRINT_I32(field) std::printf(#field "=%" PRId32 "\n", c.field)
    PRINT_U32(boot_self_test_timeout_ms);
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
    std::printf("low_pwm_permille=%u\n", c.low_pwm_permille);
#undef PRINT_U32
#undef PRINT_I32
}

int32_t CommissioningStore::load_setpoint(const sirokko::Config& config) {
    int32_t setpoint = config.setpoint_default_milli_c;
    nvs_handle_t handle{};
    if (open_nvs(&handle, NVS_READONLY)) {
        int32_t stored{};
        if (nvs_get_i32(handle, "tset_mC", &stored) == ESP_OK &&
            stored >= config.setpoint_min_milli_c && stored <= config.setpoint_max_milli_c) {
            setpoint = stored;
        }
        nvs_close(handle);
    }
    return setpoint;
}

bool CommissioningStore::save_setpoint(int32_t milli_c) {
    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return false;
    const bool ok = nvs_set_i32(handle, "tset_mC", milli_c) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

void CommissioningStore::append_fault(uint32_t uptime_ms, sirokko::State state, uint32_t faults) {
    FaultJournal journal{};
    size_t size = sizeof(journal);
    nvs_handle_t handle{};
    if (!open_nvs(&handle)) return;
    if (nvs_get_blob(handle, "fault_log", &journal, &size) != ESP_OK ||
        size != sizeof(journal) || journal.magic != kJournalMagic) {
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
    if (error != ESP_OK || size != sizeof(journal) || journal.magic != kJournalMagic) {
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
