#include "panel_render.hpp"
#include "panel_damage.hpp"
#include "panel_canvas.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using panel_render::Renderer;
using panel_render::Scene;
using panel_ui::Page;
using panel_ui::EditField;
using panel_ui::MenuItem;
using sirokko::State;
using Picture=std::array<uint16_t,panel_render::kWidth*panel_render::kHeight>;

struct Fixture {
    const char* id;
    panel_ui::Screen screen{};
    panel_ui::View view{};
    panel_ui::LocalCredentials credentials{};
};
void fresh(panel_ui::View& v,uint64_t now) {
    v.now_ms=now;v.sensors.room.sample_ms=v.sensors.air.sample_ms=v.sensors.exhaust.sample_ms=now;
}
Fixture base(const char* id) {
    Fixture f{id};
    f.view.control.state=State::RUN;f.view.control.start_block=sirokko::Block::NONE;
    f.view.control.commissioning.block=sirokko::Block::NONE;
    f.view.control.envelope_provisioned=true;f.view.control.learned_model.valid=true;
    f.view.control.boot_id=1;f.view.control.run_id=1;
    f.view.settings.room_setpoint_milli_c=26000;f.view.settings.manual_percent=55;
    f.view.sensors.room={24500,10000,0,true};f.view.sensors.air={65000,10000,0,true};
    f.view.sensors.exhaust={220000,10000,0,true};fresh(f.view,10000);
    f.screen.menu_count=8;
    f.screen.menu_rows={MenuItem::MODE,MenuItem::TEMPERATURE,MenuItem::SHUTDOWN_TIMER,MenuItem::CALIBRATION};
    return f;
}
std::vector<Fixture> fixtures() {
    std::vector<Fixture> result;
    auto add=[&](const char* id)->Fixture& {result.push_back(base(id));return result.back();};
    add("heat").view.control.outputs.fuel=true;add("idle").view.control.state=State::IDLE;
    add("manual").view.settings.mode=sirokko::Mode::MANUAL;
    auto& edit=add("edit");edit.screen.page=Page::EDIT;edit.screen.editing=true;edit.screen.edit_field=EditField::TEMPERATURE;edit.screen.edit_value=27000;
    auto& timer=add("timer-running");timer.view.control.outputs.fuel=true;timer.view.control.duration_active=true;timer.view.control.duration_remaining_ms=80*60000ULL;
    auto& above=add("above");above.view.control.room_excess=true;above.view.sensors.room.milli_c=27100;
    for(auto pair: {std::pair{"prepurge",State::PREPURGE},{"preheat",State::PREHEAT},{"ignition",State::IGNITION},
                   {"verify",State::VERIFY},{"cooldown",State::STOP_PURGE},{"recovery",State::RECOVERY_PURGE}})
        add(pair.first).view.control.state=pair.second;
    auto& fc=add("fault-cooling");fc.view.control.state=State::FAULT_PURGE;fc.view.control.faults=sirokko::FAULT_COMMISSIONING;fc.view.control.commissioning.reason=sirokko::CommissioningReason::OVERALL_TIMEOUT;
    auto& fl=add("fault-latched");fl.view.control.state=State::FAULT;fl.view.control.faults=sirokko::FAULT_COMMISSIONING;fl.view.control.commissioning.reason=sirokko::CommissioningReason::OPERATOR_NEGATIVE;
    auto& sensor=add("sensor");sensor.view.control.state=State::IDLE;sensor.view.sensors.room.valid=false;sensor.view.control.start_block=sirokko::Block::SENSOR_INVALID;
    auto& cal=add("calibrate");cal.view.control.commissioning.active=true;cal.view.control.commissioning.phase=sirokko::CommissioningPhase::SETTLE_POINT;
    auto& cc=add("cal-cooling");cc.view.control.state=State::STOP_PURGE;cc.view.control.commissioning.active=true;cc.view.control.commissioning.phase=sirokko::CommissioningPhase::COMPLETE_PENDING_STOP;
    auto& saved=add("cal-saved");saved.view.control.state=State::IDLE;saved.view.control.commissioning.phase=sirokko::CommissioningPhase::SAVED;
    auto& reference=add("cal-reference");reference.view.control.commissioning.active=true;reference.view.control.commissioning.phase=sirokko::CommissioningPhase::SETTLE_REFERENCE;
    auto& repeat=add("cal-repeat");repeat.view.control.commissioning.active=true;repeat.view.control.commissioning.phase=sirokko::CommissioningPhase::SETTLE_REPEAT_POINT;
    auto& store=add("cal-store");store.view.control.state=State::IDLE;store.view.control.commissioning.active=true;store.view.control.commissioning.phase=sirokko::CommissioningPhase::COMPLETE_PENDING_STOP;
    auto& cancelled=add("cal-cancelled");cancelled.view.control.state=State::STOP_PURGE;cancelled.view.control.commissioning.phase=sirokko::CommissioningPhase::CANCELLED;
    add("menu").screen.page=Page::MENU;
    auto& mode=add("mode");mode.screen.page=Page::EDIT;mode.screen.editing=true;mode.screen.edit_field=EditField::MODE;mode.screen.edit_value=1;
    auto& te=add("timer-edit");te.screen.page=Page::EDIT;te.screen.editing=true;te.screen.edit_field=EditField::SHUTDOWN_TIMER;te.screen.edit_value=90;
    add("diagnostics").screen.page=Page::DIAGNOSTICS;
    auto& wifi=add("wifi");wifi.screen.page=Page::WIFI;wifi.credentials.available=true;
    // Explicit public fixtures only, never fetched from runtime/NVS.
    std::snprintf(wifi.credentials.ssid.data(),wifi.credentials.ssid.size(),"SSID устройства");
    std::snprintf(wifi.credentials.password.data(),wifi.credentials.password.size(),"••••••••");
    add("ota").screen.page=Page::OTA;
    auto& menu=add("menu-service");menu.screen.page=Page::MENU;menu.screen.menu_index=menu.screen.menu_first=4;
    menu.screen.menu_rows={MenuItem::DIAGNOSTICS,MenuItem::WIFI,MenuItem::OTA,MenuItem::BACK};
    auto& off=add("timer-off");off.screen.page=Page::EDIT;off.screen.editing=true;off.screen.edit_field=EditField::SHUTDOWN_TIMER;
    add("maintenance").view.control.state=State::MAINTENANCE;
    add("no-data").screen.notice=panel_ui::Notice::VIEW_UNAVAILABLE;
    auto& blocked=add("blocked");blocked.view.control.state=State::IDLE;blocked.view.control.start_block=sirokko::Block::ENVELOPE_MISSING;
    add("queued").screen.notice=panel_ui::Notice::QUEUED;
    add("rejected").screen.notice=panel_ui::Notice::EXTERNAL_CHANGE;
    auto& blocked_calibration=add("command-rejected");blocked_calibration.screen.notice=panel_ui::Notice::CALIBRATION_BLOCKED;
    blocked_calibration.view.control.commissioning.block=sirokko::Block::STORAGE_UNCONFIRMED;
    return result;
}
Picture picture(const Renderer& renderer) {
    Picture p{};
    constexpr size_t count=panel_render::kWidth*panel_render::kStripeRows;
    std::array<uint16_t,count+2> output{},scratch{};
    for(int y=0;y<panel_render::kHeight;y+=panel_render::kStripeRows) {
        output.front()=output.back()=scratch.front()=scratch.back()=0xa55a;
        assert(renderer.draw_band(y,panel_render::kStripeRows,output.data()+1,scratch.data()+1));
        assert(output.front()==0xa55a&&output.back()==0xa55a&&scratch.front()==0xa55a&&scratch.back()==0xa55a);
        std::copy_n(output.data()+1,count,p.data()+y*panel_render::kWidth);
    }
    assert(!renderer.draw_band(-1,16,output.data(),scratch.data()));
    assert(!renderer.draw_band(std::numeric_limits<int>::max(),16,output.data(),scratch.data()));
    assert(!renderer.draw_band(0,17,output.data(),scratch.data()));
    assert(!renderer.draw_band(235,16,output.data(),scratch.data()));
    assert(!renderer.draw_band(0,16,output.data(),output.data()));
    return p;
}
void save(const std::filesystem::path& path,const Picture& p) {
    std::ofstream out(path,std::ios::binary);assert(out);
    out<<"P6\n320 240\n255\n";
    for(uint16_t pixel:p) {
        const auto c=panel_draw::from565(pixel);
        const char rgb[]={static_cast<char>(c>>16),static_cast<char>(c>>8),static_cast<char>(c)};
        out.write(rgb,3);
    }
    assert(out.good());
}
// Pixel-level verification of the accepted heat wave and sample-driven marker motion.
Picture snapshot(const Fixture& f,uint64_t now) {
    Renderer r;r.update(f.screen,f.view,f.credentials,now);return picture(r);
}
void heat_wave_tests() {
    constexpr uint64_t cycle=2400,base_time=cycle*10;
    auto f=base("heat-wave");f.view.control.outputs.fuel=true;
    double previous_centroid=0;
    size_t changed_total=0;
    for(uint64_t phase: {0ULL,400ULL,700ULL,1000ULL,1300ULL,1600ULL,2200ULL}) {
        fresh(f.view,base_time+phase);const auto on=snapshot(f,base_time+phase);
        auto off=f;off.view.control.outputs.fuel=false;const auto plain=snapshot(off,base_time+phase);
        size_t changed=0;double mass=0,moment=0;
        constexpr float pi=3.14159265358979323846f,scale=112.0f/145.0f;
        const float ca=135+270*(24.5f-15)/15,ta=135+270*(26.0f-15)/15;
        for(int y=0;y<240;++y)for(int x=0;x<320;++x)if(on[y*320+x]!=plain[y*320+x]) {
            ++changed;
            const float dx=x+0.5f-160,dy=y+0.5f-127,radial=std::hypot(dx,dy);
            assert(std::abs(radial-112)<=24*scale*0.46f+0.001f);
            float a=std::atan2(dy,dx)*180/pi;if(a<135)a+=360;
            assert(a>=ca&&a<=ta);
            assert(std::hypot(dx-112*std::cos(ca*pi/180),dy-112*std::sin(ca*pi/180))>4*scale+1);
            assert(std::hypot(dx-112*std::cos(ta*pi/180),dy-112*std::sin(ta*pi/180))>12*scale+1);
            const auto oc=panel_draw::from565(on[y*320+x]),bc=panel_draw::from565(plain[y*320+x]);
            const double w=static_cast<double>((oc>>8)&255)-static_cast<double>((bc>>8)&255);
            if(w>0){mass+=w;moment+=a*w;}
        }
        if(phase==0||phase==2200)assert(changed==0);
        else {assert(changed>0&&mass>0);assert(moment/mass>previous_centroid);previous_centroid=moment/mass;}
        changed_total+=changed;
        Renderer flag;flag.update(f.screen,f.view,f.credentials,base_time+phase);assert(flag.animated());
    }
    assert(changed_total>0);
    fresh(f.view,base_time+1000);
    std::vector<Fixture> inactive;
    auto add=[&]()->Fixture& {inactive.push_back(f);return inactive.back();};
    add().view.control.state=State::IDLE;
    add().view.control.state=State::STOP_PURGE;
    add().view.control.state=State::FAULT;
    add().view.control.faults=sirokko::FAULT_OVER_TEMPERATURE;
    add().view.sensors.room.valid=false;
    add().view.sensors.room.status=1;
    add().view.sensors.room.sample_ms=base_time+1000-sirokko::kRoomSensorStaleMs-1;
    add().view.sensors.room.sample_ms=base_time+2000;
    add().view.settings.mode=sirokko::Mode::MANUAL;
    add().view.sensors.room.milli_c=25800;
    auto& high=add();high.view.sensors.room.milli_c=27000;high.view.control.room_excess=true;
    auto& edit=add();edit.screen.page=Page::EDIT;edit.screen.editing=true;
    edit.screen.edit_field=EditField::TEMPERATURE;edit.screen.edit_value=27000;
    auto& cal=add();cal.view.control.commissioning.active=true;cal.view.control.commissioning.phase=sirokko::CommissioningPhase::SETTLE_POINT;
    add().screen.notice=panel_ui::Notice::VIEW_UNAVAILABLE;
    for(const auto& value:inactive) {
        auto off=value;off.view.control.outputs.fuel=false;
        assert(snapshot(value,base_time+1000)==snapshot(off,base_time+1000));
    }
    f.view.control.duration_active=true;f.view.control.duration_remaining_ms=80*60000ULL;
    auto off=f;off.view.control.outputs.fuel=false;
    assert(snapshot(f,base_time+1000)!=snapshot(off,base_time+1000));
    f.view.control.duration_active=false;
    // STOP/fault must erase an already visible wave without a crossfade.
    for(State state: {State::STOP_PURGE,State::FAULT_PURGE}) {
        Renderer r;r.update(f.screen,f.view,{},base_time+1000);
        auto stopped=f;stopped.view.control.state=state;
        if(state==State::FAULT_PURGE)stopped.view.control.faults=sirokko::FAULT_OVER_TEMPERATURE;
        r.update(stopped.screen,stopped.view,{},base_time+1001);
        assert(!r.transitioning());assert(picture(r)==snapshot(stopped,base_time+1001));
    }
    // Actual equality or a newly lowered target cuts the wave immediately, before the graphical tween ends.
    for(bool lower_target: {false,true}) {
        Renderer on;on.update(f.screen,f.view,{},base_time+1000);Renderer quiet=on;
        auto next=f;
        if(lower_target)next.view.settings.room_setpoint_milli_c=24000;
        else next.view.sensors.room.milli_c=26000;
        on.update(next.screen,next.view,{},base_time+1001);next.view.control.outputs.fuel=false;
        quiet.update(next.screen,next.view,{},base_time+1001);assert(picture(on)==picture(quiet));
    }
    // Marker follows measured samples, not wave time; numeric temperature is immediately actual.
    auto measured=base("approach-test");measured.view.control.outputs.fuel=true;
    Renderer moving;fresh(measured.view,base_time);moving.update(measured.screen,measured.view,{},base_time);
    float previous=24.5f;
    uint64_t t=base_time;
    for(int32_t value: {24800,25100,25400,25700,26000}) {
        t+=400;measured.view.sensors.room.milli_c=value;fresh(measured.view,t);
        moving.update(measured.screen,measured.view,{},t);
        assert(moving.frame().current==previous);
        assert(moving.frame().view.sensors.room.milli_c==value&&moving.frame().target==26);
        moving.update(measured.screen,measured.view,{},t+panel_render::kMarkerMs/2);
        assert(moving.frame().current>previous&&moving.frame().current<value/1000.0f);
        moving.update(measured.screen,measured.view,{},t+panel_render::kMarkerMs);
        assert(moving.frame().current==value/1000.0f);previous=moving.frame().current;
    }
    assert(moving.frame().current==moving.frame().target&&!moving.animated());
    // Cooling moves the measured point away again, without inventing a rise toward the goal.
    t+=400;measured.view.sensors.room.milli_c=25000;fresh(measured.view,t);
    moving.update(measured.screen,measured.view,{},t);moving.update(measured.screen,measured.view,{},t+panel_render::kMarkerMs);
    assert(moving.frame().current==25&&moving.frame().target==26);
    fresh(measured.view,t+400);moving.update(measured.screen,measured.view,{},t+400);
    assert(moving.frame().current==25);
    std::printf("PASS: heat wave pixel mask/marker invariance/direction/pause; %zu disabled cases; STOP/fault preemption; measured-point approach/retreat\n",inactive.size());
}

