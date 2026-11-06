#include "panel_font.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

bool different(const panel_font::Glyph& left, const panel_font::Glyph& right) {
    for (int index = 0; index < panel_font::kGlyphWidth; ++index) {
        if (left.columns[index] != right.columns[index]) return true;
    }
    return false;
}

void test_utf8_cyrillic_and_ascii_case() {
    uint32_t codepoint{};
    const char* tail = panel_font::next_utf8("СИРОККО", &codepoint);
    assert(codepoint == 0x0421U);
    assert(*tail != '\0');
    assert(different(panel_font::glyph(codepoint), panel_font::glyph('?')));
    assert(different(panel_font::glyph('a'), panel_font::glyph('A')));
}

void test_invalid_utf8_has_visible_fallback() {
    const char malformed[] = {static_cast<char>(0xFF), '\0'};
    uint32_t codepoint{};
    const char* tail = panel_font::next_utf8(malformed, &codepoint);
    assert(codepoint == static_cast<uint32_t>('?'));
    assert(*tail == '\0');
    assert(different(panel_font::glyph(codepoint), panel_font::glyph(' ')));
}

}  // namespace

int main() {
    test_utf8_cyrillic_and_ascii_case();
    test_invalid_utf8_has_visible_fallback();
    std::cout << "PASS: panel UTF-8 glyph lookup and fallback\n";
}
