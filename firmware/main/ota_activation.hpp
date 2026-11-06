#pragma once
#include <cstdint>
#include <limits>
#include "sirokko/control.hpp"

namespace sirokko {
// Owned exclusively by the control task. Network code only reads its published
// epoch and sends a request; it never selects a boot partition or reboots.
class OtaActivationGate {
public:
    void observe(bool safe) {
        if(!safe) { epoch_=0;target_=nullptr;pending_epoch_=0;return; }
        if(!epoch_ && counter_!=std::numeric_limits<uint32_t>::max()) epoch_=++counter_;
    }
    uint32_t epoch() const {return epoch_;}
    bool queue(uint32_t epoch,const void* verified_partition,uint64_t now_ms) {
        if(!epoch || epoch!=epoch_ || !verified_partition || target_) return false;
        target_=verified_partition;pending_epoch_=epoch;queued_ms_=now_ms;return true;
    }
    const void* take_due(uint64_t now_ms) {
        if(!target_ || !epoch_ || pending_epoch_!=epoch_ || now_ms<queued_ms_ || now_ms-queued_ms_<200) return nullptr;
        const void* result=target_;target_=nullptr;pending_epoch_=0;return result;
    }
private:
    uint32_t epoch_{},counter_{},pending_epoch_{};
    uint64_t queued_ms_{};
    const void* target_{};
};

inline bool ota_control_safe(const Snapshot& state,const Sensors& sensors,
                             const ProcessProfile& profile,uint64_t now_ms) {
    const auto cold=[now_ms](const SensorSample& sample,int32_t limit) {
        return sample.valid && sample.status==0 && sample.sample_ms<=now_ms &&
            now_ms-sample.sample_ms<=kSensorStaleMs && sample.milli_c>=0 && sample.milli_c<=limit;
    };
    return state.state==State::MAINTENANCE && !state.faults && profile.qualified &&
        !state.outputs.fuel && !state.outputs.glow && !state.outputs.fan_request &&
        cold(sensors.exhaust,profile.maximum_purge_exit_exhaust_milli_c) &&
        cold(sensors.air,profile.maximum_purge_exit_air_milli_c);
}
} // namespace sirokko
