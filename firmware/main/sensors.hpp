#pragma once

#include <cstdint>

#include "sirokko/control.hpp"

class SensorSuite {
public:
    bool begin();
    void poll(uint32_t now_ms);

    sirokko::SensorSample exhaust() const;
    sirokko::SensorSample hot_air() const;
    sirokko::SensorSample room() const;
    uint8_t exhaust_status() const;
    uint8_t hot_air_status() const;

private:
    struct Impl;
    Impl* impl_{};
};
