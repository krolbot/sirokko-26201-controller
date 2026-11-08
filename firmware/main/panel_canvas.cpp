// SPDX-License-Identifier: MIT
#include "panel_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace panel_draw {
namespace {

constexpr int kViewHeight = 240;
constexpr int kMaximumWidth = 320;
constexpr int kMaximumRows = 240;
constexpr int kMaximumTextCodepoints = 512;
constexpr float kPi = 3.1415926535897932384626433832795F;
constexpr float kDegreesToRadians = kPi / 180.0F;

struct FontGlyph {
    uint32_t codepoint;
    uint32_t bitmap_offset;
    uint16_t bitmap_bytes;
    int16_t advance_64;
    int16_t bearing_x;
    int16_t bearing_y;
    uint8_t width;
    uint8_t height;
};

struct FontFace {
    uint8_t pixel_size;
    uint16_t weight;
    int16_t ascent;
    int16_t descent;
    const FontGlyph* glyphs;
    uint16_t glyph_count;
    const uint8_t* bitmap;
    uint32_t bitmap_count;
    uint16_t fallback_index;
};

#include "panel_fonts_data.inc"

struct IconMask {
    const uint8_t* pixels;
    uint16_t width;
    uint16_t height;
};

#include "panel_icons_data.inc"

static_assert(font_data::kFaceCount > 0U, "generated Roboto data is required");
static_assert(sizeof(icon_data::kIcons) / sizeof(icon_data::kIcons[0]) ==
                  static_cast<size_t>(Icon::Chevron) + 1U,
              "generated icon order must match panel_draw::Icon");

bool finite(float value) {
    return std::isfinite(static_cast<float>(value));
}

struct Bounds { int x0, x1, y0, y1; };
Bounds clipped(double left,double top,double right,double bottom,int band,int rows,int width) {
    // Clamp in double before conversion; even extreme caller coordinates cannot overflow an int.
    return {
        static_cast<int>(std::floor(std::clamp(left-1.0,0.0,static_cast<double>(width)))),
        static_cast<int>(std::ceil(std::clamp(right+1.0,0.0,static_cast<double>(width)))),
        static_cast<int>(std::floor(std::clamp(top-band-1.0,0.0,static_cast<double>(rows)))),
        static_cast<int>(std::ceil(std::clamp(bottom-band+1.0,0.0,static_cast<double>(rows)))),
    };
}

bool valid_color_alpha(uint8_t alpha) {
    return alpha != 0U;
}

uint8_t coverage_from_distance(float signed_distance) {
    const float coverage = std::clamp(0.5F - signed_distance, 0.0F, 1.0F);
    return static_cast<uint8_t>(std::lround(coverage * 255.0F));
}

float normalize_degrees(float degrees) {
    float normalized = std::fmod(degrees, 360.0F);
    if (normalized < 0.0F) normalized += 360.0F;
    return normalized;
}

const FontFace* select_face(float size, int weight) {
    if (!finite(size) || size <= 0.0F) return nullptr;
    const uint16_t requested_weight = weight >= 450 ? 500U : 400U;
    const FontFace* selected = nullptr;
    float best_distance = std::numeric_limits<float>::infinity();
    for (uint16_t index = 0; index < font_data::kFaceCount; ++index) {
        const FontFace& candidate = font_data::kFaces[index];
        if (candidate.weight != requested_weight) continue;
        const float distance = std::abs(static_cast<float>(candidate.pixel_size) - size);
        if (distance < best_distance) {
            best_distance = distance;
            selected = &candidate;
        }
    }
    return selected;
}

const FontGlyph& fallback_glyph(const FontFace& face) {
    if (face.fallback_index < face.glyph_count) return face.glyphs[face.fallback_index];
    return face.glyphs[0];
}

const FontGlyph& glyph_for(const FontFace& face, uint32_t codepoint) {
    uint16_t low = 0;
    uint16_t high = face.glyph_count;
    while (low < high) {
        const uint16_t middle = static_cast<uint16_t>(low + (high - low) / 2U);
        const uint32_t current = face.glyphs[middle].codepoint;
        if (current == codepoint) return face.glyphs[middle];
        if (current < codepoint) low = static_cast<uint16_t>(middle + 1U);
        else high = middle;
    }
    return fallback_glyph(face);
}

bool continuation(unsigned char value) {
    return (value & 0xC0U) == 0x80U;
}

// C strings are NUL-terminated.  Each continuation byte is checked only after the
// preceding byte is known non-NUL, so malformed tails never require an over-read.
const char* next_utf8(const char* text, uint32_t* codepoint) {
    if (!text || !codepoint) return text;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    const unsigned char first = bytes[0];
    if (first < 0x80U) {
        *codepoint = first;
        return text + 1;
    }
    if (first >= 0xC2U && first <= 0xDFU && bytes[1] != 0U && continuation(bytes[1])) {
        *codepoint = (static_cast<uint32_t>(first & 0x1FU) << 6U) | (bytes[1] & 0x3FU);
        return text + 2;
    }
    if (first >= 0xE0U && first <= 0xEFU && bytes[1] != 0U && bytes[2] != 0U &&
        continuation(bytes[1]) && continuation(bytes[2]) &&
        !(first == 0xE0U && bytes[1] < 0xA0U) && !(first == 0xEDU && bytes[1] > 0x9FU)) {
        *codepoint = (static_cast<uint32_t>(first & 0x0FU) << 12U) |
                     (static_cast<uint32_t>(bytes[1] & 0x3FU) << 6U) | (bytes[2] & 0x3FU);
        return text + 3;
    }
    if (first >= 0xF0U && first <= 0xF4U && bytes[1] != 0U && bytes[2] != 0U && bytes[3] != 0U &&
        continuation(bytes[1]) && continuation(bytes[2]) && continuation(bytes[3]) &&
        !(first == 0xF0U && bytes[1] < 0x90U) && !(first == 0xF4U && bytes[1] > 0x8FU)) {
        *codepoint = (static_cast<uint32_t>(first & 0x07U) << 18U) |
                     (static_cast<uint32_t>(bytes[1] & 0x3FU) << 12U) |
                     (static_cast<uint32_t>(bytes[2] & 0x3FU) << 6U) | (bytes[3] & 0x3FU);
        return text + 4;
    }
    *codepoint = static_cast<uint32_t>('?');
    return text + 1;
}

float measured_width(const FontFace& face, const char* utf8) {
    if (!utf8) return 0.0F;
    const char* cursor = utf8;
    float width = 0.0F;
    for (int index = 0; *cursor != '\0' && index < kMaximumTextCodepoints; ++index) {
        uint32_t codepoint{};
        cursor = next_utf8(cursor, &codepoint);
        width += static_cast<float>(glyph_for(face, codepoint).advance_64) / 64.0F;
    }
    return width;
}

bool floor_to_int(double value, int* result) {
    if (!result || !std::isfinite(value)) return false;
    const double floored = std::floor(value);
    if (floored < static_cast<double>(std::numeric_limits<int>::min()) ||
        floored > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    *result = static_cast<int>(floored);
    return true;
}

uint8_t glyph_alpha(const FontFace& face, const FontGlyph& glyph, int pixel_index) {
    if (pixel_index < 0) return 0U;
    const uint32_t byte_index = glyph.bitmap_offset + static_cast<uint32_t>(pixel_index / 2);
    if (byte_index >= face.bitmap_count || static_cast<uint32_t>(pixel_index / 2) >= glyph.bitmap_bytes) return 0U;
    const uint8_t packed = face.bitmap[byte_index];
    const uint8_t nibble = (pixel_index & 1) == 0 ? static_cast<uint8_t>(packed >> 4U)
                                                   : static_cast<uint8_t>(packed & 0x0FU);
    return static_cast<uint8_t>(nibble * 17U);
}

uint8_t icon_alpha_at(const IconMask& icon, int x, int y) {
    if (x < 0 || y < 0 || x >= static_cast<int>(icon.width) || y >= static_cast<int>(icon.height)) return 0U;
    const size_t index = static_cast<size_t>(y) * icon.width + static_cast<uint16_t>(x);
    const uint8_t packed = icon.pixels[index / 2U];
    const uint8_t nibble = (index & 1U) == 0U ? static_cast<uint8_t>(packed >> 4U)
                                              : static_cast<uint8_t>(packed & 0x0FU);
    return static_cast<uint8_t>(nibble * 17U);
}

uint8_t sampled_icon_alpha(const IconMask& icon, float local_x, float local_y) {
    // local coordinates use the original 24x24 SVG viewBox.  Offset by half a
    // source pixel so bilinear sampling is centered on rasterized mask pixels.
    const float source_x = local_x * static_cast<float>(icon.width) / 24.0F - 0.5F;
    const float source_y = local_y * static_cast<float>(icon.height) / 24.0F - 0.5F;
    if (!std::isfinite(source_x) || !std::isfinite(source_y) ||
        source_x < -1.0F || source_y < -1.0F || source_x > icon.width || source_y > icon.height) return 0U;
    const float x_floor=std::floor(source_x),y_floor=std::floor(source_y);
    const int x0 = static_cast<int>(x_floor);
    const int y0 = static_cast<int>(y_floor);
    const float fx = source_x - x_floor;
    const float fy = source_y - y_floor;
    const float top = static_cast<float>(icon_alpha_at(icon, x0, y0)) * (1.0F - fx) +
                       static_cast<float>(icon_alpha_at(icon, x0 + 1, y0)) * fx;
    const float bottom = static_cast<float>(icon_alpha_at(icon, x0, y0 + 1)) * (1.0F - fx) +
                          static_cast<float>(icon_alpha_at(icon, x0 + 1, y0 + 1)) * fx;
    return static_cast<uint8_t>(std::lround(std::clamp(top * (1.0F - fy) + bottom * fy, 0.0F, 255.0F)));
}

}  // namespace

uint16_t rgb565(Color color) {
    const uint32_t red = (color >> 16U) & 0xFFU;
    const uint32_t green = (color >> 8U) & 0xFFU;
    const uint32_t blue = color & 0xFFU;
    const uint16_t r5 = static_cast<uint16_t>((red * 31U + 127U) / 255U);
    const uint16_t g6 = static_cast<uint16_t>((green * 63U + 127U) / 255U);
    const uint16_t b5 = static_cast<uint16_t>((blue * 31U + 127U) / 255U);
    return static_cast<uint16_t>((r5 << 11U) | (g6 << 5U) | b5);
}

Color from565(uint16_t color) {
    const uint32_t red = ((static_cast<uint32_t>(color >> 11U) & 0x1FU) * 255U + 15U) / 31U;
    const uint32_t green = ((static_cast<uint32_t>(color >> 5U) & 0x3FU) * 255U + 31U) / 63U;
    const uint32_t blue = ((static_cast<uint32_t>(color) & 0x1FU) * 255U + 15U) / 31U;
    return (red << 16U) | (green << 8U) | blue;
}

void Canvas::begin(uint16_t* pixels, int band_y, int rows, int width) {
    pixels_ = nullptr;
    band_y_ = 0;
    rows_ = 0;
    width_ = 0;
    // The local panel has a fixed 320x240 logical surface.  Rejecting impossible
    // band dimensions prevents a malformed caller from turning stripe loops into a
    // giant full-frame walk or an overflowing pixel count.
    if (!pixels || width <= 0 || width > kMaximumWidth || rows <= 0 || rows > kMaximumRows) return;
    pixels_ = pixels;
    band_y_ = band_y;
    rows_ = rows;
    width_ = width;
}

bool Canvas::valid() const {
    return pixels_ && width_ > 0 && rows_ > 0;
}

void Canvas::blend_local(int x, int local_y, Color color, uint8_t alpha) {
    if (!valid() || !valid_color_alpha(alpha) || x < 0 || x >= width_ || local_y < 0 || local_y >= rows_) return;
    const int64_t global_y = static_cast<int64_t>(band_y_) + local_y;
    if (global_y < 0 || global_y >= kViewHeight) return;
    uint16_t& destination = pixels_[static_cast<size_t>(local_y) * static_cast<size_t>(width_) +
                                   static_cast<size_t>(x)];
    if (alpha == 255U) {
        destination = rgb565(color);
        return;
    }
    const Color existing = from565(destination);
    const uint32_t inverse = 255U - alpha;
    const uint32_t red = ((((color >> 16U) & 0xFFU) * alpha) + (((existing >> 16U) & 0xFFU) * inverse) + 127U) / 255U;
    const uint32_t green = ((((color >> 8U) & 0xFFU) * alpha) + (((existing >> 8U) & 0xFFU) * inverse) + 127U) / 255U;
    const uint32_t blue = (((color & 0xFFU) * alpha) + ((existing & 0xFFU) * inverse + 127U)) / 255U;
    destination = rgb565((red << 16U) | (green << 8U) | blue);
}

void Canvas::clear(Color color) {
    if (!valid()) return;
    const uint16_t packed = rgb565(color);
    const size_t count = static_cast<size_t>(width_) * static_cast<size_t>(rows_);
    for (size_t index = 0; index < count; ++index) pixels_[index] = packed;
}

void Canvas::rect(float x, float y, float w, float h, Color color, float radius) {
    if (!valid() || !finite(x) || !finite(y) || !finite(w) || !finite(h) || !finite(radius) ||
        w <= 0.0F || h <= 0.0F) {
        return;
    }
    const float left = x;
    const float top = y;
    const float half_w = static_cast<float>(w) * 0.5F;
    const float half_h = static_cast<float>(h) * 0.5F;
    const float center_x = left + half_w;
    const float center_y = top + half_h;
    const float corner = std::clamp(static_cast<float>(radius), 0.0F, std::min(half_w, half_h));
    const auto bounds=clipped(left,top,static_cast<double>(left)+w,static_cast<double>(top)+h,band_y_,rows_,width_);
    for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
        const float pixel_y = static_cast<float>(band_y_) + static_cast<float>(local_y) + 0.5F;
        for (int pixel_x = bounds.x0; pixel_x < bounds.x1; ++pixel_x) {
            const float qx = std::abs(static_cast<float>(pixel_x) + 0.5F - center_x) - (half_w - corner);
            const float qy = std::abs(pixel_y - center_y) - (half_h - corner);
            const float outside = std::hypot(std::max(qx, 0.0F), std::max(qy, 0.0F));
            const float inside = std::min(std::max(qx, qy), 0.0F);
            blend_local(pixel_x, local_y, color, coverage_from_distance(outside + inside - corner));
        }
    }
}

