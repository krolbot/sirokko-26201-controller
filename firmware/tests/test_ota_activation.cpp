#include "ota_activation.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>

using namespace sirokko;

namespace {

void wait_ready(const std::atomic<bool>& ready) {
    ready.wait(false,std::memory_order_acquire);
}

void enter_lease(OtaActivationGate& gate,uint32_t* lease) {
    gate.observe(true);
    *lease=gate.epoch();
    assert(*lease);
    assert(gate.state()==OtaCommitState::LEASED);
}

void acknowledge_cancelled(OtaActivationGate& gate) {
    gate.observe(false);
    assert(gate.state()==OtaCommitState::CANCELLED_ACKABLE);
    assert(gate.acknowledge_revoke());
    assert(gate.state()==OtaCommitState::UNSAFE);
    assert(!gate.epoch());
}

void test_lifecycle_and_delay() {
    OtaActivationGate gate;
    int partition{};
    assert(gate.state()==OtaCommitState::UNSAFE);
    assert(!gate.epoch());
    assert(!gate.queue(0,&partition,10));
    assert(!gate.claim_due(10));

    uint32_t first{};
    enter_lease(gate,&first);
    assert(!gate.queue(first,nullptr,10));
    assert(gate.queue(first,&partition,10));
    assert(gate.state()==OtaCommitState::QUEUED);
    assert(gate.epoch()==first);
    assert(!gate.claim_due(9));
    assert(!gate.claim_due(10+OtaActivationGate::kActivationDelayMs-1));
    assert(gate.claim_due(10+OtaActivationGate::kActivationDelayMs)==&partition);
    assert(gate.state()==OtaCommitState::COMMITTING);
    assert(gate.commit_started());
    assert(!gate.claim_due(999));
    assert(!gate.revoke_precommit());
    gate.finish_commit(true);
    assert(gate.state()==OtaCommitState::COMMITTED);
    assert(!gate.commit_started());
    assert(!gate.epoch());
    assert(!gate.claim_due(1000));
    assert(!gate.revoke_precommit());

    // A terminal result cannot silently turn the old safe=true into a new lease.
    gate.observe(true);
    assert(gate.state()==OtaCommitState::COMMITTED);
    gate.observe(false);
    assert(gate.state()==OtaCommitState::UNSAFE);
    uint32_t second{};
    enter_lease(gate,&second);
    assert(second!=first);
    assert(!gate.queue(first,&partition,1000));
}

void test_revoke_latch_and_old_epoch() {
    OtaActivationGate gate;
    int partition{};

    // A STOP/exit can arrive before the control task has published any lease.
    assert(gate.revoke_precommit());
    assert(gate.state()==OtaCommitState::CANCELLED);
    gate.observe(true);
    assert(gate.state()==OtaCommitState::CANCELLED);
    assert(!gate.epoch());
    // Only an observed unsafe interval makes a cancellation acknowledgeable;
    // a subsequent revoke invalidates that acknowledgement boundary again.
    assert(!gate.acknowledge_revoke());
    gate.observe(false);
    assert(gate.state()==OtaCommitState::CANCELLED_ACKABLE);
    assert(gate.revoke_precommit());
    assert(gate.state()==OtaCommitState::CANCELLED);
    assert(!gate.acknowledge_revoke());
    acknowledge_cancelled(gate);

    uint32_t first{};
    enter_lease(gate,&first);
    assert(gate.queue(first,&partition,100));
    assert(gate.revoke_precommit());
    assert(gate.state()==OtaCommitState::CANCELLED);
    assert(!gate.epoch());
    assert(!gate.claim_due(100+OtaActivationGate::kActivationDelayMs));
    gate.observe(true);  // stale safe observation cannot undo a revoke.
    assert(gate.state()==OtaCommitState::CANCELLED);
    acknowledge_cancelled(gate);

    uint32_t second{};
    enter_lease(gate,&second);
    assert(second!=first);
    assert(!gate.queue(first,&partition,400));
    assert(gate.queue(second,&partition,400));
    assert(gate.claim_due(400+OtaActivationGate::kActivationDelayMs)==&partition);
    gate.finish_commit(false);
    assert(gate.state()==OtaCommitState::FAILED);
    gate.observe(true);
    assert(gate.state()==OtaCommitState::FAILED);
    gate.observe(false);
    uint32_t third{};
    enter_lease(gate,&third);
    assert(third!=second);
}

void test_ordinary_unsafe_and_time_rollback() {
    OtaActivationGate gate;
    int partition{};
    uint32_t first{};
    enter_lease(gate,&first);
    assert(gate.queue(first,&partition,1000));
    assert(!gate.claim_due(999));
    gate.observe(false);
    assert(gate.state()==OtaCommitState::UNSAFE);
    assert(!gate.claim_due(1200));

    uint32_t second{};
    enter_lease(gate,&second);
    assert(second!=first);
    constexpr uint64_t near_wrap=std::numeric_limits<uint64_t>::max()-100U;
    assert(gate.queue(second,&partition,near_wrap));
    // A backward clock cannot underflow into a due activation.
    assert(!gate.claim_due(5));
    gate.observe(false);
    assert(gate.state()==OtaCommitState::UNSAFE);

    gate.reset();  // test-only reset; no concurrent caller exists.
    assert(gate.state()==OtaCommitState::UNSAFE);
    assert(!gate.epoch());
}

void stress_revoke_before_claim() {
    constexpr unsigned rounds=256;
    int partition{};
    for(unsigned round=0;round<rounds;++round) {
        OtaActivationGate gate;
        uint32_t lease{};
        enter_lease(gate,&lease);
        assert(gate.queue(lease,&partition,0));

        std::atomic<bool> done{false};
        bool revoked{};
        std::thread stopper([&] {
            revoked=gate.revoke_precommit();
            done.store(true,std::memory_order_release);
            done.notify_one();
        });
        wait_ready(done);
        stopper.join();

        assert(revoked);
        assert(gate.state()==OtaCommitState::CANCELLED);
        assert(!gate.claim_due(OtaActivationGate::kActivationDelayMs));
        acknowledge_cancelled(gate);
    }
}

void stress_revoke_after_claim() {
    constexpr unsigned rounds=256;
    int partition{};
    for(unsigned round=0;round<rounds;++round) {
        OtaActivationGate gate;
        uint32_t lease{};
        enter_lease(gate,&lease);
        assert(gate.queue(lease,&partition,0));
        assert(gate.claim_due(OtaActivationGate::kActivationDelayMs)==&partition);
        assert(gate.commit_started());

        bool revoked{};
        std::thread stopper([&] { revoked=gate.revoke_precommit(); });
        stopper.join();

        assert(!revoked);
        assert(gate.state()==OtaCommitState::COMMITTING);
        gate.finish_commit((round&1U)==0U);
        assert(gate.state()==((round&1U)==0U ? OtaCommitState::COMMITTED:OtaCommitState::FAILED));
    }
}

void stress_claim_revoke_cas_race() {
    constexpr unsigned rounds=2000;
    unsigned cancelled{};
    unsigned claimed{};
    int partition{};
    for(unsigned round=0;round<rounds;++round) {
        OtaActivationGate gate;
        uint32_t lease{};
        enter_lease(gate,&lease);
        assert(gate.queue(lease,&partition,0));

        std::atomic<bool> ready{false};
        std::atomic<bool> go{false};
        bool revoked{};
        std::thread stopper([&] {
            ready.store(true,std::memory_order_release);
            ready.notify_one();
            go.wait(false,std::memory_order_acquire);
            revoked=gate.revoke_precommit();
        });
        wait_ready(ready);
        go.store(true,std::memory_order_release);
        go.notify_one();
        const void* result=gate.claim_due(OtaActivationGate::kActivationDelayMs);
        stopper.join();

        if(result) {
            ++claimed;
            assert(result==&partition);
            assert(!revoked);
            assert(gate.state()==OtaCommitState::COMMITTING);
            gate.finish_commit(false);
        } else {
            ++cancelled;
            assert(revoked);
            assert(gate.state()==OtaCommitState::CANCELLED);
            acknowledge_cancelled(gate);
        }
    }
    // Scheduling chooses the winner; the test requires only the valid CAS outcomes.
    assert(cancelled+claimed==rounds);
    std::cout<<"CAS race: revoke="<<cancelled<<", claim="<<claimed<<'\n';
}

void stress_revoke_queue_race() {
    constexpr unsigned rounds=512;
    int partition{};
    for(unsigned round=0;round<rounds;++round) {
        OtaActivationGate gate;
        uint32_t lease{};
        enter_lease(gate,&lease);

        std::atomic<bool> ready{false};
        std::atomic<bool> go{false};
        bool revoked{};
        std::thread stopper([&] {
            ready.store(true,std::memory_order_release);
            ready.notify_one();
            go.wait(false,std::memory_order_acquire);
            revoked=gate.revoke_precommit();
        });
        wait_ready(ready);
        go.store(true,std::memory_order_release);
        go.notify_one();
        const bool queued=gate.queue(lease,&partition,0);
        stopper.join();

        assert(revoked);
        assert(queued || gate.state()==OtaCommitState::CANCELLED);
        assert(gate.state()==OtaCommitState::CANCELLED);
        assert(!gate.epoch());
        assert(!gate.claim_due(OtaActivationGate::kActivationDelayMs));

        // A revoke racing the explicit acknowledgement cannot be lost: either
        // it wins ACKABLE -> CANCELLED, or it follows ACK -> UNSAFE with the
        // same transition.
        gate.observe(false);
        assert(gate.state()==OtaCommitState::CANCELLED_ACKABLE);
        std::atomic<bool> ack_ready{false};
        std::atomic<bool> ack_go{false};
        bool repeated_revoke{};
        std::thread repeated_stopper([&] {
            ack_ready.store(true,std::memory_order_release);
            ack_ready.notify_one();
            ack_go.wait(false,std::memory_order_acquire);
            repeated_revoke=gate.revoke_precommit();
        });
        wait_ready(ack_ready);
        ack_go.store(true,std::memory_order_release);
        ack_go.notify_one();
        gate.acknowledge_revoke();
        repeated_stopper.join();

        assert(repeated_revoke);
        assert(gate.state()==OtaCommitState::CANCELLED);
        assert(!gate.acknowledge_revoke());
        acknowledge_cancelled(gate);
    }
}

void stress_revoke_stale_observe() {
    constexpr unsigned rounds=512;
    for(unsigned round=0;round<rounds;++round) {
        OtaActivationGate gate;
        std::atomic<bool> ready{false};
        std::atomic<bool> go{false};
        bool revoked{};
        std::thread stopper([&] {
            ready.store(true,std::memory_order_release);
            ready.notify_one();
            go.wait(false,std::memory_order_acquire);
            revoked=gate.revoke_precommit();
        });
        wait_ready(ready);
        go.store(true,std::memory_order_release);
        go.notify_one();
        gate.observe(true);
        stopper.join();

        assert(revoked);
        assert(gate.state()==OtaCommitState::CANCELLED);
        assert(!gate.epoch());
        gate.observe(true);
        assert(gate.state()==OtaCommitState::CANCELLED);
        acknowledge_cancelled(gate);
    }
}

void test_ota_control_safe() {
    // Synthetic values exercise admission only; not heater qualification limits.
    Snapshot state{};
    state.state=State::MAINTENANCE;
    ProcessProfile profile{};
    profile.qualified=true;
    profile.maximum_purge_exit_air_milli_c=30000;
    profile.maximum_purge_exit_exhaust_milli_c=40000;
    Sensors sensors{{20000,1000,0,true},{20000,1000,0,true},{20000,1000,0,true}};
    assert(ota_control_safe(state,sensors,profile,1000));
    assert(ota_control_safe(state,sensors,profile,1000+kSensorStaleMs));
    assert(!ota_control_safe(state,sensors,profile,999));
    assert(!ota_control_safe(state,sensors,profile,1000+kSensorStaleMs+1));
    sensors.air.milli_c=30001;
    assert(!ota_control_safe(state,sensors,profile,1000));
    sensors.air.milli_c=20000;
    sensors.exhaust.milli_c=40001;
    assert(!ota_control_safe(state,sensors,profile,1000));
    sensors.exhaust.milli_c=20000;
    sensors.exhaust.status=1;
    assert(!ota_control_safe(state,sensors,profile,1000));
    sensors.exhaust.status=0;
    sensors.exhaust.valid=false;
    assert(!ota_control_safe(state,sensors,profile,1000));
    sensors.exhaust.valid=true;
    state.outputs.fuel=true;
    assert(!ota_control_safe(state,sensors,profile,1000));
    state.outputs.fuel=false;
    state.outputs.glow=true;
    assert(!ota_control_safe(state,sensors,profile,1000));
    state.outputs.glow=false;
    state.outputs.fan_request=true;
    assert(!ota_control_safe(state,sensors,profile,1000));
    state.outputs.fan_request=false;
    state.faults=FAULT_STORAGE;
    assert(!ota_control_safe(state,sensors,profile,1000));
    state.faults=0;
    state.state=State::IDLE;
    assert(!ota_control_safe(state,sensors,profile,1000));
    state.state=State::MAINTENANCE;
    profile.qualified=false;
    assert(!ota_control_safe(state,sensors,profile,1000));
}

} // namespace

int main() {
    test_lifecycle_and_delay();
    test_revoke_latch_and_old_epoch();
    test_ordinary_unsafe_and_time_rollback();
    stress_revoke_before_claim();
    stress_revoke_after_claim();
    stress_claim_revoke_cas_race();
    stress_revoke_queue_race();
    stress_revoke_stale_observe();
    test_ota_control_safe();
    std::cout<<"PASS: OTA atomic revoke/claim lease, cancellation latch, old epoch, rollback, delay and cold/output gates\n";
}
