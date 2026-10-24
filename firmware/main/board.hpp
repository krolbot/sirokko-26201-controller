#pragma once

#include <cstddef>
#include <cstdint>

namespace sirokko::board {

inline constexpr int kMaxExhaustCs = 10;
inline constexpr int kMaxAirCs = 9;
inline constexpr int kMaxExhaustDrdy = 1;
inline constexpr int kMaxAirDrdy = 2;
inline constexpr int kSpiMosi = 11;
inline constexpr int kSpiSclk = 12;
inline constexpr int kSpiMiso = 13;
inline constexpr int kRoomDq = 4;

inline constexpr int kTftCs = 47;
inline constexpr int kTftDc = 48;
inline constexpr int kTftReset = -1;  // The module RST follows the DevKit EN/CHIP_PU net.
inline constexpr int kMaxSpiMode = 1;
inline constexpr int kTftSpiMode = 0;
inline constexpr uint32_t kMaxSpiClockHz = 5'000'000;
inline constexpr uint32_t kTftSpiClockHz = 20'000'000;
inline constexpr int kTftWidth = 240;
inline constexpr int kTftHeight = 320;
inline constexpr int kTftBandRows = 40;
inline constexpr int kTftBandCount = kTftHeight / kTftBandRows;
inline constexpr size_t kTftBandBytes =
    static_cast<size_t>(kTftWidth) * kTftBandRows * sizeof(uint16_t);
inline constexpr uint32_t kTftBandWireTimeUs = static_cast<uint32_t>(
    (static_cast<uint64_t>(kTftBandBytes) * 8u * 1'000'000u + kTftSpiClockHz - 1u) /
    kTftSpiClockHz);
inline constexpr uint32_t kTftBandTransferBudgetUs = kTftBandWireTimeUs + 500u;

inline constexpr bool shared_spi_chip_selects_are_distinct() {
    return kTftCs != kMaxExhaustCs && kTftCs != kMaxAirCs &&
           kMaxExhaustCs != kMaxAirCs;
}

static_assert(kTftHeight % kTftBandRows == 0);
static_assert(shared_spi_chip_selects_are_distinct());

}  // namespace sirokko::board