void Canvas::circle(float cx, float cy, float radius, Color color) {
    if (!valid() || !finite(cx) || !finite(cy) || !finite(radius) || radius <= 0.0F) return;
    const float center_x = cx;
    const float center_y = cy;
    const float r = radius;
    const auto bounds=clipped(static_cast<double>(cx)-radius,static_cast<double>(cy)-radius,static_cast<double>(cx)+radius,static_cast<double>(cy)+radius,band_y_,rows_,width_);
    for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
        const float pixel_y = static_cast<float>(band_y_) + static_cast<float>(local_y) + 0.5F;
        for (int pixel_x = bounds.x0; pixel_x < bounds.x1; ++pixel_x) {
            const float distance = std::hypot(static_cast<float>(pixel_x) + 0.5F - center_x, pixel_y - center_y) - r;
            blend_local(pixel_x, local_y, color, coverage_from_distance(distance));
        }
    }
}

void Canvas::line(float x1, float y1, float x2, float y2, float width, Color color) {
    if (!valid() || !finite(x1) || !finite(y1) || !finite(x2) || !finite(y2) || !finite(width) || width <= 0.0F) {
        return;
    }
    const float start_x = x1;
    const float start_y = y1;
    const float delta_x = static_cast<float>(x2) - start_x;
    const float delta_y = static_cast<float>(y2) - start_y;
    const float length_squared = delta_x * delta_x + delta_y * delta_y;
    const float half_width = static_cast<float>(width) * 0.5F;
    const auto bounds=clipped(std::min(x1,x2)-static_cast<double>(half_width),std::min(y1,y2)-static_cast<double>(half_width),std::max(x1,x2)+static_cast<double>(half_width),std::max(y1,y2)+static_cast<double>(half_width),band_y_,rows_,width_);
    for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
        const float pixel_y = static_cast<float>(band_y_) + static_cast<float>(local_y) + 0.5F;
        for (int pixel_x = bounds.x0; pixel_x < bounds.x1; ++pixel_x) {
            const float pixel_center_x = static_cast<float>(pixel_x) + 0.5F;
            float closest_x = start_x;
            float closest_y = start_y;
            if (length_squared > 0.0F && std::isfinite(length_squared)) {
                const float projection = ((pixel_center_x - start_x) * delta_x + (pixel_y - start_y) * delta_y) /
                                          length_squared;
                const float position = std::clamp(projection, 0.0F, 1.0F);
                closest_x += position * delta_x;
                closest_y += position * delta_y;
            }
            const float distance = std::hypot(pixel_center_x - closest_x, pixel_y - closest_y) - half_width;
            blend_local(pixel_x, local_y, color, coverage_from_distance(distance));
        }
    }
}

