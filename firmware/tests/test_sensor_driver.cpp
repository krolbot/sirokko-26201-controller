// Execute the production MAX31856/DS18B20 driver against a register-level peripheral model.
#include "sensor_stubs/fake_sensors_idf.hpp"

#include "../main/board.hpp"
#include "../main/sensors.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

struct Fixture {
    SensorSuite suite;

    Fixture() {
        fake_sensors::reset();
        assert(suite.begin());
        assert(fake_sensors::devices.size() == fake_sensors::kMax31856Count);
    }

    void poll(uint64_t now_ms) {
        fake_sensors::clock_us = now_ms * 1000U;
        suite.poll(now_ms);
    }

    sirokko::Sensors readings() const { return suite.readings(); }
};

Fixture& fixture() {
    // Keep production SensorSuite objects reachable for the process lifetime: firmware owns this object globally too.
    static auto* all = new std::vector<std::unique_ptr<Fixture>>;
    all->push_back(std::make_unique<Fixture>());
    return *all->back();
}

void start_conversion(Fixture& value) {
    value.poll(1);
    assert(fake_sensors::cr0_write_count(0, 0x55) == 1);
    assert(fake_sensors::cr0_write_count(1, 0x55) == 1);
}

void complete_conversion(Fixture& value, uint64_t now_ms) {
    fake_sensors::set_ready(0, true);
    fake_sensors::set_ready(1, true);
    value.poll(now_ms);
}

sirokko::TickInput tick_input(uint64_t now_ms, const sirokko::Sensors& sensors) {
    sirokko::TickInput input{};
    input.now_ms = now_ms;
    input.sensors = sensors;
    return input;
}

sirokko::ProcessProfile qualified_profile() {
    sirokko::ProcessProfile profile{};
    profile.qualified = true;
    profile.recovery_purge_ms = 1;
    profile.prepurge_ms = 1;
    profile.preheat_ms = 1;
    profile.ignition_ms = 1;
    profile.verify_ms = 1;
    profile.postpurge_ms = 1;
    profile.maximum_purge_exit_exhaust_milli_c = 150000;
    profile.maximum_purge_exit_air_milli_c = 150000;
    profile.maximum_exhaust_milli_c = 200000;
    profile.maximum_air_milli_c = 200000;
    profile.minimum_ignition_rise_milli_c = 1000;
    profile.minimum_run_exhaust_milli_c = 50000;
    profile.purge_speed_code = 1000;
    profile.run_speed_code = 2000;
    profile.minimum_ignition_air_rise_milli_c = 1000;
    assert(sirokko::valid_process_profile(profile));
    return profile;
}

void test_initialization_and_normal_measurements() {
    Fixture& value = fixture();
    assert(fake_sensors::bus_config.mosi_io_num == board::kMosi);
    assert(fake_sensors::bus_config.miso_io_num == board::kMiso);
    assert(fake_sensors::bus_config.sclk_io_num == board::kSck);
    assert(fake_sensors::bus_config.max_transfer_sz == 16);
    assert(fake_sensors::device_configs.size() == 2);
    for (size_t index = 0; index < 2; ++index) {
        assert(fake_sensors::device_configs[index].mode == 3);
        assert(fake_sensors::device_configs[index].clock_speed_hz == board::kSpiClockHz);
        assert(fake_sensors::device(index).registers[0x00] == 0x15);  // Interrupt fault mode, CJ enabled, 50 Hz.
        assert(fake_sensors::device(index).registers[0x01] == 0x03);  // Type K.
        assert(fake_sensors::device(index).registers[0x02] == 0x00);
    }
    assert(fake_sensors::device(0).chip_select == board::kExhaustCs);
    assert(fake_sensors::device(1).chip_select == board::kAirCs);

    fake_sensors::set_temperature_milli_c(0, 100000);
    fake_sensors::set_temperature_milli_c(1, 90000);
    start_conversion(value);
    complete_conversion(value, 170);
    const auto thermal = value.readings();
    assert(thermal.exhaust.valid && thermal.exhaust.status == 0);
    assert(thermal.exhaust.milli_c == 100000 && thermal.exhaust.sample_ms == 170);
    assert(thermal.air.valid && thermal.air.status == 0);
    assert(thermal.air.milli_c == 90000 && thermal.air.sample_ms == 170);
    assert(fake_sensors::cr0_write_count(0, 0x17) == 0);
    assert(fake_sensors::cr0_write_count(1, 0x17) == 0);

    value.poll(801);  // Also completes the normal 12-bit DS18B20 conversion.
    const auto all = value.readings();
    assert(all.room.valid && all.room.status == 0 && all.room.milli_c == 23500 && all.room.sample_ms == 801);
}

