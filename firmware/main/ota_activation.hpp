#pragma once
#include <atomic>
#include <cstdint>
#include "sirokko/control.hpp"

namespace sirokko {

enum class OtaCommitState : uint8_t {
    UNSAFE,
    LEASED,
    QUEUED,
    COMMITTING,
    CANCELLED,
    CANCELLED_ACKABLE,
    COMMITTED,
    FAILED,
};

// The control task exclusively owns observe(), queue(), claim_due(),
// finish_commit(), acknowledge_revoke(), reset() and the payload below.
// revoke_precommit() is the only method that may run from another task.
class OtaActivationGate {
public:
    static constexpr uint64_t kActivationDelayMs{200};

    // A safe observation leases the current cold MAINTENANCE interval.  A false
    // observation withdraws ordinary leases, marks a STOP/exit cancellation
    // acknowledgement-ready, but never clears that cancellation or interrupts
    // COMMITTING.
    void observe(bool safe) noexcept {
        if(safe) {
            uint32_t current=state_epoch_.load(std::memory_order_acquire);
            if(unpack_state(current)!=OtaCommitState::UNSAFE || counter_==kEpochMask) return;
            const uint32_t next_epoch=++counter_;
            const uint32_t desired=pack(next_epoch,OtaCommitState::LEASED);
            // A concurrent revoke can turn UNSAFE into CANCELLED before this CAS.
            // Consuming an unpublished counter value is safe and prevents reuse.
            state_epoch_.compare_exchange_strong(current,desired,std::memory_order_release,
                                                 std::memory_order_acquire);
            return;
        }

        uint32_t current=state_epoch_.load(std::memory_order_acquire);
        for(;;) {
            const auto current_state=unpack_state(current);
            if(current_state==OtaCommitState::UNSAFE || current_state==OtaCommitState::COMMITTING ||
               current_state==OtaCommitState::CANCELLED_ACKABLE) return;
            if(current_state==OtaCommitState::CANCELLED) {
                if(state_epoch_.compare_exchange_weak(current,
                                                       pack(unpack_epoch(current),OtaCommitState::CANCELLED_ACKABLE),
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire)) return;
                continue;
            }
            if(state_epoch_.compare_exchange_weak(current,pack(0,OtaCommitState::UNSAFE),
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
                clear_payload();
                return;
            }
        }
    }

    // A lease is externally usable only before claim.  COMMITTING and terminal
    // states deliberately publish no lease for another activation request.
    uint32_t epoch() const noexcept {
        const uint32_t current=state_epoch_.load(std::memory_order_acquire);
        const auto current_state=unpack_state(current);
        return current_state==OtaCommitState::LEASED || current_state==OtaCommitState::QUEUED ?
            unpack_epoch(current):0;
    }

    OtaCommitState state() const noexcept {
        return unpack_state(state_epoch_.load(std::memory_order_acquire));
    }

    // Control task only.  The payload is written before QUEUED is published;
    // revoke_precommit() never reads or writes it.
    bool queue(uint32_t epoch,const void* verified_partition,uint64_t now_ms) noexcept {
        if(!epoch || epoch>kEpochMask || !verified_partition) return false;
        uint32_t current=state_epoch_.load(std::memory_order_acquire);
        if(unpack_state(current)!=OtaCommitState::LEASED || unpack_epoch(current)!=epoch) return false;
        target_=verified_partition;
        queued_ms_=now_ms;
        const uint32_t desired=pack(epoch,OtaCommitState::QUEUED);
        if(state_epoch_.compare_exchange_strong(current,desired,std::memory_order_release,
                                                std::memory_order_acquire)) return true;
        clear_payload();
        return false;
    }

    // Control task only.  A successful CAS is the irrevocable boundary before
    // esp_ota_set_boot_partition(); a revoker winning the CAS returns nullptr.
    const void* claim_due(uint64_t now_ms) noexcept {
        uint32_t current=state_epoch_.load(std::memory_order_acquire);
        if(unpack_state(current)!=OtaCommitState::QUEUED || !target_ || now_ms<queued_ms_ ||
           now_ms-queued_ms_<kActivationDelayMs) return nullptr;
        const uint32_t desired=pack(unpack_epoch(current),OtaCommitState::COMMITTING);
        if(!state_epoch_.compare_exchange_strong(current,desired,std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) return nullptr;
        return target_;
    }

    // Lock-free arbitrary-task operation.  It never touches payload or a
    // 64-bit timestamp.  true means this revoke owns (or has refreshed) the
    // pre-commit cancellation latch; false means COMMITTING already won or the
    // attempt is terminal.
    bool revoke_precommit() noexcept {
        uint32_t current=state_epoch_.load(std::memory_order_acquire);
        for(;;) {
            const auto current_state=unpack_state(current);
            if(current_state==OtaCommitState::COMMITTING || current_state==OtaCommitState::COMMITTED ||
               current_state==OtaCommitState::FAILED) return false;
            const bool already_cancelled=current_state==OtaCommitState::CANCELLED ||
                current_state==OtaCommitState::CANCELLED_ACKABLE;
            const uint32_t stamp=already_cancelled ? next_cancellation_stamp(unpack_epoch(current)):
                (current_state==OtaCommitState::UNSAFE ? next_cancellation_stamp(0):unpack_epoch(current));
            const uint32_t desired=pack(stamp,OtaCommitState::CANCELLED);
            if(state_epoch_.compare_exchange_weak(current,desired,std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) return true;
        }
    }

    // Control task only, immediately after the boot-partition API returns.
    // COMMITTING cannot be revoked; success and failure remain terminal until
    // the control task observes unsafe before any later maintenance lease.
    void finish_commit(bool boot_partition_set) noexcept {
        uint32_t current=state_epoch_.load(std::memory_order_acquire);
        if(unpack_state(current)!=OtaCommitState::COMMITTING) return;
        const auto terminal=boot_partition_set ? OtaCommitState::COMMITTED:OtaCommitState::FAILED;
        if(state_epoch_.compare_exchange_strong(current,pack(unpack_epoch(current),terminal),
                                                std::memory_order_release,
                                                std::memory_order_acquire)) clear_payload();
    }

    bool commit_started() const noexcept {
        return state()==OtaCommitState::COMMITTING;
    }

    // Control task only.  observe(false) transitions CANCELLED to
    // CANCELLED_ACKABLE; this method is the only transition that clears it. A
    // concurrent revoke returns the phase to CANCELLED with a new stamp, so
    // this CAS fails and the control task must process that revoke first.
    bool acknowledge_revoke() noexcept {
        uint32_t current=state_epoch_.load(std::memory_order_acquire);
        if(unpack_state(current)!=OtaCommitState::CANCELLED_ACKABLE) return false;
        if(!state_epoch_.compare_exchange_strong(current,pack(0,OtaCommitState::UNSAFE),
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) return false;
        clear_payload();
        return true;
    }

    // Boot/test only: no concurrent revoke or control call may exist.
    void reset() noexcept {
        target_=nullptr;
        queued_ms_=0;
        counter_=0;
        state_epoch_.store(pack(0,OtaCommitState::UNSAFE),std::memory_order_release);
    }

private:
    static constexpr uint32_t kStateBits{3};
    static constexpr uint32_t kStateMask{(1U<<kStateBits)-1U};
    static constexpr uint32_t kEpochMask{0x1FFFFFFFU};
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "OtaActivationGate requires lock-free 32-bit atomics");

    static constexpr uint32_t pack(uint32_t epoch,OtaCommitState state) noexcept {
        return (epoch<<kStateBits)|static_cast<uint32_t>(state);
    }
    static constexpr uint32_t unpack_epoch(uint32_t packed) noexcept { return packed>>kStateBits; }
    static constexpr OtaCommitState unpack_state(uint32_t packed) noexcept {
        return static_cast<OtaCommitState>(packed&kStateMask);
    }
    static constexpr uint32_t next_cancellation_stamp(uint32_t stamp) noexcept {
        return stamp==kEpochMask ? 1U:stamp+1U;
    }
    void clear_payload() noexcept { target_=nullptr;queued_ms_=0; }

    // These are deliberately non-atomic: only the control task may access
    // them.  revoke_precommit() is restricted to state_epoch_ above.
    const void* target_{};
    uint64_t queued_ms_{};
    uint32_t counter_{};
    std::atomic<uint32_t> state_epoch_{};
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
