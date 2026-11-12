#include "../main/captive_dns.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <random>
int main() {
    const uint8_t query[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'w','w','w',4,'t','e','s','t',0,0,1,0,1};
    std::array<uint8_t,512> packet{};
    auto reset=[&]{packet.fill(0);std::memcpy(packet.data(),query,sizeof(query));};
    reset();auto n=sirokko::portal::dns_reply(packet.data(),sizeof(query),packet.size());
    assert(n==sizeof(query)+16 && packet[0]==0x12 && packet[1]==0x34 && packet[7]==1);
    assert(packet[n-4]==192 && packet[n-3]==168 && packet[n-2]==26 && packet[n-1]==1);
    reset();packet[23]=28;assert(sirokko::portal::dns_reply(packet.data(),sizeof(query),packet.size())==sizeof(query));assert(packet[7]==0);
    reset();assert(sirokko::portal::dns_reply(packet.data(),sizeof(query),sizeof(query))==0);
    for(size_t size=0;size<sizeof(query);++size){reset();assert(sirokko::portal::dns_reply(packet.data(),size,packet.size())==0);}
    reset();packet[12]=0xc0;assert(sirokko::portal::dns_reply(packet.data(),sizeof(query),packet.size())==0);
    reset();packet[5]=2;assert(sirokko::portal::dns_reply(packet.data(),sizeof(query),packet.size())==0);
    reset();packet[2]=0x81;assert(sirokko::portal::dns_reply(packet.data(),sizeof(query),packet.size())==0);
    std::mt19937 rng(42);
    for(int i=0;i<10000;++i){for(auto& b:packet)b=static_cast<uint8_t>(rng());const size_t size=rng()%513;assert(sirokko::portal::dns_reply(packet.data(),size,packet.size())<=packet.size());}
}
