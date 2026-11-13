#pragma once
#include "runtime_api.hpp"
#include "json_builder.hpp"
inline constexpr size_t kStateJsonCapacity = 16384;
struct WebStateInfo {
    const char *version{};
    bool password_required{};
    bool next_password_required{};
    bool wifi_restart_required{};
};
bool build_state_json(const RuntimeView &view, const WebStateInfo &info, JsonBuilder *json);
