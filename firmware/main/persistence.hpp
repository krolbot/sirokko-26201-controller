#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include "record_codec.hpp"
#include "sirokko/control.hpp"

namespace sirokko {
// STOP/fault wins until the control owner seals the exact journal candidate.
class ModelCommitGate {
public:
    enum class State : uint32_t { IDLE, OPEN, CANCELLED, CLAIMED };
    bool open() {
        auto expected = State::IDLE;
        return state_.compare_exchange_strong(expected, State::OPEN);
    }
    bool revoke_precommit() {
        auto state = state_.load();
        while (state != State::CLAIMED && state != State::CANCELLED) {
            if (state_.compare_exchange_weak(state, State::CANCELLED)) return true;
        }
        return state == State::CANCELLED;
    }
    bool claim() {
        auto expected = State::OPEN;
        return state_.compare_exchange_strong(expected, State::CLAIMED);
    }
    bool cancelled() const { return state_.load() == State::CANCELLED; }
    bool claimed() const { return state_.load() == State::CLAIMED; }
    void reset() { state_.store(State::IDLE); }
private:
    static_assert(std::atomic<State>::is_always_lock_free,
                  "Model cancellation must not use a mutex");
    std::atomic<State> state_{State::IDLE};
};

using ModelRecord = record_codec::Record<LearnedOperatingModel>;
struct ModelStageV4 {
    uint32_t schema{1};
    uint64_t boot_id{};
    uint64_t transaction_id{};
    uint8_t previous_present{};
    ModelRecord previous{};
    ModelRecord candidate{};
};

inline bool same_model_record(const ModelRecord& a, const ModelRecord& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
inline bool valid_model_stage(const ModelStageV4& stage, uint32_t model_magic) {
    return stage.schema == 1 && stage.boot_id != 0 && stage.transaction_id != 0 &&
        stage.previous_present <= 1 &&
        record_codec::valid(stage.candidate, model_magic, sizeof(stage.candidate)) &&
        stage.candidate.value.valid &&
        (!stage.previous_present || record_codec::valid(stage.previous, model_magic, sizeof(stage.previous)));
}
// A journal is never a boot-time promotion request. It merely proves that the
// active record is either the prior value or the exact candidate written after seal.
inline bool model_boot_consistent(const ModelStageV4& stage, bool active_present,
                                  const ModelRecord& active) {
    return active_present ? same_model_record(active, stage.candidate) ||
        (stage.previous_present && same_model_record(active, stage.previous)) : !stage.previous_present;
}
}  // namespace sirokko