void Canvas::arc(float cx, float cy, float radius, float start_deg, float end_deg, float width, Color color) {
    if (!valid() || !finite(cx) || !finite(cy) || !finite(radius) || !finite(start_deg) || !finite(end_deg) ||
        !finite(width) || radius <= 0.0F || width <= 0.0F) {
        return;
    }
    const float raw_sweep = static_cast<float>(end_deg) - static_cast<float>(start_deg);
    if (std::abs(raw_sweep) < 0.0001F) return;
    const bool full_circle = std::abs(raw_sweep) >= 359.999F;
    float sweep = 360.0F;
    if (!full_circle) {
        sweep = std::fmod(raw_sweep, 360.0F);
        if (sweep < 0.0F) sweep += 360.0F;
        if (sweep < 0.0001F) return;
    }
    const float start = normalize_degrees(start_deg);
    const float end_radians = (start + sweep) * kDegreesToRadians;
    const float center_x = cx;
    const float center_y = cy;
    const float r = radius;
    const float endpoint_start_x = center_x + r * std::cos(start * kDegreesToRadians);
    const float endpoint_start_y = center_y + r * std::sin(start * kDegreesToRadians);
    const float endpoint_end_x = center_x + r * std::cos(end_radians);
    const float endpoint_end_y = center_y + r * std::sin(end_radians);
    const float half_width = static_cast<float>(width) * 0.5F;
    const auto bounds=clipped(static_cast<double>(cx)-radius-half_width,static_cast<double>(cy)-radius-half_width,static_cast<double>(cx)+radius+half_width,static_cast<double>(cy)+radius+half_width,band_y_,rows_,width_);
    for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
        const float pixel_y = static_cast<float>(band_y_) + static_cast<float>(local_y) + 0.5F;
        for (int pixel_x = bounds.x0; pixel_x < bounds.x1; ++pixel_x) {
            const float pixel_x_center = static_cast<float>(pixel_x) + 0.5F;
            const float dx = pixel_x_center - center_x;
            const float dy = pixel_y - center_y;
            const float radius_squared=dx*dx+dy*dy;
            const float inner=std::max(0.0F,r-half_width-1.0F),outer=r+half_width+1.0F;
            if(radius_squared<inner*inner||radius_squared>outer*outer)continue;
            const float radial = std::sqrt(radius_squared);
            float distance{};
            if (full_circle) {
                distance = std::abs(radial - r);
            } else {
                float angle = std::atan2(dy, dx) / kDegreesToRadians;
                if (angle < 0.0F) angle += 360.0F;
                const float relative = normalize_degrees(angle - start);
                if (relative <= sweep) {
                    distance = std::abs(radial - r);
                } else {
                    distance = std::min(std::hypot(pixel_x_center - endpoint_start_x, pixel_y - endpoint_start_y),
                                        std::hypot(pixel_x_center - endpoint_end_x, pixel_y - endpoint_end_y));
                }
            }
            blend_local(pixel_x, local_y, color, coverage_from_distance(distance - half_width));
        }
    }
}

