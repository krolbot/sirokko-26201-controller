// SPDX-License-Identifier: MIT
#include "panel_canvas.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kBandRows = 16;
constexpr uint16_t kCanary = 0xA55AU;
constexpr panel_draw::Color kWhite = 0xFFFFFFU;
constexpr panel_draw::Color kInk = 0x292929U;
constexpr panel_draw::Color kOrange = 0xE58042U;
constexpr panel_draw::Color kTrack = 0xE1E1E1U;
constexpr panel_draw::Color kWarm = 0xF9BC9AU;
constexpr panel_draw::Color kBlue = 0x537F9DU;

size_t changed_pixels(const std::vector<uint16_t>& pixels, uint16_t background) {
    return static_cast<size_t>(std::count_if(pixels.begin(), pixels.end(), [background](uint16_t pixel) {
        return pixel != background;
    }));
}

void draw_probe_scene(panel_draw::Canvas& canvas) {
    canvas.clear(kWhite);
    canvas.radial_glow(160.0F, 127.0F, 148.0F, 0xF07832U, 0.15F);
    canvas.arc(160.0F, 127.0F, 112.0F, 135.0F, 405.0F, 18.5F, kTrack);
    canvas.arc(160.0F, 127.0F, 112.0F, 135.0F, 333.0F, 18.5F, kWarm);
    canvas.arc(160.0F, 127.0F, 112.0F, 315.0F, 333.0F, 18.5F, 0xF07832U);
    canvas.circle(243.1F, 84.7F, 10.5F, kWhite);
    canvas.arc(243.1F, 84.7F, 10.5F, 0.0F, 360.0F, 3.0F, 0xF07832U);
    canvas.circle(228.5F, 94.1F, 4.0F, kWhite);

    canvas.text(160.0F, 83.0F, "Нагрев", 18.0F, 0xD38557U, panel_draw::Align::Center, 500);
    canvas.text(160.0F, 147.0F, "26", 57.0F, kInk, panel_draw::Align::Center);
    canvas.text(211.0F, 120.0F, "°C", 19.0F, kInk);
    canvas.icon(panel_draw::Icon::ThermometerFilled, 120.0F, 171.0F, 17.0F, 0xDF7E45U);
    canvas.text(139.0F, 177.0F, "24,5 °C", 18.0F, 0xDF7E45U, panel_draw::Align::Left, 500);

    canvas.rect(18.0F, 198.0F, 284.0F, 27.0F, 0xFFF0E6U, 5.0F);
    canvas.icon(panel_draw::Icon::Fan, 38.0F, 211.5F, 18.0F, kOrange, 31.0F);
    canvas.text(56.0F, 216.0F, "Продувка", 15.0F, kInk, panel_draw::Align::Left, 500);
    canvas.icon(panel_draw::Icon::Chevron, 280.0F, 211.5F, 18.0F, kOrange);
}

void test_rgb565() {
    using namespace panel_draw;
    assert(rgb565(0x000000U) == 0x0000U);
    assert(rgb565(0xFFFFFFU) == 0xFFFFU);
    assert(rgb565(0xFF0000U) == 0xF800U);
    assert(rgb565(0x00FF00U) == 0x07E0U);
    assert(rgb565(0x0000FFU) == 0x001FU);
    assert(from565(0xF800U) == 0xFF0000U);
    assert(from565(0x07E0U) == 0x00FF00U);
    assert(from565(0x001FU) == 0x0000FFU);
}

void test_primitives_are_antialiased() {
    using namespace panel_draw;
    std::vector<uint16_t> pixels(static_cast<size_t>(kWidth) * kBandRows);
    Canvas canvas;
    canvas.begin(pixels.data(), 32, kBandRows);
    canvas.clear(0x000000U);
    canvas.circle(20.25F, 38.75F, 8.6F, kWhite);
    canvas.rect(35.2F, 34.3F, 21.4F, 10.7F, kWhite, 3.1F);
    canvas.line(65.1F, 33.5F, 91.8F, 45.4F, 2.3F, kWhite);
    canvas.arc(112.0F, 40.0F, 12.5F, 210.0F, 490.0F, 2.2F, kWhite);
    const uint16_t black = rgb565(0x000000U);
    const uint16_t white = rgb565(kWhite);
    bool has_antialias = false;
    for (uint16_t pixel : pixels) {
        if (pixel != black && pixel != white) {
            has_antialias = true;
            break;
        }
    }
    assert(has_antialias);
}