void test_transient_fault_is_published_then_cleared_between_conversions() {
    Fixture& value = fixture();
    fake_sensors::set_temperature_milli_c(0, 100000);
    fake_sensors::set_temperature_milli_c(1, 100000);
    start_conversion(value);
    fake_sensors::set_physical_fault(0, true, 0x85);
    complete_conversion(value, 170);
    const auto fault = value.readings();
    assert(!fault.exhaust.valid && fault.exhaust.status == 0x85);
    assert(fault.air.valid && fault.air.status == 0);
    // The raw SR is visible to the consumer before the next conversion attempts FAULTCLR.
    assert(fake_sensors::cr0_write_count(0, 0x17) == 0);
    assert(fake_sensors::cr0_write_count(1, 0x17) == 0);

    // Prove driver recovery does not clear the Controller's independent safety latch.
    sirokko::Settings settings{};
    settings.mode = sirokko::Mode::MANUAL;
    sirokko::Controller controller(qualified_profile(), settings, 7);
    auto good = fault;
    good.exhaust = {100000, 170, 0, true};
    good.air = {100000, 170, 0, true};
    assert(controller.tick(tick_input(170, good)).state == sirokko::State::IDLE);
    auto input = tick_input(171, good);
    input.start = true;
    assert(controller.tick(input).state == sirokko::State::PREPURGE);
    assert(controller.tick(tick_input(172, good)).state == sirokko::State::PREHEAT);
    assert(controller.tick(tick_input(173, good)).state == sirokko::State::IGNITION);
    auto elevated = good;
    elevated.exhaust.milli_c += 2000;
    elevated.air.milli_c += 2000;
    elevated.exhaust.sample_ms = 174;
    elevated.air.sample_ms = 174;
    assert(controller.tick(tick_input(174, elevated)).state == sirokko::State::VERIFY);
    assert(controller.tick(tick_input(175, elevated)).state == sirokko::State::RUN);

    // Feed the actual production driver's published fault to the active Controller.
    const auto latched = controller.tick(tick_input(420, fault));
    assert((latched.faults & sirokko::FAULT_EXHAUST) != 0U);
    assert(latched.start_block == sirokko::Block::FAULT_LATCHED);

    fake_sensors::set_physical_fault(0, false);
    value.poll(251);  // Explicit CR0=0x17 clear, then a fresh CR0=0x55 one-shot.
    const auto held_fault = value.readings();
    assert(!held_fault.exhaust.valid && held_fault.exhaust.status == 0x85);
    assert(fake_sensors::cr0_write_count(0, 0x17) == 1);
    assert(fake_sensors::cr0_write_count(1, 0x17) == 0);
    assert(fake_sensors::device(0).registers[0x00] == 0x15);  // Modelled FAULTCLR/1SHOT bits self-clear.
    complete_conversion(value, 420);
    const auto recovered = value.readings();
    assert(recovered.exhaust.valid && recovered.exhaust.status == 0 && recovered.exhaust.sample_ms == 420);
    assert(recovered.air.valid && recovered.air.status == 0);

    const auto still_latched = controller.tick(tick_input(420, recovered));
    assert((still_latched.faults & sirokko::FAULT_EXHAUST) != 0U);
    assert(still_latched.start_block == sirokko::Block::FAULT_LATCHED);
    const auto entered_fault = controller.tick(tick_input(421, recovered));
    assert(entered_fault.state == sirokko::State::FAULT &&
           (entered_fault.faults & sirokko::FAULT_EXHAUST) != 0U);
    auto reset = tick_input(422, recovered);
    reset.reset_fault = true;
    const auto reset_snapshot = controller.tick(reset);
    assert(reset_snapshot.faults == sirokko::FAULT_NONE && reset_snapshot.state == sirokko::State::IDLE);
}

void test_persistent_fault_relatches_per_channel() {
    Fixture& value = fixture();
    start_conversion(value);
    fake_sensors::set_physical_fault(0, true, 0x01);
    fake_sensors::set_fault_reassert_immediately(0, true);
    complete_conversion(value, 170);
    assert(!value.readings().exhaust.valid && value.readings().exhaust.status == 0x01);
    assert(value.readings().air.valid);

    value.poll(251);
    assert(fake_sensors::cr0_write_count(0, 0x17) == 1);
    assert(fake_sensors::cr0_write_count(1, 0x17) == 0);
    // Datasheet-permitted immediate reassertion leaves DRDY low; the driver must publish SR again.
    complete_conversion(value, 420);
    const auto repeated = value.readings();
    assert(!repeated.exhaust.valid && repeated.exhaust.status == 0x01);
    assert(repeated.air.valid && repeated.air.status == 0);

    value.poll(501);
    assert(fake_sensors::cr0_write_count(0, 0x17) == 2);
    assert(fake_sensors::cr0_write_count(1, 0x17) == 0);
}

