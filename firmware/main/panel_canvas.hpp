// SPDX-License-Identifier: MIT
// Strip-oriented, allocation-free RGB565 canvas for the 320x240 local panel.
#pragma once

#include <cstdint>

namespace panel_draw {

using Color = uint32_t; // 0xRRGGBB

struct Point {
    float x;
    float y;
};

enum class Align { Left, Center, Right };

enum class Icon {
    Clock,
    Settings,
    Warning,
    Thermometer,
    ThermometerFilled,
    Upload,
    Wifi,
    Check,
    Power,
    Stop,
    Heat,
    Fan,
    Chevron,
};

class Canvas {
  public:
    // pixels owns exactly width * rows native-endian RGB565 values. band_y is in the 320x240 view.
    void begin(uint16_t *pixels, int band_y, int rows, int width = 320);
    void clear(Color c);
    void rect(float x, float y, float w, float h, Color c, float radius = 0);
    void circle(float cx, float cy, float r, Color c);
    void line(float x1, float y1, float x2, float y2, float width, Color c);
    // SVG angles: 0 = right, 90 = down, positive clockwise. Stroke ends are round.
    void arc(float cx, float cy, float r, float start_deg, float end_deg, float width, Color c);
    void text(float x, float baseline, const char *utf8, float size, Color c, Align align = Align::Left,
              int weight = 400);
    void icon(Icon which, float cx, float cy, float size, Color c, float angle_deg = 0);
    void radial_glow(float cx, float cy, float radius, Color c, float alpha);

  private:
    void blend_local(int x, int local_y, Color c, uint8_t alpha);
    bool valid() const;

    uint16_t *pixels_{};
    int band_y_{};
    int rows_{};
    int width_{};
};

float text_width(const char *utf8, float size, int weight = 400);
uint16_t rgb565(Color);
Color from565(uint16_t);

} // namespace panel_draw