void test_stripe_clipping_and_canaries() {
    using namespace panel_draw;
    constexpr size_t kPixels = static_cast<size_t>(kWidth) * kBandRows;
    std::array<uint16_t, kPixels + 2U * 17U> guarded{};
    guarded.fill(kCanary);
    uint16_t* stripe = guarded.data() + 17;
    Canvas canvas;
    canvas.begin(stripe, 96, kBandRows);
    canvas.clear(kWhite);
    canvas.rect(-500.0F, -500.0F, 1000.0F, 1000.0F, 0x123456U, 999.0F);
    canvas.circle(-1000000.0F, 103.0F, 1000000.0F, 0x654321U);
    canvas.line(-1000000.0F, 0.0F, 1000000.0F, 239.0F, 3.0F, kBlue);
    canvas.arc(160.0F, 120.0F, 1000000.0F, -720.0F, 1080.0F, 2.0F, kOrange);
    canvas.text(-1000000.0F, 103.0F, "Сирокко Ёё • — →", 18.0F, kInk);
    canvas.icon(Icon::Fan, 160.0F, 103.0F, 80.0F, kOrange, 33.0F);
    canvas.radial_glow(160.0F, 103.0F, 1000000.0F, kOrange, 0.1F);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    canvas.rect(nan, 0.0F, 1.0F, 1.0F, kInk);
    canvas.circle(0.0F, infinity, 1.0F, kInk);
    canvas.line(0.0F, 0.0F, infinity, 1.0F, 1.0F, kInk);
    canvas.arc(0.0F, 0.0F, 1.0F, 0.0F, nan, 1.0F, kInk);
    canvas.icon(Icon::Clock, 1.0F, 1.0F, nan, kInk);
    canvas.radial_glow(0.0F, 0.0F, infinity, kInk, 0.5F);

    for (size_t index = 0; index < 17U; ++index) assert(guarded[index] == kCanary);
    for (size_t index = 17U + kPixels; index < guarded.size(); ++index) assert(guarded[index] == kCanary);
}

void test_bands_match_full_image() {
    using namespace panel_draw;
    std::vector<uint16_t> complete(static_cast<size_t>(kWidth) * kHeight);
    std::vector<uint16_t> striped(static_cast<size_t>(kWidth) * kHeight);
    Canvas full;
    full.begin(complete.data(), 0, kHeight);
    draw_probe_scene(full);
    for (int band_y = 0; band_y < kHeight; band_y += kBandRows) {
        Canvas band;
        band.begin(striped.data() + static_cast<size_t>(band_y) * kWidth, band_y, kBandRows);
        draw_probe_scene(band);
    }
    assert(complete == striped);
}

