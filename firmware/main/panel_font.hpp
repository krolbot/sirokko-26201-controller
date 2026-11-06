// SPDX-License-Identifier: MIT
// Custom 5x7 panel glyphs authored for Sirokko; no third-party font asset is embedded.
#pragma once

#include <cstdint>

namespace panel_font {

struct Glyph {
    uint8_t columns[5];  // bit 0 is the top pixel of a 5x7 glyph
};

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6;

const Glyph& glyph(uint32_t codepoint);
// Decodes one UTF-8 codepoint and returns the first byte after it. Malformed input becomes '?'.
const char* next_utf8(const char* text, uint32_t* codepoint);

}  // namespace panel_font