void audit_panel_scene_tests() {
    const char* titles[] = {"Нужна настройка", "Калибровка выключена", "Неверный диапазон", "Не уложится в срок",
                            "Нет показаний", "Высокая температура", "Нужно охлаждение", "Устраните причину"};
    const char* details[] = {"Процесс не настроен", "Диапазон не разрешён", "Проверьте границы",
                             "Темп изменения слишком мал", "Проверьте датчики", "Дождитесь охлаждения",
                             "Дождитесь окончания", "Сначала сбросьте аварию", "Рабочий режим прерван",
                             "Нарушен темп команды", "Опорный режим нестабилен", "Оператор: нет/неустойчиво"};
    const char* footers[] = {"Настройте в браузере", "Нажать энкодер — сбросить", "Сброс: ожидание снимка",
                             "Сброс не подтверждён", "Управление через браузер", "Повторите действие"};
    for (const char* title : titles) assert(panel_draw::text_width(title, 22) <= 292);
    for (const char* detail : details) assert(panel_draw::text_width(detail, 12) <= 280);
    for (const char* footer : footers) assert(panel_draw::text_width(footer, 13) <= 280);

    for (sirokko::Block block : {sirokko::Block::ENVELOPE_MISSING, sirokko::Block::ENVELOPE_HARDWARE_MISMATCH,
                                 sirokko::Block::ENVELOPE_INVALID, sirokko::Block::STORAGE_UNCONFIRMED,
                                 sirokko::Block::SENSOR_INVALID}) {
        auto f = base("calibration-block");
        f.view.control.state = State::IDLE;
        f.screen.notice = panel_ui::Notice::CALIBRATION_BLOCKED;
        f.view.control.commissioning.block = block;
        Renderer renderer;
        renderer.update(f.screen, f.view, f.credentials, f.view.now_ms);
        assert(renderer.frame().scene == Scene::CommandRejected);
        const auto image = picture(renderer);
        assert(std::any_of(image.begin(), image.end(), [](uint16_t pixel) { return pixel != 0; }));
    }

    auto wifi = base("wifi-secret");
    wifi.screen.page = Page::WIFI;
    wifi.credentials.available = true;
    std::snprintf(wifi.credentials.ssid.data(), wifi.credentials.ssid.size(), "Sirokko");
    std::snprintf(wifi.credentials.password.data(), wifi.credentials.password.size(), "secret-password");
    Renderer renderer;
    renderer.update(wifi.screen, wifi.view, wifi.credentials, 10000);
    const auto visible_wifi = picture(renderer);
    auto exited = wifi;
    exited.screen.page = Page::MENU;
    exited.credentials = {};
    renderer.update(exited.screen, exited.view, exited.credentials, 10001);
    const auto transition = picture(renderer);
    Renderer expected;
    auto sanitized_wifi = wifi;
    sanitized_wifi.credentials = {};
    expected.update(sanitized_wifi.screen, sanitized_wifi.view, sanitized_wifi.credentials, 10000);
    assert(transition == picture(expected));
    assert(transition != visible_wifi);
    assert(!renderer.frame().credentials.available);
    for (char value : renderer.frame().credentials.ssid) assert(value == '\0');
    for (char value : renderer.frame().credentials.password) assert(value == '\0');
}

