#pragma once
#include "sirokko/control.hpp"

namespace sirokko {
enum class ServiceResult : uint8_t { OK, BUSY, CONFIG, SENSOR, HOT, OUTPUT, REPLAY, INVALID, UNAUTHORIZED, IO, HASH, TARGET, VERSION };
// Owned only by the control task. A granted lease excludes START until explicit release.
class ServiceLease {
public:
    ServiceResult acquire(const Status&, const Inputs&, const Config&, bool output_healthy);
    static ServiceResult eligible(const Status&, const Inputs&, const Config&, bool output_healthy);
    void release() { held_ = false; }
    bool held() const { return held_; }
private:
    bool held_{};
};
const char* service_result_name(ServiceResult);
} // namespace sirokko
