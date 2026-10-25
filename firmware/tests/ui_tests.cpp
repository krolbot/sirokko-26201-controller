#include "sirokko/ui.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sirokko;


namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Config config() {
    Config result{};
    result.setpoint_min_milli_c = 5000;
    result.setpoint_max_milli_c = 35000;
    result.setpoint_default_milli_c = 20000;
    result.sensor_stale_ms = 2000;
    result.pwm_frequency_hz = 10000;
    result.low_pwm_permille = 420;
    result.recovery_purge_pwm_permille = 650;
    result.commissioning_accepted = true;
    return result;
}

UiKeys up_press() {
    UiKeys keys{};
    keys.up_press = true;
    return keys;
}

UiKeys down_press() {
    UiKeys keys{};
    keys.down_press = true;
    return keys;
}

UiKeys chord() {
    UiKeys keys{};
    keys.up_held = true;
    keys.down_held = true;
    return keys;
}

UiKeys stop() {
    UiKeys keys{};
    keys.stop = true;
    return keys;
}

void enter_settings(LocalMenu& menu, OperatorSettings& settings, const Config& profile,
                    uint32_t start_ms = 0) {
    menu.tick({}, start_ms, settings, profile);  // boot release arms the local chord.
    menu.tick(chord(), start_ms + 10, settings, profile);
    check(menu.page() == UiPage::HOME, "chord must wait 800 ms");
    menu.tick(chord(), start_ms + 810, settings, profile);
    check(menu.page() == UiPage::SETTINGS, "fresh 800 ms chord must open settings");
    menu.tick({}, start_ms + 811, settings, profile);
}

void open_selected_page(LocalMenu& menu, OperatorSettings& settings, const Config& profile,
                        uint32_t start_ms) {
    menu.tick(chord(), start_ms, settings, profile);
    menu.tick(chord(), start_ms + 800, settings, profile);
    menu.tick({}, start_ms + 801, settings, profile);
}

void test_home_and_edit_bounds() {
    Config profile = config();
    OperatorSettings settings{};
    settings.setpoint_milli_c = 20000;
    settings.hysteresis_milli_c = 2000;
    LocalMenu menu;

    menu.tick({}, 0, settings, profile);
    UiAction action = menu.tick(up_press(), 1, settings, profile);
    check(action.settings_changed && settings.setpoint_milli_c == 20500,
          "HOME UP must raise the setpoint by 500 mC");

    settings.setpoint_milli_c = profile.setpoint_max_milli_c;
    action = menu.tick(up_press(), 2, settings, profile);
    check(!action.settings_changed && settings.setpoint_milli_c == profile.setpoint_max_milli_c,
          "setpoint must clamp at Config maximum");

    enter_settings(menu, settings, profile, 10);
    menu.tick(down_press(), 900, settings, profile);
    check(menu.selection() == 1, "SETTINGS DOWN must select the following item");
    open_selected_page(menu, settings, profile, 1000);
    check(menu.page() == UiPage::HYSTERESIS, "SETTINGS chord must open selected editor");

    action = menu.tick(up_press(), 1900, settings, profile);
    check(action.settings_changed && settings.hysteresis_milli_c == 2500,
          "hysteresis editor must change live in 500 mC steps");
    settings.hysteresis_milli_c = kHysteresisMaxMilliC;
    action = menu.tick(up_press(), 1901, settings, profile);
    check(!action.settings_changed, "hysteresis must clamp at its safety maximum");

    menu.tick(chord(), 2000, settings, profile);
    menu.tick(chord(), 2800, settings, profile);
    check(menu.page() == UiPage::SETTINGS, "editor chord must return to SETTINGS");
}

