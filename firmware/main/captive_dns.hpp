#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sirokko::portal {
// Single uncompressed question, bounded to the classic 512-byte DNS datagram.
// Return no answers for IPv6 instead of inventing an unreachable IPv6 address.
inline size_t dns_reply(uint8_t* data, size_t size, size_t capacity) {
    if (!data || size < 17 || size > 512 || size > capacity ||
        (data[2] & 0xf8) || data[4] != 0 || data[5] != 1 ||
        data[6] || data[7] || data[8] || data[9]) return 0;
    size_t pos = 12;
    while (pos < size && data[pos]) {
        const size_t length = data[pos++];
        if (length > 63 || pos + length >= size || pos + length - 12 > 253) return 0;
        pos += length;
    }
    if (pos >= size || pos + 5 > size) return 0;
    ++pos;
    const bool address = data[pos] == 0 && data[pos+1] == 1 && data[pos+2] == 0 && data[pos+3] == 1;
    const size_t end = pos + 4;
    if (address && end + 16 > capacity) return 0;
    data[2] = static_cast<uint8_t>(0x84 | (data[2] & 1));
    data[3] = 0;
    data[6] = 0; data[7] = address ? 1 : 0;
    data[8] = data[9] = data[10] = data[11] = 0;
    if (!address) return end;
    const uint8_t answer[] = {0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,192,168,0,1};
    std::memcpy(data + end, answer, sizeof(answer));
    return end + sizeof(answer);
}
} // namespace sirokko::portal
bool captive_dns_start();
