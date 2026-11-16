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

// Owned by the control task. Only revoke_precommit() may run from another task.
class OtaActivationGate {
  public:
    static constexpr uint64_t kActivationDelayMs{200};

    // Issue a lease while cold and in MAINTENANCE. On exit, withdraw leases and
    // allow cancellation acknowledgement; an active commit continues.
    void observe(bool safe) noexcept {
        if (safe) {
            uint32_t current = state_epoch_.load(std::memory_order_acquire);
            if (unpack_state(current) != OtaCommitState::UNSAFE || counter_ == kEpochMask)
                return;
            const uint32_t next_epoch = ++counter_;
            const uint32_t desired = pack(next_epoch, OtaCommitState::LEASED);
            // A concurrent revoke may win this CAS; its unused epoch stays consumed.
            state_epoch_.compare_exchange_strong(current, desired, std::memory_order_release,
                                                 std::memory_order_acquire);
            return;
        }

        uint32_t current = state_epoch_.load(std::memory_order_acquire);
        for (;;) {
            const auto current_state = unpack_state(current);
            if (current_state == OtaCommitState::UNSAFE || current_state == OtaCommitState::COMMITTING ||
                current_state == OtaCommitState::CANCELLED_ACKABLE)
                return;
            if (current_state == OtaCommitState::CANCELLED) {
                if (state_epoch_.compare_exchange_weak(
                        current, pack(unpack_epoch(current), OtaCommitState::CANCELLED_ACKABLE),
                        std::memory_order_acq_rel, std::memory_order_acquire))
                    return;
                continue;
            }
            if (state_epoch_.compare_exchange_weak(current, pack(0, OtaCommitState::UNSAFE),
                                                   std::memory_order_acq_rel, std::memory_order_acquire)) {
                clear_payload();
                return;
            }
        }
    }

    // Publish leases only while activation can still be queued or claimed.
    uint32_t epoch() const noexcept {
        const uint32_t current = state_epoch_.load(std::memory_order_acquire);
        const auto current_state = unpack_state(current);
        return current_state == OtaCommitState::LEASED || current_state == OtaCommitState::QUEUED
                   ? unpack_epoch(current)
                   : 0;
    }

    OtaCommitState state() const noexcept {
        return unpack_state(state_epoch_.load(std::memory_order_acquire));
    }

    // Write the payload before publishing QUEUED.
    bool queue(uint32_t epoch, const void *verified_partition, uint64_t now_ms) noexcept {
        if (!epoch || epoch > kEpochMask || !verified_partition)
            return false;
        uint32_t current = state_epoch_.load(std::memory_order_acquire);
        if (unpack_state(current) != OtaCommitState::LEASED || unpack_epoch(current) != epoch)
            return false;
        target_ = verified_partition;
        queued_ms_ = now_ms;
        const uint32_t desired = pack(epoch, OtaCommitState::QUEUED);
        if (state_epoch_.compare_exchange_strong(current, desired, std::memory_order_release,
                                                 std::memory_order_acquire))
            return true;
        clear_payload();
        return false;
    }

