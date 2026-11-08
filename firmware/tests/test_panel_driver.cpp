// Exercise the production panel integration with explicit test-only DMA/runtime shims.
#include "../main/panel.cpp"
#include <array>
#include <cassert>
#include <cstdio>

namespace {
RuntimeView test_runtime{};
bool view_available=true,change_during_grant=false;
int submitted{};
void advance(uint64_t now) {fake_idf::clock_us=now*1000;test_runtime.now_ms=now;}
void finish_frame(uint64_t& now) {
    for(int steps=0;steps<100;++steps) {
        if(fake_idf::pending)fake_idf::complete();
        complete_transfer(0);
        if(!g_display.frame_active) return;
        advance(++now);render_step(now);
    }
    assert(false&&"frame did not finish");
}
void verify_framebuffer() {
    std::array<uint16_t,320*16> expected{},scratch{};
    for(int y=0;y<240;y+=16) {
        assert(g_renderer.draw_band(y,16,expected.data(),scratch.data()));
        assert(std::equal(expected.begin(),expected.end(),fake_idf::framebuffer.begin()+y*320));
    }
}
void reset_display() {
    if(fake_idf::pending)fake_idf::complete();
    complete_transfer(0);display_release();g_started=false;fake_idf::fail_draw=false;
}
}
bool runtime_view(RuntimeView* out){if(!view_available)return false;*out=test_runtime;return true;}
bool runtime_issue(sirokko::Action a,uint64_t hash,sirokko::Grant* grant) {
    if(change_during_grant)++test_runtime.control.state_version;
    *grant={1,test_runtime.control.state_version,hash,test_runtime.now_ms+1000,a,true};return true;
}
bool runtime_submit(const sirokko::Grant&,sirokko::Action,uint64_t,const sirokko::Settings*){++submitted;return true;}
bool runtime_network_credentials(char*,size_t,char*,size_t){return false;}

