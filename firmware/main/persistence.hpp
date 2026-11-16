#pragma once
#include <cstdint>
#include "sirokko/control.hpp"

namespace sirokko {
enum class CycleRecordStatus { MISSING, CLEAN, INTERRUPTED, INVALID };

template <class PersistClean>
Startup startup_for_cycle(CycleRecordStatus cycle, bool configured, PersistClean persist_clean) {
    if (cycle == CycleRecordStatus::CLEAN)
        return Startup::CLEAN;
    // A fresh device needs a persisted clean record before heating can be enabled.
    if (cycle == CycleRecordStatus::MISSING && !configured && persist_clean())
        return Startup::CLEAN;
    return Startup::RECOVER;
}

} // namespace sirokko
