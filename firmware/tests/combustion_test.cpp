#include "control_fixture.hpp"
#include "thermal_history.hpp"
#include <iostream>
using namespace sirokko;
using namespace control_fixture;

combustion::Monitor learned() {
    combustion::Monitor m;
    m.reset(40000, 2000);
    for (uint64_t t = 500; t <= 30000; t += 500) {
        const int noise = (t / 500) % 2 ? 1000 : -1000;
        m.observe(t, input(t, 200000 + noise, 80000 + noise / 2).sensors, 2000);
    }
    assert(m.snapshot().phase == combustion::Phase::WATCHING);
    return m;
}
void monitoring() {
    auto m = learned();
    auto s = m.observe(30500, input(30500, 140000, 78000).sensors, 2000);
    assert(s.phase == combustion::Phase::SUSPECT && s.exhaust_slope_milli_c_per_s < 0);
    assert(s.reference_exhaust_milli_c == 200000);
    // Reusing the same conversion is never another confirming observation.
    for (uint64_t now = 30520; now <= 31000; now += 20)
        assert(m.observe(now, input(30500, 140000, 78000).sensors, 2000).phase == combustion::Phase::SUSPECT);
    assert(m.observe(31000, input(31000, 200000, 80000).sensors, 2000).phase == combustion::Phase::WATCHING);
    for (uint64_t t = 31500; t <= 33000; t += 500)
        s = m.observe(t, input(t, 140000, 78000).sensors, 2000);
    assert(s.phase == combustion::Phase::LOST && s.suspect_ms == 1500);

    m = learned();
    for (uint64_t t = 30500; t <= 240000 && m.snapshot().phase != combustion::Phase::LOST; t += 500) {
        const int drop = static_cast<int>((t - 30000) / 500) * 250;
        m.observe(t, input(t, 200000 - drop, 80000).sensors, 2000);
    }
    assert(m.snapshot().phase == combustion::Phase::LOST); // No downward re-learning.
    m = learned();
    s = m.observe(30500, input(30500, 140000, 60000).sensors, 1700);
    assert(s.phase == combustion::Phase::LEARNING); // A commanded change has a different reference.
    for (uint64_t t = 31000; t < 70000; t += 500)
        m.observe(t, input(t, 140000, 60000).sensors, 1700);
    assert(m.snapshot().phase == combustion::Phase::WATCHING);
    assert(m.snapshot().reference_exhaust_milli_c == 140000);
    m = learned();
    auto invalid = input(30500).sensors;
    invalid.exhaust.valid = false;
    assert(m.observe(30500, invalid, 2000).phase == combustion::Phase::LEARNING);
    m = learned();
    assert(m.observe(100, input(100).sensors, 2000).phase == combustion::Phase::LEARNING);
    m = learned();
    m.observe(30500, input(30500, 140000, 80000).sensors, 2000);
    assert(m.observe(32500, input(32500, 140000, 80000).sensors, 2000).phase == combustion::Phase::LEARNING);

    m.reset(40000, 2000);
    for (uint64_t t = 500; t < 60000; t += 500)
        m.observe(t, input(t, 200000 - static_cast<int>(t), 80000).sensors, 2000);
    assert(m.snapshot().phase == combustion::Phase::LEARNING); // Warmup/cooldown is not a stable baseline.
    m.reset(40000, 2000);
    for (uint64_t t = 500; t < 60000; t += 500)
        m.observe(t, input(t, 200000 + ((t / 500) % 2 ? 20000 : -20000), 80000).sensors, 2000);
    assert(m.snapshot().phase == combustion::Phase::LEARNING); // Large noise cannot inflate the drop margin.

    auto c = ignition();
    c.tick(input(kIgnitionAt + 100, 200000, 80000));
    auto settings = c.settings();
    settings.mode = Mode::MANUAL;
    settings.manual_percent = 100;
    assert(c.update_settings(settings));
    auto control = c.tick(input(kIgnitionAt + 200, 200000, 80000));
    uint64_t now = kIgnitionAt + 200;
    for (unsigned i = 0; i < 65; ++i) {
        now += 500;
        control = c.tick(input(now, 200000, 80000));
    }
    assert(control.combustion.phase == combustion::Phase::WATCHING);
    auto suspicious = c;
    auto check = suspicious.tick(input(now + 500, 140000, 80000));
    assert(check.combustion.phase == combustion::Phase::SUSPECT);
    assert(!suspicious.update_settings(settings));
    auto stop = input(now + 520, 140000, 80000);
    stop.stop = true;
    assert(suspicious.tick(stop).state == State::STOP_PURGE);
    assert(suspicious.update_settings(settings));
    auto absolute = c;
    auto immediate = absolute.tick(input(now + 20, 30000, 80000));
    assert(immediate.faults & FAULT_FLAME_LOST); // Additional analysis never delays the hard floor.
    for (unsigned i = 0; i < 4; ++i) {
        now += 500;
        control = c.tick(input(now, 140000, 78000));
    }
    assert(control.faults & FAULT_COMBUSTION_DROP);
    assert(control.state == State::FAULT_PURGE && control.outputs.fan_request);
    off(control);
}
void confirmed_calibration() {
    Settings settings{};
    settings.duration_ms = 60000;
    Controller c(envelope(), settings, 1, Startup::CLEAN);
    auto i = input(100);
    i.start = true;
    auto s = c.tick(i);
    assert(s.state == State::IDLE && !s.commissioning.active);
    assert(s.start_block == Block::MODEL_UNAVAILABLE);
    i = input(101);
    i.start_commissioning = true;
    s = c.tick(i);
    assert(s.commissioning.active && !s.duration_active);
    bool saved = false;
    for (uint64_t t = 350; t < 6ULL * 3600000; t += 250) {
        i = input(t);
        if (s.outputs.fuel) {
            i.sensors.exhaust.milli_c = s.outputs.speed_code * 100;
            i.sensors.air.milli_c = s.outputs.speed_code * 40;
        }
        s = c.tick(i);
        assert(s.faults == 0);
        LearnedOperatingModel candidate{};
        if (c.pending_learned_model(&candidate)) {
            assert(s.commissioning.active && !s.outputs.fuel);
            assert(c.begin_model_storage(1) && c.seal_model_storage(1));
            auto cancelled = c;
            auto stop = i;
            stop.now_ms++;
            stop.stop = true;
            cancelled.tick(stop);
            assert(cancelled.complete_model_storage(1, true));
            stop = input(t + 2);
            auto stopped = cancelled.tick(stop);
            assert(stopped.state == State::IDLE && !stopped.commissioning.active);
            assert(!stopped.outputs.fuel && !stopped.outputs.fan_request);

            auto failed = c;
            assert(!failed.complete_model_storage(1, false));
            assert(!failed.tick(input(t + 2)).learned_model.valid);
            assert(c.complete_model_storage(1, true));
            assert(c.snapshot(t).learned_model.valid);
            i = input(t + 1);
            i.cycle_record_ready = false;
            assert(c.tick(i).state == State::IDLE);
            i = input(t + 2);
            s = c.tick(i);
            assert(s.state == State::IDLE && !s.commissioning.active && !s.outputs.fuel);
            i = input(t + 3);
            i.start = true;
            s = c.tick(i);
            assert(s.state == State::PREPURGE && !s.commissioning.active);
            assert(s.duration_active && s.duration_remaining_ms == 60000);
            saved = true;
            break;
        }
    }
    assert(saved);
    // A subsequent boot with the verified model never starts or recalibrates by itself.
    Controller rebooted(envelope(), settings, 2, Startup::CLEAN);
    assert(rebooted.install_learned_model(c.learned_model()));
    auto ready = rebooted.tick(input(100));
    assert(ready.state == State::IDLE && !ready.commissioning.active);
    auto start = input(120);
    start.start = true;
    ready = rebooted.tick(start);
    assert(ready.state == State::PREPURGE && !ready.commissioning.active);
}
void history() {
    ThermalHistory history;
    Snapshot s{};
    s.boot_id = 7;
    for (uint64_t t = 0; t < 2 * kHistoryCapacity * kHistoryPeriodMs; t += kHistoryPeriodMs)
        history.record(t, input(t).sensors, s);
    HistorySnapshot copy{};
    history.copy(&copy);
    assert(copy.count == kHistoryCapacity && copy.boot_id == 7);
    assert(copy.points.front().at_ms == kHistoryCapacity * kHistoryPeriodMs);
    for (size_t n = 1; n < copy.count; ++n)
        assert(copy.points[n].at_ms > copy.points[n - 1].at_ms);
    const uint64_t now = copy.points.back().at_ms + 20;
    auto sensors = input(now).sensors;
    sensors.air.valid = false;
    s.faults = FAULT_AIR;
    s.state = State::FAULT_PURGE;
    history.record(now, sensors, s);
    history.copy(&copy);
    assert(copy.points.back().at_ms == now && !(copy.points.back().valid & 2));
    char encoded[256]{};
    JsonBuilder json(encoded, sizeof(encoded));
    append_history_point(&json, copy.points.back());
    assert(json.ok());
    s.boot_id++;
    history.record(0, input(0).sensors, s);
    history.copy(&copy);
    assert(copy.count == 1 && copy.boot_id == 8);
}
int main() {
    monitoring();
    confirmed_calibration();
    history();
    std::cout << "PASS combustion/history: noisy reference, spike, sustained/slow drop, transitions, "
                 "stale/repeated data, real cutoff, confirmed calibration/storage/STOP, no automatic "
                 "restart, ring wrap/reboot\n";
}