void Canvas::radial_glow(float cx, float cy, float radius, Color color, float alpha) {
    if (!valid() || !finite(cx) || !finite(cy) || !finite(radius) || !finite(alpha) ||
        radius <= 0.0F || alpha <= 0.0F) {
        return;
    }
    const float center_x = cx;
    const float center_y = cy;
    const float r = radius;
    const float maximum_alpha = std::clamp(static_cast<float>(alpha), 0.0F, 1.0F);
    const auto bounds=clipped(static_cast<double>(cx)-radius,static_cast<double>(cy)-radius,static_cast<double>(cx)+radius,static_cast<double>(cy)+radius,band_y_,rows_,width_);
    for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
        const float pixel_y = static_cast<float>(band_y_) + static_cast<float>(local_y) + 0.5F;
        for (int pixel_x = bounds.x0; pixel_x < bounds.x1; ++pixel_x) {
            const float distance = std::hypot(static_cast<float>(pixel_x) + 0.5F - center_x, pixel_y - center_y);
            const float falloff = std::clamp(1.0F - distance / r, 0.0F, 1.0F);
            const uint8_t coverage = static_cast<uint8_t>(std::lround(falloff * maximum_alpha * 255.0F));
            blend_local(pixel_x, local_y, color, coverage);
        }
    }
}