    // Winning this CAS commits the caller to esp_ota_set_boot_partition().
    // If cancellation wins, return nullptr.
    const void *claim_due(uint64_t now_ms) noexcept {
        uint32_t current = state_epoch_.load(std::memory_order_acquire);
        if (unpack_state(current) != OtaCommitState::QUEUED || !target_ || now_ms < queued_ms_ ||
            now_ms - queued_ms_ < kActivationDelayMs)
            return nullptr;
        const uint32_t desired = pack(unpack_epoch(current), OtaCommitState::COMMITTING);
        if (!state_epoch_.compare_exchange_strong(current, desired, std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
            return nullptr;
        return target_;
    }

    // Lock-free cancellation from any task; touches only state_epoch_.
    // Returns false once committing has started or finished.
    bool revoke_precommit() noexcept {
        uint32_t current = state_epoch_.load(std::memory_order_acquire);
        for (;;) {
            const auto current_state = unpack_state(current);
            if (current_state == OtaCommitState::COMMITTING || current_state == OtaCommitState::COMMITTED ||
                current_state == OtaCommitState::FAILED)
                return false;
            const bool already_cancelled = current_state == OtaCommitState::CANCELLED ||
                                           current_state == OtaCommitState::CANCELLED_ACKABLE;
            const uint32_t stamp = already_cancelled
                                       ? next_cancellation_stamp(unpack_epoch(current))
                                       : (current_state == OtaCommitState::UNSAFE ? next_cancellation_stamp(0)
                                                                                  : unpack_epoch(current));
            const uint32_t desired = pack(stamp, OtaCommitState::CANCELLED);
            if (state_epoch_.compare_exchange_weak(current, desired, std::memory_order_acq_rel,
                                                   std::memory_order_acquire))
                return true;
        }
    }

    // Called after the boot-partition API returns. The result stays terminal
    // until observe(false) permits a new maintenance interval.
    void finish_commit(bool boot_partition_set) noexcept {
        uint32_t current = state_epoch_.load(std::memory_order_acquire);
        if (unpack_state(current) != OtaCommitState::COMMITTING)
            return;
        const auto terminal = boot_partition_set ? OtaCommitState::COMMITTED : OtaCommitState::FAILED;
        if (state_epoch_.compare_exchange_strong(current, pack(unpack_epoch(current), terminal),
                                                 std::memory_order_release, std::memory_order_acquire))
            clear_payload();
    }

    bool commit_started() const noexcept { return state() == OtaCommitState::COMMITTING; }

    // Clear cancellation after observe(false). A concurrent revoke changes
    // the stamp and requires another acknowledgement.
    bool acknowledge_revoke() noexcept {
        uint32_t current = state_epoch_.load(std::memory_order_acquire);
        if (unpack_state(current) != OtaCommitState::CANCELLED_ACKABLE)
            return false;
        if (!state_epoch_.compare_exchange_strong(current, pack(0, OtaCommitState::UNSAFE),
                                                  std::memory_order_acq_rel, std::memory_order_acquire))
            return false;
        clear_payload();
        return true;
    }

    // Initialization only, before concurrent access.
    void reset() noexcept {
        target_ = nullptr;
        queued_ms_ = 0;
        counter_ = 0;
        state_epoch_.store(pack(0, OtaCommitState::UNSAFE), std::memory_order_release);
    }

  private:
    static constexpr uint32_t kStateBits{3};
    static constexpr uint32_t kStateMask{(1U << kStateBits) - 1U};
    static constexpr uint32_t kEpochMask{0x1FFFFFFFU};
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "OtaActivationGate requires lock-free 32-bit atomics");

    static constexpr uint32_t pack(uint32_t epoch, OtaCommitState state) noexcept {
        return (epoch << kStateBits) | static_cast<uint32_t>(state);
    }
    static constexpr uint32_t unpack_epoch(uint32_t packed) noexcept { return packed >> kStateBits; }
    static constexpr OtaCommitState unpack_state(uint32_t packed) noexcept {
        return static_cast<OtaCommitState>(packed & kStateMask);
    }
    static constexpr uint32_t next_cancellation_stamp(uint32_t stamp) noexcept {
        return stamp == kEpochMask ? 1U : stamp + 1U;
    }
    void clear_payload() noexcept {
        target_ = nullptr;
        queued_ms_ = 0;
    }

    // Control-task data; cross-task cancellation uses state_epoch_.
    const void *target_{};
    uint64_t queued_ms_{};
    uint32_t counter_{};
    std::atomic<uint32_t> state_epoch_{};
};

inline bool ota_control_safe(const Snapshot &state, const Sensors &sensors, const SafetyEnvelope &envelope,
                             uint64_t now_ms) {
    return state.state == sirokko::State::MAINTENANCE && !state.faults && !state.outputs.fuel &&
           !state.outputs.glow && !state.outputs.fan_request &&
           (!envelope.provisioned || sirokko::cooling_complete(envelope, sensors, now_ms));
}
} // namespace sirokko
