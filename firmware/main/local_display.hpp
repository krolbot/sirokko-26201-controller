#pragma once
#include <atomic>
#include <cstdint>
#include "board.hpp"
#include "sirokko/ui.hpp"

inline constexpr uint32_t local_display_startup_fault(bool initialized) {
    return initialized ? sirokko::FAULT_NONE : sirokko::FAULT_LOCAL_UI;
}
class LocalDisplay {
public:
    bool begin();
    bool render(const sirokko::UiFrame&);
    bool healthy() const { return healthy_.load(); }
private:
    bool submit_band(uint8_t band);
    void* io_{};
    void* panel_{};
    uint16_t* pixels_{};
    std::atomic<bool> transfer_in_progress_{};
    int64_t transfer_started_us_{};
    uint8_t next_band_{};
    sirokko::UiFrame frame_{};
    std::atomic<bool> healthy_{};
};
