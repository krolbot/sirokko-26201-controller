#pragma once
#include <cstdint>
#include "sirokko/control.hpp"

namespace sirokko {
enum class CycleRecordStatus { MISSING, CLEAN, INTERRUPTED, INVALID };

template <class PersistClean>
Startup startup_for_cycle(CycleRecordStatus cycle, bool configured, PersistClean persist_clean) {
    if (cycle == CycleRecordStatus::CLEAN)
        return Startup::CLEAN;
    // No profile and no cycle means heating has never been permitted. Make the
    // clean record durable before configuration can enable heating.
    if (cycle == CycleRecordStatus::MISSING && !configured && persist_clean())
        return Startup::CLEAN;
    return Startup::RECOVER;
}

} // namespace sirokko
