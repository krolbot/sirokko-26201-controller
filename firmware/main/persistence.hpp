#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include "record_codec.hpp"
#include "sirokko/control.hpp"

namespace sirokko {
// STOP/fault may revoke only until the single CAS claim. Payloads remain in the
// control-owned job and are copied through bounded FreeRTOS queues, not shared.
class RangeCommitGate {
public:
    enum class State : uint32_t { IDLE, OPEN, CANCELLED, CLAIMED };
    bool open() {
        auto expected=State::IDLE;
        return state_.compare_exchange_strong(expected,State::OPEN);
    }
    bool revoke_precommit() {
        auto state=state_.load();
        while(state!=State::CLAIMED && state!=State::CANCELLED) {
            if(state_.compare_exchange_weak(state,State::CANCELLED)) return true;
        }
        return state==State::CANCELLED;
    }
    bool claim() {
        auto expected=State::OPEN;
        return state_.compare_exchange_strong(expected,State::CLAIMED);
    }
    bool cancelled() const {return state_.load()==State::CANCELLED;}
    bool claimed() const {return state_.load()==State::CLAIMED;}
    // Only the control owner, with no in-flight worker and no live candidate.
    void reset() {state_.store(State::IDLE);}
private:
    static_assert(std::atomic<State>::is_always_lock_free,"Range cancellation must not use a mutex");
    std::atomic<State> state_{State::IDLE};
};

using RangeRecord=record_codec::Record<OperatingRange>;
struct RangeStageV3 {
    uint32_t schema{1};
    uint64_t boot_id{};
    uint64_t transaction_id{};
    uint8_t previous_present{};
    RangeRecord previous{};
    RangeRecord candidate{};
};
inline bool same_range_record(const RangeRecord& a,const RangeRecord& b) {
    return std::memcmp(&a,&b,sizeof(a))==0;
}
inline bool valid_range_stage(const RangeStageV3& stage,uint32_t range_magic) {
    return stage.schema==1 && stage.boot_id && stage.transaction_id && stage.previous_present<=1 &&
        record_codec::valid(stage.candidate,range_magic,sizeof(stage.candidate)) && stage.candidate.value.valid &&
        (!stage.previous_present || record_codec::valid(stage.previous,range_magic,sizeof(stage.previous)));
}
// No stage is ever promoted on boot. Only an intact, already-active v2 record
// consistent with the last journal may be installed. Missing previous is legal
// only for first calibration; a corrupt or missing active record is not success.
inline bool range_boot_consistent(const RangeStageV3& stage,bool active_present,const RangeRecord& active) {
    return active_present ? same_range_record(active,stage.candidate) ||
        (stage.previous_present && same_range_record(active,stage.previous)) : !stage.previous_present;
}
} // namespace sirokko
