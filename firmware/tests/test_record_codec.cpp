#include "record_codec.hpp"
#include "sirokko/control.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace sirokko;
int main() {
    constexpr uint32_t magic=0x53495232;
    Settings settings;settings.mode=Mode::MANUAL;settings.manual_percent=65;settings.duration_ms=60000;
    auto original=record_codec::encode(magic,settings);
    assert(record_codec::valid(original,magic,sizeof(original)));
    assert(original.value.manual_percent==65 && original.value.duration_ms==60000);
    size_t flips=0;
    for(size_t byte=0;byte<sizeof(original);++byte) for(unsigned bit=0;bit<8;++bit) {
        auto damaged=original;
        auto* raw=reinterpret_cast<unsigned char*>(&damaged);
        raw[byte]^=1U<<bit;
        // Trailing ABI padding beyond checksum has no persistent semantics.
        if(byte>=offsetof(decltype(original),checksum)+sizeof(original.checksum)) continue;
        assert(!record_codec::valid(damaged,magic,sizeof(damaged)));++flips;
    }
    assert(!record_codec::valid(original,magic+1,sizeof(original)));
    assert(!record_codec::valid(original,magic,sizeof(original)-1));
    auto bad=original;bad.version=99;
    bad.checksum=record_codec::checksum(&bad,offsetof(decltype(bad),checksum));
    assert(!record_codec::valid(bad,magic,sizeof(bad)));
    Settings out_of_range=settings;out_of_range.manual_percent=66;
    auto encoded_bad=record_codec::encode(magic,out_of_range);
    assert(record_codec::valid(encoded_bad,magic,sizeof(encoded_bad)) && !valid_settings(encoded_bad.value));
    std::cout<<"PASS NVS record codec: "<<flips<<" single-bit corruptions rejected, length/type/version guards, semantic validation separate\n";
}
