#include "persistence.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace sirokko;

namespace {
LearnedOperatingModel model(uint16_t minimum, uint32_t revision) {
    LearnedOperatingModel result{};
    result.valid = true;
    result.minimum_speed_code = minimum;
    result.maximum_speed_code = 1000;
    result.slew_up_per_s = 0.004;
    result.slew_down_per_s = 0.003;
    result.provenance = {0xA55A, 100, 200, revision};
    result.measurements.elapsed_ms = 100;
    result.measurements.tested_point_count = 2;
    result.fingerprint = learned_model_fingerprint(result);
    return result;
}
}

int main() {
    ModelCommitGate gate;
    assert(gate.revoke_precommit() && !gate.open() && !gate.claim());
    gate.reset(); assert(gate.open() && !gate.open());
    assert(gate.revoke_precommit() && !gate.claim());
    gate.reset(); assert(gate.open() && gate.claim());
    assert(!gate.revoke_precommit() && gate.claimed() && !gate.claim());
    unsigned cancelled = 0, committed = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        gate.reset(); assert(gate.open());
        std::atomic<bool> start{false}; bool claim = false, revoke = false;
        std::thread a([&] { while (!start.load()) std::this_thread::yield(); claim = gate.claim(); });
        std::thread b([&] { while (!start.load()) std::this_thread::yield(); revoke = gate.revoke_precommit(); });
        start.store(true); a.join(); b.join();
        assert(claim != revoke);
        assert(claim ? gate.claimed() : gate.cancelled());
        if (claim) ++committed; else ++cancelled;
    }

    constexpr uint32_t magic = 0x4D4F4434;  // model_active4 payload.
    const auto old_model = model(700, 3);
    const auto candidate = model(460, 4);
    ModelStageV4 stage{};
    stage.boot_id = 1; stage.transaction_id = 1; stage.previous_present = 1;
    stage.previous = record_codec::encode(magic, old_model);
    stage.candidate = record_codec::encode(magic, candidate);
    assert(valid_model_stage(stage, magic));
    assert(model_boot_consistent(stage, true, stage.previous));
    assert(model_boot_consistent(stage, true, stage.candidate));
    assert(!model_boot_consistent(stage, false, {}));
    auto different = old_model; different.minimum_speed_code = 600;
    different.fingerprint = learned_model_fingerprint(different);
    assert(!model_boot_consistent(stage, true, record_codec::encode(magic, different)));
    stage.previous_present = 0; assert(valid_model_stage(stage, magic));
    assert(model_boot_consistent(stage, false, {}));
    assert(!model_boot_consistent(stage, true, stage.previous));
    stage.transaction_id = 0; assert(!valid_model_stage(stage, magic)); stage.transaction_id = 1;
    stage.schema = 2; assert(!valid_model_stage(stage, magic)); stage.schema = 1;
    stage.previous_present = 2; assert(!valid_model_stage(stage, magic)); stage.previous_present = 0;
    stage.candidate.checksum ^= 1; assert(!valid_model_stage(stage, magic));
    std::cout << "PASS persistence: 1000 concurrent revoke/claim races (cancelled=" << cancelled
              << ", committed=" << committed << "), exact model journal consistency, staged model never boot-promoted\n";
}
