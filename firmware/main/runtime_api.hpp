#pragma once

#include <cstddef>
#include <cstdint>
#include "sirokko/control.hpp"

struct RuntimeView {
    sirokko::Snapshot control;
    sirokko::Sensors sensors;
    sirokko::Settings settings;
    uint64_t now_ms{};
};

bool runtime_view(RuntimeView* view);
bool runtime_issue(sirokko::Action action, uint64_t params_hash, sirokko::Grant* grant);
bool runtime_submit(const sirokko::Grant& grant, sirokko::Action action,
                    uint64_t delay_ms, const sirokko::Settings* settings);
void runtime_stop();
bool runtime_maintenance_safe();
void runtime_exit_maintenance();
bool web_start(const char* ap_password);