void test_chord_latching_boot_and_timeout() {
    Config profile = config();
    OperatorSettings settings{};
    LocalMenu menu;

    menu.tick(chord(), 0, settings, profile);
    menu.tick(chord(), 2000, settings, profile);
    check(menu.page() == UiPage::HOME, "boot-held chord must be ignored until both keys release");
    menu.tick({}, 2001, settings, profile);
    menu.tick(chord(), 2002, settings, profile);
    menu.tick(chord(), 2802, settings, profile);
    check(menu.page() == UiPage::SETTINGS, "released then fresh chord must be accepted");
    menu.tick({}, 2803, settings, profile);

    const uint8_t selection = menu.selection();
    menu.tick(up_press(), 2804, settings, profile);
    check(menu.selection() != selection, "released chord permits normal single-key navigation");
    menu.tick({}, 32804, settings, profile);
    check(menu.page() == UiPage::HOME, "30 second inactivity must return HOME");

    LocalMenu held;
    enter_settings(held, settings, profile, 40'000);
    held.tick(chord(), 41'000, settings, profile);
    held.tick(chord(), 41'800, settings, profile);
    check(held.page() == UiPage::SETPOINT, "settings chord must enter an editor before hold timeout");
    held.tick(chord(), 71'800, settings, profile);
    check(held.page() == UiPage::HOME, "a consumed held chord must not defeat the 30 second timeout");
}

void test_counter_reset_requires_fresh_long_chord() {
    Config profile = config();
    OperatorSettings settings{};
    LocalMenu menu;
    enter_settings(menu, settings, profile);

    for (uint32_t i = 0; i < 5; ++i) menu.tick(down_press(), 900 + i, settings, profile);
    check(menu.selection() == 5, "counter page must be selectable");
    open_selected_page(menu, settings, profile, 1000);
    check(menu.page() == UiPage::COUNTERS, "selected COUNTERS page must open");

    menu.tick(chord(), 1900, settings, profile);
    menu.tick(chord(), 2700, settings, profile);
    check(menu.page() == UiPage::RESET_COUNTERS, "COUNTERS chord must open reset confirmation");
    menu.tick(chord(), 4700, settings, profile);
    check(menu.page() == UiPage::RESET_COUNTERS,
          "entry chord must not count as the fresh confirmation chord");
    menu.tick({}, 4701, settings, profile);

    menu.tick(chord(), 4800, settings, profile);
    UiAction action = menu.tick(chord(), 6799, settings, profile);
    check(!action.reset_counters, "counter reset must wait for full 2000 ms");
    action = menu.tick(chord(), 6800, settings, profile);
    check(action.reset_counters && menu.page() == UiPage::COUNTERS,
          "fresh 2000 ms confirmation chord must emit one reset action");
    action = menu.tick(chord(), 6801, settings, profile);
    check(!action.reset_counters, "held confirmation chord must not repeat reset action");

    menu.tick({}, 6802, settings, profile);
    menu.tick(chord(), 6900, settings, profile);
    menu.tick(chord(), 7700, settings, profile);
    check(menu.page() == UiPage::RESET_COUNTERS, "second reset confirmation must be reachable");
    menu.tick(stop(), 7701, settings, profile);
    check(menu.page() == UiPage::HOME, "STOP must cancel reset confirmation and return HOME");
}

uint16_t wire(uint16_t rgb565) {
    return static_cast<uint16_t>((rgb565 << 8) | (rgb565 >> 8));
}

UiFrame frame() {
    UiFrame result{};
    result.now_ms = 50'000;
    result.config = config();
    result.settings = {kOperatorSchema, 21000, 2000, -300};
    result.raw_room = {20400, 49'500, SensorQuality::OK};
    result.room = {20100, 49'500, SensorQuality::OK};
    result.exhaust = {183400, 49'000, SensorQuality::OK};
    result.hot_air = {64200, 48'000, SensorQuality::CRC_ERROR};
    result.status.state = State::RUN_LOW;
    result.status.faults = FAULT_NONE;
    result.status.commissioning_ready = true;
    result.status.outputs = {false, true, true, true, true, false, true, 420};
    result.counters = {14, 2, 9'000'000, 310'000};
    result.last_event.to = State::RUN_LOW;
    result.last_event.reason = StopReason::THERMOSTAT;
    result.boot_id = 7;
    result.session_id = 3;
    result.profile_token = 0xa1b2c3d4;
    result.exhaust_fault = 0x04;
    result.hot_air_fault = 0x20;
    result.safety_ok = true;
    result.settings_ok = true;
    result.storage_ok = true;
    result.network_configured = true;
    result.network_connected = true;
    return result;
}

bool has_non_background(const std::array<uint16_t, kUiWidth * kUiBandRows>& pixels,
                        uint16_t background, int first_row, int last_row) {
    const uint16_t expected = wire(background);
    for (int y = first_row; y <= last_row; ++y) {
        for (int x = 0; x < kUiWidth; ++x) {
            if (pixels[y * kUiWidth + x] != expected) return true;
        }
    }
    return false;
}

void test_renderer_wire_format_and_pages() {
    UiFrame ui = frame();
    std::array<uint16_t, kUiWidth * kUiBandRows> pixels{};
    pixels.fill(0xaaaa);
    render_ui_band(pixels.data(), 0, ui);
    check(pixels.back() == wire(0x0010), "HOME title band must use big-endian RGB565 dark blue");
    check(has_non_background(pixels, 0x0010, 3, 16), "first 2x text row must paint glyph pixels");
    check(has_non_background(pixels, 0x0010, 22, 35), "second 2x text row must paint glyph pixels");

    for (UiPage page : {UiPage::SETTINGS, UiPage::SETPOINT, UiPage::HYSTERESIS,
                        UiPage::ROOM_OFFSET, UiPage::DIAGNOSTICS, UiPage::PROFILE,
                        UiPage::COUNTERS, UiPage::RESET_COUNTERS, UiPage::VERSION}) {
        ui.page = page;
        for (uint8_t band = 0; band < 8; ++band) {
            pixels.fill(0xaaaa);
            render_ui_band(pixels.data(), band, ui);
            check(pixels[0] != 0xaaaa || pixels.back() != 0xaaaa,
                  "every valid band must be cleared by the same renderer");
        }
    }

    pixels.fill(0xaaaa);
    render_ui_band(pixels.data(), 8, ui);
    check(pixels.front() == 0xaaaa && pixels.back() == 0xaaaa,
          "out-of-range bands must not write the caller buffer");
    check(std::string(ui_state_label(State::RUN_LOW)) == "РАБОТА НИЗКАЯ",
          "state labels must be Russian UI labels");
    check(std::string(ui_fault_label(FAULT_OUTPUT)) == "ВЫХОДЫ",
          "output fault needs a UI label");
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"home_and_edit_bounds", test_home_and_edit_bounds},
        {"chord_latching_boot_and_timeout", test_chord_latching_boot_and_timeout},
        {"counter_reset_requires_fresh_long_chord", test_counter_reset_requires_fresh_long_chord},
        {"renderer_wire_format_and_pages", test_renderer_wire_format_and_pages},
    };

    size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << "RESULT " << passed << "/" << std::size(tests) << " passed\n";
    return passed == std::size(tests) ? 0 : 1;
}
