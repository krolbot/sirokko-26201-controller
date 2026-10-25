#define main baseline_main
#include "sim_tests.cpp"
#undef main
#include "sirokko/ui.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace sirokko;


namespace {

Config profile() {
    Config config=test_config(); // Simulation only, never a measured Sirokko profile.
    return config;
}

UiFrame example_frame() {
    Sim sim(profile()); sim.reach_run_high(); sim.in.room.milli_c=20000; sim.advance(sim.config.stage_min_ms);
    UiFrame frame{};
    frame.now_ms = sim.in.now_ms;
    frame.config = sim.config;
    frame.settings = {kOperatorSchema, 20000, 1000, -300};
    frame.raw_room = {20300, frame.now_ms, SensorQuality::OK};
    frame.room = sim.in.room; frame.exhaust=sim.in.exhaust; frame.hot_air=sim.in.hot_air;
    frame.status=sim.machine.status(frame.now_ms);
    frame.counters = {14, 2, 9'000'000, 310'000};
    frame.last_event.to = State::RUN_LOW;
    frame.last_event.reason = StopReason::NONE;
    frame.boot_id = 7;
    frame.session_id = 3;
    frame.profile_token = CommissioningStore{}.candidate_token(frame.config);
    std::strcpy(frame.firmware_version,"0.1.6-DRAFT");
    frame.exhaust_fault = 0;
    frame.hot_air_fault = 0;
    frame.safety_ok = true;
    frame.settings_ok = true;
    frame.storage_ok = true;
    frame.network_configured = true;
    frame.network_connected = true;
    return frame;
}

uint16_t host_color(uint16_t wire_color) {
    return static_cast<uint16_t>((wire_color << 8) | (wire_color >> 8));
}

uint8_t expand_component(uint16_t value, uint16_t maximum) {
    return static_cast<uint8_t>((static_cast<unsigned>(value) * 255u + maximum / 2u) / maximum);
}

void write_ppm(const std::filesystem::path& path, const UiFrame& frame) {
    std::array<uint16_t, kUiWidth * kUiHeight> image{};
    std::array<uint16_t, kUiWidth * kUiBandRows> band{};
    for (uint8_t index = 0; index < kUiHeight / kUiBandRows; ++index) {
        render_ui_band(band.data(), index, frame);
        for (int row = 0; row < kUiBandRows; ++row) {
            for (int column = 0; column < kUiWidth; ++column) {
                image[(index * kUiBandRows + row) * kUiWidth + column] =
                    band[row * kUiWidth + column];
            }
        }
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot open " + path.string());
    output << "P6\n" << kUiWidth << ' ' << kUiHeight << "\n255\n";
    for (const uint16_t wire_pixel : image) {
        const uint16_t pixel = host_color(wire_pixel);
        const char rgb[3] = {
            static_cast<char>(expand_component(static_cast<uint16_t>((pixel >> 11) & 0x1f), 0x1f)),
            static_cast<char>(expand_component(static_cast<uint16_t>((pixel >> 5) & 0x3f), 0x3f)),
            static_cast<char>(expand_component(static_cast<uint16_t>(pixel & 0x1f), 0x1f)),
        };
        output.write(rgb, sizeof(rgb));
    }
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

void render_example(const std::filesystem::path& directory, std::string_view name, UiFrame frame,
                    UiPage page, uint8_t selection = 0) {
    frame.page = page;
    frame.selection = selection;
    const std::filesystem::path output = directory / (std::string(name) + ".ppm");
    write_ppm(output, frame);
    std::cout << output.string() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: render_ui <output-directory>\n";
        return 2;
    }
    try {
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const UiFrame frame = example_frame();
        render_example(directory, "home", frame, UiPage::HOME);
        render_example(directory, "settings", frame, UiPage::SETTINGS, 4);
        render_example(directory, "setpoint", frame, UiPage::SETPOINT);
        render_example(directory, "hysteresis", frame, UiPage::HYSTERESIS);
        render_example(directory, "room_offset", frame, UiPage::ROOM_OFFSET);
        render_example(directory, "diagnostics", frame, UiPage::DIAGNOSTICS);
        render_example(directory, "profile", frame, UiPage::PROFILE);
        render_example(directory, "counters", frame, UiPage::COUNTERS);
        render_example(directory, "reset_counters", frame, UiPage::RESET_COUNTERS);
        render_example(directory, "version", frame, UiPage::VERSION);
        Sim failure(profile()); failure.reach_run_high(); failure.in.hot_air.quality=SensorQuality::HARDWARE_FAULT; failure.tick();
        auto fault=frame; fault.now_ms=failure.in.now_ms; fault.status=failure.machine.status(fault.now_ms);
        fault.room=failure.in.room; fault.raw_room=fault.room; fault.raw_room.milli_c-=fault.settings.room_offset_milli_c;
        fault.exhaust=failure.in.exhaust; fault.hot_air=failure.in.hot_air; fault.hot_air_fault=0x20;
        render_example(directory, "fault", fault, UiPage::HOME);
        render_example(directory, "diagnostics_fault", fault, UiPage::DIAGNOSTICS);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "render_ui: " << error.what() << '\n';
        return 1;
    }
}
