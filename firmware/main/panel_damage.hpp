#pragma once
#include <array>
#include <cstdint>
#include "panel_render.hpp"

namespace panel_render {
// Tile signatures avoid a full framebuffer. Force/invalidate bypasses comparisons on every safety scene.
// Signatures are presentation optimizations only, never input to a control/safety decision.
class DamageTracker {
  public:
    static constexpr int kTileWidth = 32;
    static constexpr int kColumns = kWidth / kTileWidth;
    static constexpr int kBands = kHeight / kStripeRows;
    struct Stripe {
        int band{-1}, x{}, width{};
        std::array<uint64_t, kColumns> hashes{};
    };
    void invalidate() { valid_.fill(false); }
    Stripe inspect(const uint16_t *pixels, int y, int rows) const {
        Stripe s;
        if (!pixels || y < 0 || y % kStripeRows || rows != kStripeRows || y + rows > kHeight)
            return s;
        s.band = y / kStripeRows;
        int first = kColumns, last = -1;
        for (int tile = 0; tile < kColumns; ++tile) {
            uint64_t h = 1469598103934665603ULL;
            for (int row = 0; row < rows; ++row)
                for (int x = 0; x < kTileWidth; ++x)
                    h = (h ^ pixels[row * kWidth + tile * kTileWidth + x]) * 1099511628211ULL;
            s.hashes[static_cast<size_t>(tile)] = h;
            const size_t index = static_cast<size_t>(s.band * kColumns + tile);
            if (!valid_[index] || hashes_[index] != h) {
                if (first == kColumns)
                    first = tile;
                last = tile;
            }
        }
        if (last >= first) {
            s.x = first * kTileWidth;
            s.width = (last - first + 1) * kTileWidth;
        }
        return s;
    }
    void commit(const Stripe &s) {
        if (s.band < 0 || s.band >= kBands)
            return;
        for (int tile = 0; tile < kColumns; ++tile) {
            const size_t i = static_cast<size_t>(s.band * kColumns + tile);
            hashes_[i] = s.hashes[static_cast<size_t>(tile)];
            valid_[i] = true;
        }
    }

  private:
    std::array<uint64_t, kColumns * kBands> hashes_{};
    std::array<bool, kColumns * kBands> valid_{};
};
} // namespace panel_render