void tests() {
    char value[48]{};
    panel_render::format_temperature(value,sizeof(value),-300);assert(std::strcmp(value,"-0,3 °C")==0);
    panel_render::format_temperature(value,sizeof(value),std::numeric_limits<int32_t>::min());assert(std::strcmp(value,"-2147483,6 °C")==0);
    panel_render::format_duration(value,sizeof(value),80*60000ULL);assert(std::strcmp(value,"1 ч 20 мин")==0);
    panel_render::format_duration(value,sizeof(value),1);assert(std::strcmp(value,"1 с")==0);
    sirokko::SensorSample s{24500,10000,0,true};
    assert(panel_render::sample_fresh(s,12000,2000));assert(!panel_render::sample_fresh(s,12001,2000));
    assert(!panel_render::sample_fresh(s,9999,2000));s.status=1;assert(!panel_render::sample_fresh(s,10000,2000));
    auto f=base("test");Renderer r;r.update(f.screen,f.view,f.credentials,10000);
    const auto initial=picture(r);
    f.screen.page=Page::EDIT;f.screen.editing=true;f.screen.edit_field=EditField::TEMPERATURE;f.screen.edit_value=27000;
    r.update(f.screen,f.view,f.credentials,10010);assert(r.frame().target==26);assert(r.frame().screen.edit_value==27000);
    r.update(f.screen,f.view,f.credentials,10100);assert(r.frame().target>26&&r.frame().target<27);assert(r.animated());
    r.update(f.screen,f.view,f.credentials,10200);assert(r.frame().target==27);
    assert(picture(r)!=initial);
    f.screen.page=Page::MENU;f.screen.editing=false;f.screen.edit_field=EditField::NONE;
    r.update(f.screen,f.view,f.credentials,10210);assert(r.transitioning());
    f.view.control.state=State::FAULT_PURGE;f.view.control.faults=sirokko::FAULT_OVER_TEMPERATURE;
    r.update(f.screen,f.view,f.credentials,10211);assert(r.frame().scene==Scene::FaultCooling);assert(!r.transitioning());
    Renderer fault;fault.update(f.screen,f.view,f.credentials,10211);assert(picture(r)==picture(fault));
    f=base("test");r.reset();r.update(f.screen,f.view,f.credentials,10000);
    f.screen.page=Page::MENU;r.update(f.screen,f.view,f.credentials,10001);assert(r.transitioning());
    f.view.control.state=State::STOP_PURGE;r.update(f.screen,f.view,f.credentials,10002);
    assert(r.frame().scene==Scene::Cooldown&&!r.transitioning());
    f=base("test");r.reset();r.update(f.screen,f.view,f.credentials,10000);
    f.view.sensors.room.valid=false;r.update(f.screen,f.view,f.credentials,10001);
    assert(r.frame().scene==Scene::SensorMissing&&!r.transitioning());
    f.screen.notice=panel_ui::Notice::VIEW_UNAVAILABLE;r.update(f.screen,f.view,f.credentials,10002);
    assert(r.frame().scene==Scene::NoData&&!r.transitioning());
    f=base("test");f.view.control.state=State::IDLE;r.reset();r.update(f.screen,f.view,f.credentials,10000);
    assert(!r.animated());const auto idle=picture(r);r.update(f.screen,f.view,f.credentials,11000);assert(picture(r)==idle);
    f.view.control.state=State::PREPURGE;r.reset();r.update(f.screen,f.view,f.credentials,10000);
    const auto fan=picture(r);r.update(f.screen,f.view,f.credentials,10250);assert(r.animated()&&fan!=picture(r));
    f=base("test");f.screen.page=Page::MENU;r.reset();r.update(f.screen,f.view,f.credentials,10000);
    f.screen.menu_index=1;r.update(f.screen,f.view,f.credentials,10010);r.update(f.screen,f.view,f.credentials,10080);
    assert(r.frame().menu_row>0&&r.frame().menu_row<1);
    r.update(f.screen,f.view,f.credentials,1);assert(!r.transitioning());
    panel_render::DamageTracker damage;
    std::array<uint16_t,panel_render::kWidth*panel_render::kStripeRows> strip{};
    auto change=damage.inspect(strip.data(),0,16);assert(change.x==0&&change.width==320);
    damage.commit(change);assert(damage.inspect(strip.data(),0,16).width==0);
    strip[65]=123;change=damage.inspect(strip.data(),0,16);assert(change.x==64&&change.width==32);
    // Failed or pending transfer must not mark pixels accepted.
    assert(damage.inspect(strip.data(),0,16).width==32);damage.commit(change);assert(damage.inspect(strip.data(),0,16).width==0);
    damage.invalidate();assert(damage.inspect(strip.data(),0,16).width==320);
}
int main(int argc,char** argv) {
    tests();
    heat_wave_tests();
    audit_panel_scene_tests();
    const auto list=fixtures();assert(list.size()==36);
    if(argc>1)std::filesystem::create_directories(argv[1]);
    const auto begin=std::chrono::steady_clock::now();
    size_t pixels=0;
    for(const auto& f:list) {
        Renderer r;r.update(f.screen,f.view,f.credentials,f.view.now_ms);
        assert(std::strcmp(panel_render::scene_id(r.frame().scene),f.id)==0);
        const auto image=picture(r);pixels+=image.size();
        if(argc>1)save(std::filesystem::path(argv[1])/(std::string(f.id)+".ppm"),image);
    }
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    std::printf("PASS: %zu approved scenes; calibration blocks/reasons, Wi-Fi scrub, animation, safety preemption, stale sensors, formatting, stripe canaries and damage commits. %zu pixels in %lld host ms (not MCU FPS).\n",list.size(),pixels,static_cast<long long>(ms));
    if(argc>2) {
        const auto directory=std::filesystem::path(argv[2]);std::filesystem::create_directories(directory);
        Renderer renderer;auto f=base("demo");f.view.control.state=State::IDLE;
        for(int index=0;index<300;++index) {
            const uint64_t t=static_cast<uint64_t>(index)*40,now=10000+t;fresh(f.view,now);
            f.screen={};f.screen.menu_count=8;
            f.screen.menu_rows={MenuItem::MODE,MenuItem::TEMPERATURE,MenuItem::SHUTDOWN_TIMER,MenuItem::CALIBRATION};
            f.view.control.faults=0;
            if(t<1200)f.view.control.state=State::IDLE;
            else if(t<2800)f.view.control.state=State::PREPURGE;
            else if(t<3400)f.view.control.state=State::PREHEAT;
            else if(t<4000)f.view.control.state=State::IGNITION;
            else if(t<4600)f.view.control.state=State::VERIFY;
            else if(t<8400) {
                f.view.control.state=State::RUN;
                if(t>=5200&&t<6200) {
                    f.screen.page=Page::EDIT;f.screen.editing=true;f.screen.edit_field=EditField::TEMPERATURE;
                    f.screen.edit_value=t<5500?27000:t<5800?28000:29000;
                } else if(t>=6200&&t<7400) {
                    f.screen.page=Page::MENU;f.screen.menu_index=static_cast<uint8_t>((t-6200)/350);
                } else if(t>=7400&&t<8100) {
                    f.screen.page=Page::EDIT;f.screen.editing=true;f.screen.edit_field=EditField::SHUTDOWN_TIMER;f.screen.edit_value=90;
                }
            } else if(t<10000) {f.view.control.state=State::STOP_PURGE;}
            else if(t<11200)f.view.control.state=State::IDLE;
            else {f.view.control.state=State::FAULT_PURGE;f.view.control.faults=sirokko::FAULT_OVER_TEMPERATURE;}
            f.view.control.outputs.fuel=f.view.control.state==State::RUN;
            renderer.update(f.screen,f.view,f.credentials,now);
            char filename[32]{};std::snprintf(filename,sizeof(filename),"frame-%04d.ppm",index);
            save(directory/filename,picture(renderer));
        }
        std::puts("Wrote 300 frames from the production C++ renderer; compressed phase durations are explicit demo fixtures.");
    }
    if(argc>3) {
        const auto directory=std::filesystem::path(argv[3]);std::filesystem::create_directories(directory);
        std::ofstream samples(directory/"samples.csv");assert(samples);
        samples<<"frame,time_ms,sample_milli_c,setpoint_milli_c,display_marker_c\n";
        Renderer renderer;auto f=base("measured-approach-demo");f.view.control.outputs.fuel=true;
        for(int index=0;index<375;++index) {
            const uint64_t t=static_cast<uint64_t>(index)*40,now=24000+t;
            // Accelerated input fixture only: never compiled into the device's renderer/control core.
            f.view.sensors.room.milli_c=20000+std::min<int32_t>(6000,static_cast<int32_t>(t/200)*100);
            fresh(f.view,now);renderer.update(f.screen,f.view,{},now);
            char name[32]{};std::snprintf(name,sizeof(name),"frame-%04d.ppm",index);
            save(directory/name,picture(renderer));
            samples<<index<<','<<t<<','<<f.view.sensors.room.milli_c<<','
                   <<f.view.settings.room_setpoint_milli_c<<','<<renderer.frame().current<<'\n';
            assert(renderer.frame().view.sensors.room.milli_c==f.view.sensors.room.milli_c);
        }
        assert(samples.good());assert(renderer.frame().current==26&&!renderer.animated());
        std::puts("Wrote 375 measured-approach fixture frames: 20→26 C, fixed setpoint 26 C; accelerated synthetic inputs, not a heating benchmark.");
    }
}
