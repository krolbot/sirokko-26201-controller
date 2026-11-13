#pragma once
#include <cstdint>
namespace network_config {
inline constexpr char address[] = "192.168.26.1";
inline constexpr char address_with_port[] = "192.168.26.1:80";
inline constexpr char url[] = "http://192.168.26.1/";
inline constexpr uint8_t octets[] = {192, 168, 26, 1};
inline constexpr unsigned clients = 4;
} // namespace network_config
