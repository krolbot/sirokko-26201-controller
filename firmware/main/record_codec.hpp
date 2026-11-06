#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// NVS record integrity, not cryptographic authenticity. Never store active RUN/timer state.
namespace record_codec {
inline uint64_t checksum(const void* data, size_t size) {
    const auto* p=static_cast<const uint8_t*>(data);
    uint64_t hash=1469598103934665603ULL;
    for(size_t i=0;i<size;++i) hash=(hash^p[i])*1099511628211ULL;
    return hash;
}
template<class T> struct Record {
    uint32_t magic{};
    uint16_t version{};
    uint16_t payload_size{};
    T value{};
    uint64_t checksum{};
};
template<class T> Record<T> encode(uint32_t magic,const T& value) {
    static_assert(std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value);
    Record<T> r{};
    r.magic=magic;r.version=2;r.payload_size=sizeof(T);r.value=value;
    r.checksum=checksum(&r,offsetof(Record<T>,checksum));
    return r;
}
template<class T> bool valid(const Record<T>& r,uint32_t magic,size_t bytes) {
    return bytes==sizeof(r) && r.magic==magic && r.version==2 && r.payload_size==sizeof(T) &&
        r.checksum==checksum(&r,offsetof(Record<T>,checksum));
}
}
