#pragma once

namespace sirokko {
// One physical DMA/render geometry shared by UI, TFT adapter and host images.
inline constexpr int kUiWidth = 240;
inline constexpr int kUiHeight = 320;
inline constexpr int kUiBandRows = 20;
inline constexpr int kUiBandCount = kUiHeight / kUiBandRows;
static_assert(kUiHeight % kUiBandRows == 0);
} // namespace sirokko
