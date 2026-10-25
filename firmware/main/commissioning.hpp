#pragma once

#include <cstddef>
#include <cstdint>

#include "sirokko/operator.hpp"

class CommissioningStore {
public:
    bool begin();
    sirokko::Config load_config(bool* integrity_ok = nullptr);
    bool save_config(const sirokko::Config& config);
    uint32_t candidate_token(const sirokko::Config& config) const;
    bool accept_config(sirokko::Config& config, uint32_t token);
    bool set_field(sirokko::Config& config, const char* key, const char* value) const;
    void print_config(const sirokko::Config& config) const;

    sirokko::OperatorSettings load_operator_settings(const sirokko::Config&, bool* integrity_ok) const;
    bool save_operator_settings(const sirokko::OperatorSettings&, const sirokko::Config&) const;
    bool load_counters(sirokko::CommandCounters*) const;
    bool save_counters(const sirokko::CommandCounters&) const;
    bool save_history(const sirokko::CommandHistory&) const;
    bool load_history(sirokko::CommandHistory*) const;
    void print_history() const;

    bool load_latched_faults(uint32_t* faults) const;
    bool save_latched_faults(uint32_t faults) const;
    bool clear_latched_faults() const;
    uint32_t boot_count() const { return boot_count_; }

private:
    uint32_t boot_count_{};
};