void Canvas::text(float x, float baseline, const char* utf8, float size, Color color, Align align, int weight) {
    if (!valid() || !utf8 || !finite(x) || !finite(baseline)) return;
    const FontFace* face = select_face(size, weight);
    if (!face) return;
    float pen = x;
    const float width = measured_width(*face, utf8);
    if (align == Align::Center) pen -= width * 0.5F;
    else if (align == Align::Right) pen -= width;
    if (!std::isfinite(pen)) return;

    const char* cursor = utf8;
    for (int character_index = 0; *cursor != '\0' && character_index < kMaximumTextCodepoints; ++character_index) {
        uint32_t codepoint{};
        cursor = next_utf8(cursor, &codepoint);
        const FontGlyph& glyph = glyph_for(*face, codepoint);
        const float glyph_x = pen + glyph.bearing_x;
        const float glyph_y = static_cast<float>(baseline) + glyph.bearing_y;
        int origin_x{},origin_y{};
        if(floor_to_int(glyph_x,&origin_x)&&floor_to_int(glyph_y,&origin_y)) {
            for (int row = 0; row < glyph.height; ++row) {
                const int64_t local_y64=static_cast<int64_t>(origin_y)+row-band_y_;
                if(local_y64<0||local_y64>=rows_)continue;
                for(int column=0;column<glyph.width;++column) {
                    const int64_t pixel_x=static_cast<int64_t>(origin_x)+column;
                    if(pixel_x<0||pixel_x>=width_)continue;
                    blend_local(static_cast<int>(pixel_x),static_cast<int>(local_y64),color,
                                glyph_alpha(*face,glyph,row*glyph.width+column));
                }
            }
        }
        pen += static_cast<float>(glyph.advance_64) / 64.0F;
        if (!std::isfinite(pen)) return;
    }
}

