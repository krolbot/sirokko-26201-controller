#pragma once

#include <cstdint>
#include "sirokko/control.hpp"

class SensorSuite {
public:
    bool begin();
    void poll(uint64_t now_ms);
    sirokko::Sensors readings() const;
private:
    struct Impl;
    Impl* impl_{};
};
