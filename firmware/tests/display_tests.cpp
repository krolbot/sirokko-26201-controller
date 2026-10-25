#define main baseline_suite_entry
#include "sim_tests.cpp"
#undef main
#define SIROKKO_DISPLAY_HOST_TEST
#include "../main/local_display.cpp"

namespace {
bool off(const Outputs& o) { return !o.fuel_command && !o.fuel_permit && !o.glow_command; }
void geometry_and_SPI_config() {
    spi_fake::reset(); LocalDisplay d; check(d.begin(),"real display init");
    const auto& c=spi_fake::config;
    check(c.pclk_hz==10'000'000 && c.spi_mode==0 && c.cs_gpio_num==47 && c.dc_gpio_num==48,"actual SPI configuration");
    check(c.cs_ena_pretrans==1 && c.cs_ena_posttrans==1,"explicit CS setup/hold cycles");
    check(spi_fake::allocated==9600 && spi_fake::completed.size()==16,"single 20-row DMA geometry/init strips");
    check(board::kTftBandRows==kUiBandRows && board::kTftBandCount==kUiBandCount && board::kTftBandWireTimeUs==7680,"shared geometry and budget");
    for (size_t i=0;i<spi_fake::completed.size();++i) check(spi_fake::completed[i].y==static_cast<int>(i)*kUiBandRows,"contiguous full-panel init");
}
void frame_and_DMA_ownership() {
    spi_fake::reset(); LocalDisplay d; check(d.begin(),"display init"); spi_fake::completed.clear();
    UiFrame first{}; first.settings={1,20000,1000,0}; first.page=UiPage::SETPOINT;
    UiFrame changed=first; changed.settings.setpoint_milli_c=31000;
    for (uint8_t i=0;i<kUiBandCount;++i) {
        check(d.render(i?changed:first),"render strip");
        check(d.render(changed),"busy render must not rewrite DMA");
        spi_fake::advance(spi_fake::pending_end);
        std::array<uint16_t,kUiWidth*kUiBandRows> expected{}; render_ui_band(expected.data(),i,first);
        check(spi_fake::completed.back().pixels==std::vector<uint16_t>(expected.begin(),expected.end()),"frame changed partway through 16 strips");
    }
    check(spi_fake::completed.size()==16,"missing strip");
    check(d.render(changed),"next frame"); spi_fake::advance(spi_fake::pending_end);
}
void shared_bus_load_and_STOP() {
    spi_fake::reset(); LocalDisplay d; check(d.begin(),"display init"); spi_fake::transactions.clear();
    Sim s; s.reach_run_high(); const uint32_t start=s.in.now_ms; const int64_t origin=spi_fake::now;
    uint32_t passes{},edges{},last_fall=start,max_fall{}; bool prior=s.out.watchdog_heartbeat;
    int64_t max_wait{};
    UiFrame f{}; f.settings={1,20000,1000,0};
    for (int64_t elapsed=0;elapsed<=10'000'000;elapsed+=100) {
        spi_fake::advance(origin+elapsed);
        // Sensor requests intentionally arrive during a pixel DMA, not in an idle bus gap.
        if (elapsed%10'000==100) {
            for (int cs:{board::kMaxExhaustCs,board::kMaxAirCs}) {
                spi_fake::reserve(cs,board::kMaxSpiMode,board::kMaxSpiClockHz,128);
                max_wait=std::max(max_wait,spi_fake::transactions.back().start-spi_fake::now);
            }
        }
        // Native build uses 100Hz ticks: pdMS_TO_TICKS(12) is one 10ms tick.
        if (elapsed%10'000==0) check(d.render(f),"loaded render");
        if (elapsed%20'000==0) {
            s.in.now_ms=start+elapsed/1000;
            s.in.local_ui_ok=d.healthy();
            s.in.exhaust.sample_ms=s.in.hot_air.sample_ms=s.in.room.sample_ms=s.in.now_ms;
            if (elapsed==10'000'000) s.in.stop_active=true;
            s.tick(false); ++passes;
            if (prior && !s.out.watchdog_heartbeat) { max_fall=std::max(max_fall,s.in.now_ms-last_fall); last_fall=s.in.now_ms; ++edges; }
            prior=s.out.watchdog_heartbeat;
        }
    }
    check(passes==501 && edges>100 && max_fall<=100,"control/heartbeat model starvation");
    check(off(s.out) && s.out.fan_request,"STOP under SPI load lost safe postpurge");
    check(max_wait>7000 && max_wait<=8000,"MAX contention was not exercised or exceeded one bounded TFT strip");
    for (size_t i=1;i<spi_fake::transactions.size();++i)
        check(spi_fake::transactions[i].start>=spi_fake::transactions[i-1].end,"overlapping abstract bus ownership");
    std::cout<<"SPI LOAD MODEL: 10s, control="<<passes<<", falling_edges="<<edges<<", max_falling_ms="<<max_fall
             <<", max_MAX_wait_us="<<max_wait<<", transactions="<<spi_fake::transactions.size()<<'\n';
}
void stalled_DMA_deadline_and_fuel_OFF() {
    spi_fake::reset(); LocalDisplay d; check(d.begin(),"display init"); Sim s; s.reach_run_high(); UiFrame f{};
    check(d.render(f),"submit stalled strip"); spi_fake::stalled=true; const int64_t origin=spi_fake::now;
    for (int i=1;i<=4;++i) {
        spi_fake::advance(origin+i*20000); d.render(f); s.in.local_ui_ok=d.healthy(); s.advance(20);
    }
    check(!d.healthy() && off(s.out) && (s.faults()&FAULT_LOCAL_UI),"stalled SPI did not close fuel via real core");
    spi_fake::reset(); LocalDisplay failed; spi_fake::draw_error=true; check(!failed.begin(),"SDK draw failure accepted");
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> cases={
        {"real_ST7789_config_10MHz_CS_cycles_and_16strips",geometry_and_SPI_config},
        {"real_display_frame_coherence_and_DMA_ownership",frame_and_DMA_ownership},
        {"shared_SPI_arbitration_load_STOP_heartbeat_model",shared_bus_load_and_STOP},
        {"real_display_stall_deadline_core_fuel_OFF",stalled_DMA_deadline_and_fuel_OFF}};
    unsigned passed{};
    for (const auto& [name,test]:cases) try { test(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
        catch (const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    spi_fake::reset();
    std::cout<<"RESULT "<<passed<<'/'<<cases.size()<<" passed (real display/core; injected SDK, nominal serial-bus model, NOT physical SPI)\n";
    return passed==cases.size()?0:1;
}