void Canvas::icon(Icon which, float cx, float cy, float size, Color color, float angle_deg) {
    if (!valid() || !finite(cx) || !finite(cy) || !finite(size) || !finite(angle_deg) || size <= 0.0F) return;
    const int icon_index = static_cast<int>(which);
    if (icon_index < 0 || icon_index >= icon_data::kIconCount) return;
    const IconMask& mask = icon_data::kIcons[icon_index];
    const float scale = static_cast<float>(size) / 24.0F;
    if (!std::isfinite(scale) || scale <= 0.0F) return;
    const float radians = normalize_degrees(angle_deg) * kDegreesToRadians;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float center_x = cx;
    const float center_y = cy;
    const auto bounds=clipped(cx-static_cast<double>(size)*0.708,cy-static_cast<double>(size)*0.708,cx+static_cast<double>(size)*0.708,cy+static_cast<double>(size)*0.708,band_y_,rows_,width_);
    for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
        const float pixel_y = static_cast<float>(band_y_) + static_cast<float>(local_y) + 0.5F;
        for (int pixel_x = bounds.x0; pixel_x < bounds.x1; ++pixel_x) {
            const float dx = static_cast<float>(pixel_x) + 0.5F - center_x;
            const float dy = pixel_y - center_y;
            // Inverse transform maps a destination point into the 24x24 mask.
            const float local_x = (cosine * dx + sine * dy) / scale + 12.0F;
            const float local_y_svg = (-sine * dx + cosine * dy) / scale + 12.0F;
            blend_local(pixel_x, local_y, color, sampled_icon_alpha(mask, local_x, local_y_svg));
        }
    }
}

float text_width(const char* utf8, float size, int weight) {
    const FontFace* face = select_face(size, weight);
    if (!face) return 0.0F;
    return static_cast<float>(measured_width(*face, utf8));
}

}  // namespace panel_draw
