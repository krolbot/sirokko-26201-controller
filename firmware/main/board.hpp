#pragma once

#include "driver/gpio.h"

namespace board {
constexpr gpio_num_t kRoomDq = GPIO_NUM_4;
constexpr gpio_num_t kExhaustDrdy = GPIO_NUM_1;
constexpr gpio_num_t kAirDrdy = GPIO_NUM_2;
constexpr gpio_num_t kAirCs = GPIO_NUM_9;
constexpr gpio_num_t kExhaustCs = GPIO_NUM_10;
constexpr gpio_num_t kMosi = GPIO_NUM_11;
constexpr gpio_num_t kSck = GPIO_NUM_12;
constexpr gpio_num_t kMiso = GPIO_NUM_13;
constexpr gpio_num_t kGlow = GPIO_NUM_14;
constexpr gpio_num_t kFuel = GPIO_NUM_15;
constexpr gpio_num_t kFanRequest = GPIO_NUM_16;
constexpr gpio_num_t kSpeedPwm = GPIO_NUM_17;
// KEY0: module 12 -> J32.12 -> J3.8 -> GPIO40. Expected NO-to-GND / active-low
// with the MCU pull-up enabled; this is a schematic expectation, not a measurement.
constexpr gpio_num_t kStartStopButton = GPIO_NUM_40;
constexpr gpio_num_t kTftMosi = GPIO_NUM_7;
constexpr gpio_num_t kTftSck = GPIO_NUM_8;
constexpr gpio_num_t kTftReset = GPIO_NUM_6;
constexpr gpio_num_t kTftDc = GPIO_NUM_41;
constexpr gpio_num_t kTftCs = GPIO_NUM_42;
constexpr gpio_num_t kEncoderA = GPIO_NUM_5;
constexpr gpio_num_t kEncoderB = GPIO_NUM_38;
constexpr gpio_num_t kEncoderPush = GPIO_NUM_47;
constexpr int kSpiClockHz = 1000000;
constexpr int kPwmFrequencyHz = 15000;
} // namespace board