int main() {
    uint64_t now=10000;advance(now);
    test_runtime.control.state=sirokko::State::IDLE;test_runtime.control.start_block=sirokko::Block::NONE;
    test_runtime.control.boot_id=7;test_runtime.settings.room_setpoint_milli_c=26000;
    test_runtime.sensors.room={24500,now,0,true};test_runtime.sensors.air={22000,now,0,true};test_runtime.sensors.exhaust={23000,now,0,true};
    if(kRotation==0) {assert(!panel_begin());assert(fake_idf::draws==0);std::puts("PASS: unsupported portrait layout rejected");return 0;}
    assert(panel_begin());assert(fake_idf::swap&&fake_idf::mirror_x&&!fake_idf::mirror_y);
    assert(fake_idf::task_priority==3&&fake_idf::task_stack==8192);assert(fake_idf::spi_hz==20000000);
    assert(fake_idf::endian==LCD_RGB_DATA_ENDIAN_BIG);
    render_step(now);assert(fake_idf::pending&&g_display.in_flight);
    const auto first=fake_idf::transfers.back().bytes;render_step(now+1);
    assert(fake_idf::draws==1&&fake_idf::transfers.back().bytes==first);
    finish_frame(now);verify_framebuffer();
    // An unchanged scene sends no additional SPI pixels.
    const int idle_draws=fake_idf::draws;now+=50;advance(now);render_step(now);finish_frame(now);
    assert(fake_idf::draws==idle_draws);verify_framebuffer();
    // An in-flight DMA stripe is retained; a newly observed fault cancels the old scene at its boundary.
    test_runtime.control.state=sirokko::State::RUN;++test_runtime.control.state_version;update_view();
    now+=50;advance(now);render_step(now);
    // The first crossfade frame has fraction 0 and can be identical to the old idle image.
    finish_frame(now);now+=50;advance(now);render_step(now);
    while(!fake_idf::pending&&g_display.frame_active){advance(++now);render_step(now);}
    assert(fake_idf::pending);
    test_runtime.control.state=sirokko::State::FAULT_PURGE;test_runtime.control.faults=sirokko::FAULT_OVER_TEMPERATURE;
    ++test_runtime.control.state_version;update_view();
    const int old_draws=fake_idf::draws;render_step(now+1);assert(fake_idf::draws==old_draws);
    fake_idf::complete();advance(++now);render_step(now);
    assert(g_renderer.frame().scene==panel_render::Scene::FaultCooling&&!g_renderer.transitioning());
    assert(fake_idf::transfers.back().y==0);finish_frame(now);verify_framebuffer();
    // No semaphore callback must not cause infinite waits, buffer reuse or another SPI request.
    g_display.damage.invalidate();now+=50;advance(now);render_step(now);assert(fake_idf::pending);
    const int pending_draws=fake_idf::draws;const int old_frees=fake_idf::frees;
    advance(now+251);render_step(now+251);assert(g_display.failed&&g_display.in_flight);
    display_release();assert(fake_idf::frees==old_frees);
    render_step(now+300);assert(fake_idf::draws==pending_draws);reset_display();
    // Synchronous enqueue rejection also stops this display path, not the control owner.
    now+=400;advance(now);assert(panel_begin());fake_idf::fail_draw=true;render_step(now);
    assert(g_display.failed&&!g_display.in_flight&&!fake_idf::pending);reset_display();
    // Never borrow a grant from a newer controller context for an older UI command.
    test_runtime.control.state=sirokko::State::IDLE;test_runtime.control.faults=0;
    advance(++now);assert(panel_begin());
    panel_ui::Command command{};command.kind=panel_ui::CommandKind::SETTINGS;command.settings=test_runtime.settings;
    assert(execute_command(command)&&submitted==1);
    ++test_runtime.control.state_version;assert(!execute_command(command)&&submitted==1);
    update_view();change_during_grant=true;assert(!execute_command(command)&&submitted==1);change_during_grant=false;
    update_view();++test_runtime.control.boot_id;assert(!execute_command(command)&&submitted==1);
    view_available=false;assert(!execute_command(command));update_view();render_step(now);
    assert(g_renderer.frame().scene==panel_render::Scene::NoData&&!g_renderer.transitioning());reset_display();
    // A real DMA/damage frame contains the wave; STOP preempts it at the pending stripe boundary.
    view_available=true;test_runtime.control.state=sirokko::State::RUN;test_runtime.control.faults=0;
    test_runtime.control.outputs.fuel=true;now=24800;advance(now);test_runtime.sensors.room.sample_ms=now;
    assert(panel_begin());render_step(now);finish_frame(now);verify_framebuffer();
    const auto heat_frame=fake_idf::framebuffer;
    now+=300;advance(now);test_runtime.sensors.room.sample_ms=now;update_view();
    render_step(now);finish_frame(now);verify_framebuffer();assert(fake_idf::framebuffer!=heat_frame);
    assert(g_renderer.frame().current==24.5f&&g_renderer.frame().target==26);
    now+=40;advance(now);render_step(now);
    while(!fake_idf::pending&&g_display.frame_active){advance(++now);render_step(now);}
    assert(fake_idf::pending);
    test_runtime.control.state=sirokko::State::STOP_PURGE;++test_runtime.control.state_version;update_view();
    const int before_stop=fake_idf::draws;render_step(now+1);assert(fake_idf::draws==before_stop);
    fake_idf::complete();advance(++now);render_step(now);
    assert(g_renderer.frame().scene==panel_render::Scene::Cooldown&&!g_renderer.transitioning());
    assert(fake_idf::transfers.back().y==0);finish_frame(now);verify_framebuffer();reset_display();
    // No fuel means no heat-wave activity or repeated DMA for a stationary gauge.
    test_runtime.control.state=sirokko::State::RUN;test_runtime.control.outputs.fuel=false;
    now+=100;advance(now);test_runtime.sensors.room.sample_ms=now;assert(panel_begin());
    render_step(now);finish_frame(now);verify_framebuffer();const int quiet_draws=fake_idf::draws;
    now+=400;advance(now);test_runtime.sensors.room.sample_ms=now;update_view();
    render_step(now);finish_frame(now);assert(fake_idf::draws==quiet_draws);verify_framebuffer();reset_display();
    assert(fake_idf::allocations==fake_idf::frees);
    std::puts("PASS: production panel.cpp: landscape, RGB565 wire order, DMA lifetime, unchanged-scene suppression, fault preemption, timeout/enqueue failure, context-bound grants, unavailable snapshot, active heat-wave DMA, immediate STOP and no-fuel silence");
}
