#pragma once
#include "runtime_api.hpp"

namespace command_policy {
struct Entry {
    sirokko::Action action;
    const char *name;
};
inline constexpr Entry commands[] = {
    {sirokko::Action::START, "START"},
    {sirokko::Action::SETTINGS, "SETTINGS"},
    {sirokko::Action::TIMER, "TIMER"},
    {sirokko::Action::PROFILE, "PROFILE"},
    {sirokko::Action::RESET_FAULT, "RESET_FAULT"},
    {sirokko::Action::OTA, "OTA"},
};
inline sirokko::Block wifi_password_block(const RuntimeView &view) {
    using namespace sirokko;
    if (!view.startup_ready)
        return Block::INITIALIZING;
    if (!view.storage_ok)
        return Block::STORAGE_UNAVAILABLE;
    const auto &control = view.control;
    if (view.storage_busy || control.outputs.fan_request || control.outputs.fuel || control.outputs.glow)
        return Block::BUSY;
    if (control.state == State::MAINTENANCE)
        return Block::MAINTENANCE;
    return control.state == State::IDLE || control.state == State::UNCONFIGURED ||
                   control.state == State::FAULT
               ? Block::NONE
               : Block::BUSY;
}
inline sirokko::Block block(const RuntimeView &view, sirokko::Action action) {
    using namespace sirokko;
    const auto &control = view.control;
    if (!view.startup_ready)
        return Block::INITIALIZING;
    if (!view.storage_ok)
        return Block::STORAGE_UNAVAILABLE;
    if (view.storage_busy)
        return Block::BUSY;
    if (action == Action::START && view.envelope.provisioned && !view.local_controls_initialized)
        return Block::LOCAL_CONTROLS_UNAVAILABLE;
    switch (action) {
    case Action::START:
        return control.start_block;
    case Action::SETTINGS:
        return control.state == State::MAINTENANCE || control.combustion.phase == combustion::Phase::SUSPECT
                   ? Block::BUSY
                   : Block::NONE;
    case Action::TIMER:
        return control.run_id && (control.state == State::PREHEAT || control.state == State::IGNITION ||
                                  control.state == State::RUN)
                   ? Block::NONE
                   : Block::BUSY;
    case Action::PROFILE:
        if (control.faults)
            return Block::FAULT_LATCHED;
        if (control.outputs.fan_request || control.outputs.fuel || control.outputs.glow)
            return Block::BUSY;
        return control.state == State::IDLE || control.state == State::UNCONFIGURED ? Block::NONE
                                                                                    : Block::BUSY;
    case Action::RESET_FAULT:
        return control.reset_fault_block;
    case Action::OTA:
        if (control.faults)
            return Block::FAULT_LATCHED;
        if (!view.envelope.provisioned)
            return control.state == State::UNCONFIGURED ? Block::NONE : Block::BUSY;
        if (control.state != State::IDLE && control.state != State::UNCONFIGURED)
            return Block::BUSY;
        for (const auto *sample : {&view.sensors.air, &view.sensors.exhaust}) {
            if (!thermocouple_valid(*sample) || sample->sample_ms > view.now_ms)
                return Block::SENSOR_INVALID;
            if (view.now_ms - sample->sample_ms > kSensorStaleMs)
                return Block::SENSOR_STALE;
        }
        return cooling_complete(view.envelope, view.sensors, view.now_ms) ? Block::NONE
                                                                          : Block::INITIAL_THERMAL_STATE;
    }
    return Block::BUSY;
}
} // namespace command_policy
