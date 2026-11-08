#include "persistence.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace sirokko;
int main() {
    RangeCommitGate gate;
    assert(gate.revoke_precommit() && !gate.open() && !gate.claim());
    gate.reset();assert(gate.open() && !gate.open());
    assert(gate.revoke_precommit() && !gate.claim());
    gate.reset();assert(gate.open() && gate.claim());
    assert(!gate.revoke_precommit() && gate.claimed() && !gate.claim());
    unsigned cancelled=0,committed=0;
    for(unsigned i=0;i<1000;++i) {
        gate.reset();assert(gate.open());
        std::atomic<bool> start{false};bool claim=false,revoke=false;
        std::thread a([&]{while(!start.load())std::this_thread::yield();claim=gate.claim();});
        std::thread b([&]{while(!start.load())std::this_thread::yield();revoke=gate.revoke_precommit();});
        start.store(true);a.join();b.join();
        assert(claim!=revoke);
        assert(claim?gate.claimed():gate.cancelled());
        if(claim)++committed;else++cancelled;
    }
    constexpr uint32_t magic=0x524E4732;
    OperatingRange old_range{true,700,1000,0.005,0.005,123};
    OperatingRange candidate{true,460,1000,0.005,0.005,123};
    RangeStageV3 stage{};stage.boot_id=1;stage.transaction_id=1;stage.previous_present=1;
    stage.previous=record_codec::encode(magic,old_range);stage.candidate=record_codec::encode(magic,candidate);
    assert(valid_range_stage(stage,magic));
    assert(range_boot_consistent(stage,true,stage.previous));
    assert(range_boot_consistent(stage,true,stage.candidate));
    assert(!range_boot_consistent(stage,false,{}));
    auto different=old_range;different.minimum_speed_code=600;
    assert(!range_boot_consistent(stage,true,record_codec::encode(magic,different)));
    stage.previous_present=0;assert(valid_range_stage(stage,magic));
    assert(range_boot_consistent(stage,false,{}));
    assert(!range_boot_consistent(stage,true,stage.previous));
    stage.transaction_id=0;assert(!valid_range_stage(stage,magic));stage.transaction_id=1;
    stage.schema=2;assert(!valid_range_stage(stage,magic));stage.schema=1;
    stage.previous_present=2;assert(!valid_range_stage(stage,magic));stage.previous_present=0;
    stage.candidate.checksum^=1;assert(!valid_range_stage(stage,magic));
    std::cout<<"PASS persistence: 1000 concurrent revoke/claim races (cancelled="<<cancelled<<", committed="<<committed
             <<"), single winner, exact active-v2/journal consistency, missing/invalid/stale records\n";
}