void test_utf8_metrics_and_baseline() {
    using namespace panel_draw;
    const float cyrillic = text_width("Сирокко Ёё °C • — →", 18.0F, 400);
    assert(cyrillic > text_width(" ", 18.0F, 400));
    assert(text_width(nullptr, 18.0F) == 0.0F);
    assert(std::abs(text_width("26", 13.90F) - text_width("26", 14.0F)) < 0.001F);
    assert(std::abs(text_width("26", 14.53F) - text_width("26", 15.0F)) < 0.001F);
    assert(std::abs(text_width("26", 44.03F) - text_width("26", 44.0F)) < 0.001F);

    std::vector<uint16_t> pixels(static_cast<size_t>(kWidth) * kHeight);
    Canvas canvas;
    canvas.begin(pixels.data(), 0, kHeight);
    canvas.clear(kWhite);
    canvas.text(10.0F, 80.0F, "Аg Ёё", 20.0F, kInk, Align::Left, 500);
    const char malformed[] = {static_cast<char>(0xF0), static_cast<char>(0x80), '\0'};
    canvas.text(10.0F, 120.0F, malformed, 20.0F, kInk);
    canvas.text(10.0F, 145.0F, "Wi-Fi: S!r0kko_#@/\\", 14.0F, kInk);
    // 57 px is deliberately numeric/subset-only; a missing large Cyrillic glyph
    // must still use the generated visible '?' fallback rather than read invalid data.
    canvas.text(260.0F, 205.0F, "Ж", 57.0F, kInk, Align::Center);

    int minimum_y = kHeight;
    int maximum_y = -1;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (pixels[static_cast<size_t>(y) * kWidth + x] != rgb565(kWhite)) {
                minimum_y = std::min(minimum_y, y);
                maximum_y = std::max(maximum_y, y);
            }
        }
    }
    assert(minimum_y < 80);
    assert(maximum_y >= 80);  // descender of g is below the supplied baseline
    assert(changed_pixels(pixels, rgb565(kWhite)) > 0U);
}

void test_icons_and_rotation() {
    using namespace panel_draw;
    constexpr std::array<Icon, 13> kIcons = {
        Icon::Clock, Icon::Settings, Icon::Warning, Icon::Thermometer, Icon::ThermometerFilled,
        Icon::Upload, Icon::Wifi, Icon::Check, Icon::Power, Icon::Stop, Icon::Heat, Icon::Fan, Icon::Chevron,
    };
    for (Icon icon : kIcons) {
        std::vector<uint16_t> pixels(static_cast<size_t>(kWidth) * kHeight);
        Canvas canvas;
        canvas.begin(pixels.data(), 0, kHeight);
        canvas.clear(kWhite);
        canvas.icon(icon, 160.0F, 120.0F, 44.0F, kBlue);
        assert(changed_pixels(pixels, rgb565(kWhite)) > 0U);
    }

    std::vector<uint16_t> unrotated(static_cast<size_t>(kWidth) * kHeight);
    std::vector<uint16_t> rotated(static_cast<size_t>(kWidth) * kHeight);
    Canvas first;
    Canvas second;
    first.begin(unrotated.data(), 0, kHeight);
    second.begin(rotated.data(), 0, kHeight);
    first.clear(kWhite);
    second.clear(kWhite);
    first.icon(Icon::Fan, 160.0F, 120.0F, 48.0F, kBlue, 0.0F);
    second.icon(Icon::Fan, 160.0F, 120.0F, 48.0F, kBlue, 33.0F);
    assert(unrotated != rotated);
}

void write_probe_ppm(const char* path) {
    using namespace panel_draw;
    std::vector<uint16_t> pixels(static_cast<size_t>(kWidth) * kHeight);
    Canvas canvas;
    canvas.begin(pixels.data(), 0, kHeight);
    draw_probe_scene(canvas);
    std::ofstream output(path, std::ios::binary);
    assert(output.good());
    output << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    for (uint16_t pixel : pixels) {
        const Color color = from565(pixel);
        const std::array<char, 3> rgb = {
            static_cast<char>((color >> 16U) & 0xFFU),
            static_cast<char>((color >> 8U) & 0xFFU),
            static_cast<char>(color & 0xFFU),
        };
        output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
    assert(output.good());
}

}  // namespace

int main(int argc, char** argv) {
    test_rgb565();
    test_primitives_are_antialiased();
    test_stripe_clipping_and_canaries();
    test_bands_match_full_image();
    test_utf8_metrics_and_baseline();
    test_icons_and_rotation();
    if (argc == 3 && std::strcmp(argv[1], "--ppm") == 0) write_probe_ppm(argv[2]);
    else assert(argc == 1);
    std::cout << "PASS: panel canvas strip clipping, AA, Roboto UTF-8, icons and RGB565\n";
}
