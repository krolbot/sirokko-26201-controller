#pragma once

#include <cstddef>
#include <cstdint>
#include "sirokko/control.hpp"

struct RuntimeView {
    sirokko::Snapshot control;
    sirokko::Sensors sensors;
    sirokko::Settings settings;
    uint64_t now_ms{};
    sirokko::SafetyEnvelope envelope;
    bool storage_ok{true};
    std::array<sirokko::ControlEvent,16> events{};
    size_t event_count{};
};

bool runtime_view(RuntimeView* view);
bool runtime_issue(sirokko::Action action, uint64_t params_hash, sirokko::Grant* grant);
bool runtime_submit(const sirokko::Grant& grant, sirokko::Action action,
                    uint64_t delay_ms, const sirokko::Settings* settings);
void runtime_stop(bool operator_combustion_problem = false);
bool runtime_revise_model(const sirokko::Grant& grant, const sirokko::LearnedModelRevision& revision);
bool runtime_network_credentials(char* ssid, size_t ssid_capacity, char* password, size_t password_capacity);
bool runtime_maintenance_safe();
uint32_t runtime_maintenance_lease();
bool runtime_activate_ota(uint32_t lease, const void* verified_partition);
void runtime_exit_maintenance();
bool web_start(const char* ap_password);
