#pragma once

#include <cstddef>
#include <cstdint>

#include "sirokko/control.hpp"

class CommissioningStore {
public:
    bool begin();
    sirokko::Config load_config(bool* integrity_ok = nullptr);
    bool save_config(const sirokko::Config& config);
    uint32_t candidate_token(const sirokko::Config& config) const;
    bool set_field(sirokko::Config& config, const char* key, const char* value) const;
    void print_config(const sirokko::Config& config) const;

    int32_t load_setpoint(const sirokko::Config& config);
    bool save_setpoint(int32_t milli_c);

    void append_fault(uint32_t uptime_ms, sirokko::State state, uint32_t faults);
    void print_faults() const;
    uint32_t boot_count() const { return boot_count_; }

private:
    uint32_t boot_count_{};
};