void test_fault_clear_spi_failure_never_validates_old_sample() {
    Fixture& value = fixture();
    start_conversion(value);
    fake_sensors::set_physical_fault(0, true, 0x04);
    complete_conversion(value, 170);
    assert(!value.readings().exhaust.valid && value.readings().exhaust.status == 0x04);

    fake_sensors::set_physical_fault(0, false);
    fake_sensors::fail_next_fault_clear(0);
    value.poll(251);
    const auto after_failed_clear = value.readings();
    assert(!after_failed_clear.exhaust.valid && after_failed_clear.exhaust.status == 0x04);
    assert(fake_sensors::cr0_write_count(0, 0x17, false) == 1);
    const auto* last = fake_sensors::last_cr0_write(0);
    assert(last != nullptr && last->value == 0x17 && !last->succeeded);
    // Only the other channel begins a new conversion; the failed channel cannot publish an old clean value.
    assert(fake_sensors::cr0_write_count(0, 0x55) == 1);
    assert(fake_sensors::cr0_write_count(1, 0x55) == 2);

    value.poll(501);
    assert(fake_sensors::cr0_write_count(0, 0x17) == 1);
    assert(fake_sensors::cr0_write_count(0, 0x55) == 2);
    complete_conversion(value, 670);
    const auto recovered = value.readings();
    assert(recovered.exhaust.valid && recovered.exhaust.status == 0 && recovered.exhaust.sample_ms == 670);
}

void test_deadline_drdy_and_wrong_config_are_rejected() {
    {
        Fixture& value = fixture();
        start_conversion(value);
        value.poll(212);  // 211 ms: over the 210 ms driver deadline without a new DRDY edge.
        const auto timeout = value.readings();
        assert(!timeout.exhaust.valid && timeout.exhaust.status == 0xFC);
        assert(!timeout.air.valid && timeout.air.status == 0xFC);
    }
    {
        Fixture& value = fixture();
        fake_sensors::set_drdy_stuck_low(0, true);
        value.poll(1);
        value.poll(212);
        const auto no_edge = value.readings();
        assert(!no_edge.exhaust.valid && no_edge.exhaust.status == 0xFC);
    }
    {
        Fixture& value = fixture();
        fake_sensors::device(0).registers[0x01] = 0x04;  // Not type K, so do not start a conversion.
        value.poll(1);
        const auto rejected = value.readings();
        assert(!rejected.exhaust.valid && rejected.exhaust.status == 0xFE);
        assert(fake_sensors::cr0_write_count(0, 0x55) == 0);
        assert(fake_sensors::cr0_write_count(1, 0x55) == 1);
    }
}

void test_ds18b20_crc_rejection_remains_intact() {
    Fixture& value = fixture();
    fake_sensors::corrupt_ds18b20_crc();
    value.poll(1);
    value.poll(801);
    const auto readings = value.readings();
    assert(!readings.room.valid && readings.room.status == 0xFC);
}

void test_late_latched_fault_is_published_before_clear() {
    Fixture& value=fixture();start_conversion(value);
    fake_sensors::device(0).latched_status=0x01;
    complete_conversion(value,170);
    assert(value.readings().exhaust.status==0x01);
    fake_sensors::device(0).latched_status=0x03;
    value.poll(251);
    assert(!value.readings().exhaust.valid && value.readings().exhaust.status==0x03);
    assert(fake_sensors::cr0_write_count(0,0x17)==0);
    value.poll(420);
    assert(value.readings().exhaust.status==0x03);
    value.poll(501);
    assert(fake_sensors::cr0_write_count(0,0x17)==1);
    complete_conversion(value,670);
    assert(value.readings().exhaust.valid && value.readings().exhaust.status==0);
}

}  // namespace

int main() {
    test_late_latched_fault_is_published_before_clear();
    test_initialization_and_normal_measurements();
    test_transient_fault_is_published_then_cleared_between_conversions();
    test_persistent_fault_relatches_per_channel();
    test_fault_clear_spi_failure_never_validates_old_sample();
    test_deadline_drdy_and_wrong_config_are_rejected();
    test_ds18b20_crc_rejection_remains_intact();
    std::cout << "PASS: production MAX31856/DS18B20 driver: normal, transient/persistent faults, clear failure, "
                 "independent channels, DRDY/deadline, configuration and CRC safety\n";
}
