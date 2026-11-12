#include "panel_render.hpp"
#include "panel_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace panel_render {
namespace {
using panel_draw::Align;
using panel_draw::Canvas;
using panel_draw::Color;
using panel_draw::Icon;
constexpr Color kInk=0x292929, kSecond=0x646464, kOrange=0xe58042, kWarm=0xfff0e6;
constexpr Color kBlue=0x537f9d, kBlueBg=0xeef4f8, kRed=0xbe5149, kRedBg=0xfcefeb, kGreen=0x5c846a;
// Same center-line geometry as the approved SVG, not an inside-stroked Pillow arc.
constexpr float kScale=112.0f/145.0f, kCx=160, kCy=127, kRadius=145*kScale;
constexpr float kStroke=24*kScale, kStart=135, kSweep=270;
constexpr float kPi=3.14159265358979323846f;

float eased(uint64_t now,uint64_t start,uint32_t duration) {
    if(now<start) return 1;
    const float t=std::min(1.0f,static_cast<float>(now-start)/duration);
    return t*t*(3-2*t);
}
float motion(float from,float to,uint64_t now,uint64_t start,uint32_t duration) {
    return from+(to-from)*eased(now,start,duration);
}
float angle(float temperature) { return kStart+kSweep*(std::clamp(temperature,15.0f,30.0f)-15)/15; }
float px(float degrees) { return kCx+kRadius*std::cos(degrees*kPi/180); }
float py(float degrees) { return kCy+kRadius*std::sin(degrees*kPi/180); }
Color blend(Color a,Color b,float fraction) {
    const unsigned t=static_cast<unsigned>(std::clamp(fraction,0.0f,1.0f)*255+0.5f);
    Color c=0;
    for(unsigned shift: {0U,8U,16U})
        c|=(((((a>>shift)&255U)*(255-t)+((b>>shift)&255U)*t+127)/255)&255U)<<shift;
    return c;
}
bool gauge_scene(Scene s) {
    return s==Scene::Heat || s==Scene::TimerRunning || s==Scene::Above || s==Scene::EditTemperature;
}
bool manual_scene(Scene s) { return s==Scene::Manual || s==Scene::EditManual; }
bool fan_scene(Scene s) {
    return s==Scene::Prepurge || s==Scene::Recovery || s==Scene::Cooldown ||
           s==Scene::CalCooling || s==Scene::CalCancelled;
}
bool family_equal(Scene a,Scene b) {
    if(gauge_scene(a)&&gauge_scene(b)) return true;
    if(manual_scene(a)&&manual_scene(b)) return true;
    if((a==Scene::Menu||a==Scene::MenuService)&&(b==Scene::Menu||b==Scene::MenuService)) return true;
    if((a==Scene::TimerEdit||a==Scene::TimerOff)&&(b==Scene::TimerEdit||b==Scene::TimerOff)) return true;
    return a==b;
}
float target_value(const panel_ui::Screen& s,const panel_ui::View& v,Scene scene) {
    if(manual_scene(scene)) return s.editing&&s.edit_field==panel_ui::EditField::MANUAL_POWER ?
        static_cast<float>(s.edit_value) : static_cast<float>(v.settings.manual_percent);
    return (s.editing&&s.edit_field==panel_ui::EditField::TEMPERATURE ? s.edit_value :
            v.settings.room_setpoint_milli_c)/1000.0f;
}
float selected_row(const panel_ui::Screen& s) {
    return static_cast<float>(std::clamp(static_cast<int>(s.menu_index)-s.menu_first,0,3));
}
void footer(Canvas& c,const char* first,const char* second=nullptr) {
    if(second) {
        c.text(160,211,first,13,kSecond,Align::Center);
        c.text(160,229,second,13,kSecond,Align::Center);
    } else if(first&&*first) c.text(160,225,first,14,kSecond,Align::Center);
}
void big_value(Canvas& c,const char* value,float baseline,float size,float unit_size,Color color) {
    const float width=panel_draw::text_width(value,size)+2*kScale+panel_draw::text_width("°C",unit_size);
    const float x=(kWidth-width)/2;
    c.text(x,baseline,value,size,color);
    c.text(x+panel_draw::text_width(value,size)+2*kScale,baseline-30*kScale,"°C",unit_size,color);
}
void message(Canvas& c,const Frame& f,const char* kicker,const char* title,const char* detail,
             Icon icon,Color color=kBlue,Color background=kBlueBg,const char* foot="",const char* detail2="") {
    c.text(160,31,kicker,15,color,Align::Center,500);
    c.circle(160,82,32,background);
    const float rotation=icon==Icon::Fan ? static_cast<float>(f.at_ms%kFanPeriodMs)*360/kFanPeriodMs : 0;
    c.icon(icon,160,82,40,color,rotation);
    float size=27;
    if(panel_draw::text_width(title,size)>292) size=24;
    if(panel_draw::text_width(title,size)>292) size=22;
    c.text(160,145,title,size,kInk,Align::Center);
    if(detail&&*detail) {
        float detail_size=16;
        if(panel_draw::text_width(detail,detail_size)>280) detail_size=14;
        if(panel_draw::text_width(detail,detail_size)>280) detail_size=12;
        c.text(160,174,detail,detail_size,kSecond,Align::Center);
    }
    if(detail2&&*detail2) {
        float detail2_size=14;
        if(panel_draw::text_width(detail2,detail2_size)>280) detail2_size=12;
        c.text(160,197,detail2,detail2_size,kSecond,Align::Center);
    }
    footer(c,foot);
}
// Presentation only: one soft pass toward the setpoint, never synthetic temperature progress.
bool heat_pulse_eligible(const Frame& f) {
    return (f.scene==Scene::Heat||f.scene==Scene::TimerRunning)&&
        f.view.control.state==sirokko::State::RUN&&f.view.control.outputs.fuel&&
        !f.view.control.faults&&!f.view.control.commissioning.active&&
        f.view.settings.mode==sirokko::Mode::TEMPERATURE&&
        sample_fresh(f.view.sensors.room,f.at_ms,sirokko::kRoomSensorStaleMs)&&
        f.view.sensors.room.milli_c<f.view.settings.room_setpoint_milli_c&&
        std::isfinite(f.current)&&std::isfinite(f.target)&&angle(f.target)-angle(f.current)>10.0f;
}
float pulse_envelope(float value) {
    const float t=std::clamp(value,0.0f,1.0f);return t*t*(3.0f-2.0f*t);
}
void apply_heat_pulse(uint16_t* pixels,int band_y,int rows,const Frame& f) {
    if(!pixels||rows<1||rows>kHeight||band_y<0||band_y>kHeight-rows||!heat_pulse_eligible(f))return;
    const auto phase=static_cast<uint32_t>(f.at_ms%kHeatPulseCycleMs);
    if(phase>=kHeatPulseTravelMs)return;
    const float start=angle(f.current),end=angle(f.target),gap=end-start;
    const float lo=start+(4*kScale+2.0f)*180.0f/(kPi*kRadius);
    const float hi=end-(12*kScale+2.0f)*180.0f/(kPi*kRadius);
    if(hi<=lo)return;
    const float sigma=std::clamp((hi-lo)*0.18f,1.5f,8.0f);
    const float center=lo-2*sigma+(hi-lo+4*sigma)*static_cast<float>(phase)/kHeatPulseTravelMs;
    const float amplitude=0.78f*pulse_envelope(static_cast<float>(phase)/180.0f)*
        pulse_envelope(static_cast<float>(kHeatPulseTravelMs-phase)/180.0f)*
        std::clamp((gap-10.0f)/17.0f,0.0f,1.0f);
    if(amplitude<=0)return;
    // Bound work to the visible wave sector, including any circle extrema within it.
    const float a0=std::max(start,center-3*sigma),a1=std::min(end,center+3*sigma);
    if(a1<a0)return;
    float left=std::min(px(a0),px(a1)),right=std::max(px(a0),px(a1));
    float top=std::min(py(a0),py(a1)),bottom=std::max(py(a0),py(a1));
    for(float cardinal:{180.0f,270.0f,360.0f})if(cardinal>=a0&&cardinal<=a1) {
        left=std::min(left,px(cardinal));right=std::max(right,px(cardinal));
        top=std::min(top,py(cardinal));bottom=std::max(bottom,py(cardinal));
    }
    const float half=kStroke*0.46f,margin=half+1.0f;
    const int x0=std::clamp(static_cast<int>(std::floor(left-margin)),0,kWidth);
    const int x1=std::clamp(static_cast<int>(std::ceil(right+margin)),0,kWidth);
    const int y0=std::clamp(static_cast<int>(std::floor(top-margin)),band_y,band_y+rows);
    const int y1=std::clamp(static_cast<int>(std::ceil(bottom+margin)),band_y,band_y+rows);
    const float inner2=(kRadius-half)*(kRadius-half),outer2=(kRadius+half)*(kRadius+half);
    const float cx=px(start),cy=py(start),tx=px(end),ty=py(end);
    const float cm=4*kScale+1.0f,tm=12*kScale+1.0f;
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
        const float xx=x+0.5f,yy=y+0.5f,dx=xx-kCx,dy=yy-kCy,q=dx*dx+dy*dy;
        if(q<inner2||q>outer2)continue;
        float a=std::atan2(dy,dx)*180.0f/kPi;if(a<kStart)a+=360.0f;
        if(a<start||a>end)continue;
        const float cdx=xx-cx,cdy=yy-cy,tdx=xx-tx,tdy=yy-ty;
        if(cdx*cdx+cdy*cdy<=cm*cm||tdx*tdx+tdy*tdy<=tm*tm)continue;
        const float along=(a-center)/sigma;if(std::abs(along)>3.0f)continue;
        const float across=(std::sqrt(q)-kRadius)/half;
        const float radial=std::max(0.0f,1.0f-across*across);
        const float strength=amplitude*std::exp(-0.5f*along*along)*radial*radial;
        const int index=(y-band_y)*kWidth+x;
        pixels[index]=panel_draw::rgb565(blend(panel_draw::from565(pixels[index]),0xffe2b8,strength));
    }
}

void draw_gauge(Canvas& c,const Frame& f) {
    const bool edit=f.scene==Scene::EditTemperature;
    const bool fresh=sample_fresh(f.view.sensors.room,f.at_ms,sirokko::kRoomSensorStaleMs);
    const float target_angle=angle(f.target);
    const float current_angle=angle(f.current);
    const Color marker=edit?kOrange:0xf07832;
    c.arc(kCx,kCy,kRadius,kStart,kStart+kSweep,kStroke,0xe1e1e1);
    if(target_angle>kStart) c.arc(kCx,kCy,kRadius,kStart,target_angle,kStroke,0xf9bc9a);
    else c.circle(px(kStart),py(kStart),12*kScale,0xf9bc9a);
    if(fresh&&!edit&&f.current<f.target)
        c.arc(kCx,kCy,kRadius,current_angle,target_angle,kStroke,0xf07832);
    if(fresh) {
        const Color under=f.current<=f.target ? (edit?0xf9bc9a:0xf07832) : 0xe1e1e1;
        c.circle(px(current_angle),py(current_angle),4*kScale,
                 blend(under,f.current<=f.target?0xffffff:0x555555,0.5f));
    }
    c.circle(px(target_angle),py(target_angle),12*kScale,marker);
    c.circle(px(target_angle),py(target_angle),9*kScale,0xffffff);
    if(!edit) c.radial_glow(kCx,kCy,192*kScale,0xf07832,0.15f);
    const char* title=edit?"Новая температура":f.scene==Scene::Above?"Выше заданной":"Нагрев";
    c.text(kCx,kCy-44*kScale,title,18*kScale,edit?0xba6c37:0xd38557,Align::Center,500);
    char value[80]{};
    const int32_t milli=edit?f.screen.edit_value:f.view.settings.room_setpoint_milli_c;
    std::snprintf(value,sizeof(value),"%ld",static_cast<long>(milli/1000));
    big_value(c,value,kCy+20*kScale,57*kScale,18.81f*kScale,edit?0xbc713e:0x272727);
    if(fresh) format_temperature(value,sizeof(value),f.view.sensors.room.milli_c);
    else std::snprintf(value,sizeof(value),"Нет показаний");
    const float size=18*kScale;
    const float x=(kWidth-(panel_draw::text_width(value,size)+23*kScale))/2;
    if(fresh) c.icon(Icon::ThermometerFilled,x+8*kScale,kCy+53*kScale,24*kScale,edit?kSecond:0xdf7e45);
    c.text(x+23*kScale,kCy+60.5f*kScale,value,size,edit?kSecond:0xdf7e45,Align::Left,500);
    if(edit) c.text(160,230,"Нажать — применить",13,kSecond,Align::Center);
    else if(f.view.control.duration_active) {
        char duration[40]{};format_duration(duration,sizeof(duration),f.view.control.duration_remaining_ms);
        std::snprintf(value,sizeof(value),"Стоп через %s",duration);
        c.text(160,230,value,13,kSecond,Align::Center);
    }
}
void draw_idle(Canvas& c,const Frame& f) {
    c.arc(kCx,kCy,kRadius,kStart,kStart+kSweep,kStroke,0xe1e1e1);
    c.text(160,65,"Остановлен",17,kSecond,Align::Center,500);
    char value[40]{};
    const bool fresh=sample_fresh(f.view.sensors.room,f.at_ms,sirokko::kRoomSensorStaleMs);
    if(fresh) {
        format_temperature(value,sizeof(value),f.view.sensors.room.milli_c,false);
        const float nw=panel_draw::text_width(value,57);
        const float x=(kWidth-nw-3-panel_draw::text_width("°C",18.81f))/2;
        c.text(x,136,value,57,kInk);
        c.text(x+nw+3,106,"°C",18.81f,kInk);
    } else c.text(160,136,"Нет показаний",24,kSecond,Align::Center);
    std::snprintf(value,sizeof(value),"Задано %ld °C",static_cast<long>(f.view.settings.room_setpoint_milli_c/1000));
    c.text(160,176,value,16,kSecond,Align::Center);
}
void draw_manual(Canvas& c,const Frame& f) {
    const bool edit=f.scene==Scene::EditManual;
    const float a=kStart+kSweep*(std::clamp(f.target,5.0f,100.0f)-5)/95;
    c.arc(kCx,kCy,kRadius,kStart,kStart+kSweep,kStroke,0xe1e1e1);
    if(a>kStart) c.arc(kCx,kCy,kRadius,kStart,a,kStroke,0xf9bc9a);
    c.circle(px(a),py(a),12*kScale,kOrange);
    c.circle(px(a),py(a),9*kScale,0xffffff);
    c.text(160,kCy-44*kScale,edit?"Новый уровень":"Ручной режим",18*kScale,0xd38557,Align::Center,500);
    char value[80]{};
    const int n=edit?f.screen.edit_value:f.view.settings.manual_percent;
    std::snprintf(value,sizeof(value),"%d",n);
    const float nw=panel_draw::text_width(value,57*kScale), uw=panel_draw::text_width("%",19*kScale);
    const float x=(kWidth-nw-2*kScale-uw)/2;
    c.text(x,kCy+20*kScale,value,57*kScale,edit?0xbc713e:kInk);
    c.text(x+nw+2*kScale,kCy-10*kScale,"%",19*kScale,edit?0xbc713e:kInk);
    c.text(160,kCy+60*kScale,"Заданный уровень",18*kScale,kSecond,Align::Center);
    if(edit) c.text(160,230,"Нажать — применить",13,kSecond,Align::Center);
    else if(f.view.control.duration_active) {
        char duration[40]{};format_duration(duration,sizeof(duration),f.view.control.duration_remaining_ms);
        std::snprintf(value,sizeof(value),"Стоп через %s",duration);
        c.text(160,230,value,13,kSecond,Align::Center);
    }
}
void draw_menu(Canvas& c,const Frame& f) {
    c.text(20,31,"Настройки",22,kInk,Align::Left,500);
    const float highlight=49+f.menu_row*40;
    c.rect(12,highlight,286,37,kWarm,5);
    c.rect(12,highlight+8,3,21,kOrange,1.5f);
    for(int i=0;i<4;++i) {
        const bool selected=static_cast<int>(f.screen.menu_index)-f.screen.menu_first==i;
        const auto item=f.screen.menu_rows[static_cast<size_t>(i)];
        c.text(25,74+i*40,panel_ui::menu_label(item),18,selected?kInk:kSecond,Align::Left,selected?500:400);
    }
    c.icon(Icon::Chevron,281.5f,highlight+18,12,kOrange);
    const float count=std::max(4,static_cast<int>(f.screen.menu_count));
    c.rect(306,49,2,160,0xeeeeee,1);
    c.rect(306,49+160*f.screen.menu_first/count,2,160*4/count,0xbcbcbc,1);
    footer(c,"2 нажатия — назад");
}
void draw_mode(Canvas& c,const Frame& f) {
    c.text(20,31,"Режим",22,kInk,Align::Left,500);
    const int current=f.view.settings.mode==sirokko::Mode::MANUAL?1:0;
    const int focus=std::clamp<int32_t>(f.screen.edit_value,0,1);
    for(int i=0;i<2;++i) {
        const float y=72+i*56;
        if(i==focus) c.rect(12,y-22,296,46,kWarm,5);
        c.circle(32,y+1,8,i==current?kOrange:0xbcbcbc);
        c.circle(32,y+1,6.5f,i==focus?kWarm:0xffffff);
        if(i==current)c.circle(32,y+1,4.5f,kOrange);
        c.text(53,y+7,i==0?"По температуре":"Ручной",20,kInk);
        if(i==current)c.text(291,y+5,"сейчас",12,kSecond,Align::Right);
    }
    footer(c,"Нажать — сохранить","2 нажатия — отмена");
}
void draw_timer(Canvas& c,const Frame& f) {
    c.text(160,31,"Таймер",18,kInk,Align::Center,500);
    c.icon(Icon::Clock,160,78,36,kOrange);
    c.text(160,117,f.screen.edit_value==0?"Автоотключение":"Отключить через",14,kSecond,Align::Center);
    char value[40]{};
    if(f.screen.edit_value==0)std::snprintf(value,sizeof(value),"Без таймера");
    else format_duration(value,sizeof(value),static_cast<uint64_t>(std::max<int32_t>(0,f.screen.edit_value))*60000);
    c.text(160,160,value,34,0xbc713e,Align::Center);
    footer(c,"Нажать — сохранить","2 нажатия — отмена");
}
void draw_diagnostics(Canvas& c,const Frame& f) {
    c.text(20,31,"Температуры",22,kInk,Align::Left,500);
    const char* labels[]={"Комната","Выход воздуха","Выхлоп"};
    const sirokko::SensorSample* samples[]={&f.view.sensors.room,&f.view.sensors.air,&f.view.sensors.exhaust};
    for(int i=0;i<3;++i) {
        const int y=84+i*46;
        c.text(20,static_cast<float>(y),labels[i],17,kSecond);
        char value[40]{};
        if(sample_fresh(*samples[i],f.at_ms,i==0?sirokko::kRoomSensorStaleMs:sirokko::kSensorStaleMs))
            format_temperature(value,sizeof(value),samples[i]->milli_c);
        else std::snprintf(value,sizeof(value),"—");
        c.text(299,static_cast<float>(y),value,22,kInk,Align::Right);
        if(i<2)c.line(20,static_cast<float>(y+15),300,static_cast<float>(y+15),1,0xeeeeee);
    }
    footer(c,"2 нажатия — назад");
}
// Byte-bounded UTF-8 lines, never logs/serializes local credentials.
void wrapped(Canvas& c,const char* source,float y,float normal_size,int maximum_lines) {
    if(panel_draw::text_width(source,normal_size)<=280) {c.text(20,y,source,normal_size,kInk);return;}
    float baseline=y-8;
    for(int row=0;row<maximum_lines && *source;++row) {
        char line[129]{};size_t n=0;
        while(*source && n+5<sizeof(line)) {
            const unsigned char lead=static_cast<unsigned char>(*source);
            size_t bytes=lead<128?1:(lead&0xe0)==0xc0?2:(lead&0xf0)==0xe0?3:4;
            size_t available=0;while(available<bytes && source[available])++available;
            if(available!=bytes)bytes=1;
            std::memcpy(line+n,source,bytes);line[n+bytes]=0;
            if(n && panel_draw::text_width(line,12)>280) {line[n]=0;break;}
            n+=bytes;source+=bytes;
        }
        c.text(20,baseline,line,12,kInk);baseline+=14;
    }
}
void draw_wifi(Canvas& c,const Frame& f) {
    c.text(20,31,"Wi-Fi",22,kInk,Align::Left,500);
    c.icon(Icon::Wifi,277,26,23,kBlue);
    if(!f.credentials.available) {
        c.text(160,120,"Нет данных сети",22,kSecond,Align::Center);
    } else {
        c.text(20,69,"Сеть прибора",14,kSecond);
        wrapped(c,f.credentials.ssid.data(),96,19,2);
        c.text(20,129,"Пароль",14,kSecond);
        wrapped(c,f.credentials.password[0]?f.credentials.password.data():"Без пароля",153,19,3);
        c.text(20,191,"192.168.0.1",24,kBlue);
    }
    footer(c,"2 нажатия — назад");
}
const char* fault_title(uint32_t faults) {
    if(faults&sirokko::FAULT_OVER_TEMPERATURE)return "Перегрев";
    if(faults&sirokko::FAULT_STORAGE)return "Ошибка памяти";
    if(faults&sirokko::FAULT_CONTROL_WATCHDOG)return "Сбой управления";
    if(faults&sirokko::FAULT_EXHAUST)return "Датчик выхлопа";
    if(faults&sirokko::FAULT_AIR)return "Датчик воздуха";
    if(faults&sirokko::FAULT_ROOM)return "Комнатный датчик";
    if(faults&sirokko::FAULT_IGNITION_TIMEOUT)return "Ошибка запуска";
    if(faults&sirokko::FAULT_FLAME_LOST)return "Потеря нагрева";
    if(faults&sirokko::FAULT_COMMISSIONING)return "Ошибка калибровки";
    return "Неисправность";
}
const char* commissioning_reason_detail(sirokko::CommissioningReason reason) {
    switch (reason) {
        case sirokko::CommissioningReason::INVALID_CONFIG: return "Неверная настройка";
        case sirokko::CommissioningReason::ALREADY_ACTIVE: return "Уже выполняется";
        case sirokko::CommissioningReason::BACKDATED_TIME: return "Время данных неверно";
        case sirokko::CommissioningReason::OVERALL_TIMEOUT: return "Не хватило времени";
        case sirokko::CommissioningReason::RUN_CONTEXT_LOST: return "Рабочий режим прерван";
        case sirokko::CommissioningReason::COMMAND_OUT_OF_RANGE: return "Команда вне диапазона";
        case sirokko::CommissioningReason::COMMAND_SLEW_VIOLATION: return "Нарушен темп команды";
        case sirokko::CommissioningReason::COMMAND_NOT_REACHED: return "Команда не достигнута";
        case sirokko::CommissioningReason::SENSOR_INVALID: return "Ошибка датчиков";
        case sirokko::CommissioningReason::STALE_SAMPLE: return "Данные устарели";
        case sirokko::CommissioningReason::SAMPLE_TIMESTAMP_REGRESSION: return "Время данных неверно";
        case sirokko::CommissioningReason::SAMPLE_SKEW: return "Датчики не совпали";
        case sirokko::CommissioningReason::REPEATED_SAMPLE: return "Нет новых данных";
        case sirokko::CommissioningReason::SAMPLE_GAP: return "Пропуск данных";
        case sirokko::CommissioningReason::REFERENCE_TIMEOUT: return "Нет опорных данных";
        case sirokko::CommissioningReason::POINT_TIMEOUT: return "Нет данных точки";
        case sirokko::CommissioningReason::REFERENCE_NOT_REPRODUCIBLE: return "Опорный режим нестабилен";
        case sirokko::CommissioningReason::NO_COMMAND_RESPONSE: return "Нет отклика команды";
        case sirokko::CommissioningReason::POINT_NOT_REPRODUCIBLE: return "Точка нестабильна";
        case sirokko::CommissioningReason::DYNAMIC_NOT_REPRODUCIBLE: return "Переход нестабилен";
        case sirokko::CommissioningReason::INSUFFICIENT_MARGIN: return "Недостаточный запас";
        case sirokko::CommissioningReason::OPERATOR_NEGATIVE: return "Оператор прервал проверку";
        case sirokko::CommissioningReason::EXTERNAL_CANCEL: return "Калибровка отменена";
        case sirokko::CommissioningReason::STORAGE_UNCONFIRMED: return "Запись не подтверждена";
        case sirokko::CommissioningReason::COMPLETE: return "Проверка завершена";
        case sirokko::CommissioningReason::NONE: return "Причина не указана";
    }
    return "Неизвестная причина";
}
const char* fault_reset_footer(const Frame& f) {
    if(f.screen.command_in_flight)return "Сброс: ожидание снимка";
    if(f.screen.notice==panel_ui::Notice::CONFIRMATION_TIMEOUT)return "Сброс не подтверждён";
    if(f.screen.notice==panel_ui::Notice::COMMAND_REJECTED)return "Сброс отклонён";
    return "Нажать энкодер — сбросить";
}
struct CalibrationBlockText { const char* title; const char* detail; const char* foot; };
CalibrationBlockText calibration_block_text(sirokko::Block block) {
    switch(block) {
        case sirokko::Block::ENVELOPE_MISSING:return {"Нет исходных данных","Пределы не подтверждены","Нужна проверка установки"};
        case sirokko::Block::ENVELOPE_HARDWARE_MISMATCH:return {"Установка изменена","Пределы не подходят","Нужна проверка установки"};
        case sirokko::Block::ENVELOPE_INVALID:return {"Неверный диапазон","Проверьте границы","Настройте в браузере"};
        case sirokko::Block::STORAGE_UNCONFIRMED:return {"Ошибка памяти","Запись не подтверждена","Пуск заблокирован"};
        case sirokko::Block::MODEL_UNAVAILABLE:return {"Нужна калибровка","Нет проверенной модели","Запустите калибровку"};
        case sirokko::Block::SENSOR_STALE:return {"Данные устарели","Проверьте датчики","Пуск заблокирован"};
        case sirokko::Block::SENSOR_SKEW:return {"Датчики не совпали","Проверьте измерения","Пуск заблокирован"};
        case sirokko::Block::SENSOR_INVALID:return {"Нет показаний","Проверьте датчики","Проверьте датчики"};
        case sirokko::Block::INITIAL_THERMAL_STATE:return {"Высокая температура","Дождитесь охлаждения","Дождитесь охлаждения"};
        case sirokko::Block::RECOVERY_PURGE:return {"Нужно охлаждение","Дождитесь окончания","Дождитесь охлаждения"};
        case sirokko::Block::FAULT_LATCHED:return {"Устраните причину","Сначала сбросьте аварию","Нажать энкодер — сбросить"};
        case sirokko::Block::BUSY:return {"Операция выполняется","Дождитесь завершения","Дождитесь завершения"};
        case sirokko::Block::MAINTENANCE:return {"Обслуживание","Завершите через браузер","Управление через браузер"};
        case sirokko::Block::STALE_COMMAND:return {"Состояние изменилось","Повторите калибровку","Повторите действие"};
        case sirokko::Block::NONE:return {"Калибровка недоступна","Состояние изменилось","Обновите состояние"};
    }
    return {"Калибровка недоступна","Состояние изменилось","Обновите состояние"};
}
void draw_block(Canvas& c,const Frame& f) {
    const char* title="Пуск заблокирован";const char* detail="Проверьте настройки";
    switch(f.view.control.start_block) {
        case sirokko::Block::ENVELOPE_MISSING:title="Нет исходных данных";detail="Пределы не подтверждены";break;
        case sirokko::Block::SENSOR_INVALID:title="Нет показаний";detail="Проверьте датчики";break;
        case sirokko::Block::INITIAL_THERMAL_STATE:title="Высокая температура";detail="Дождитесь охлаждения";break;
        case sirokko::Block::RECOVERY_PURGE:title="Нужно охлаждение";detail="Запуск недоступен";break;
        case sirokko::Block::FAULT_LATCHED:title="Устраните причину";detail="Ошибка сохранена";break;
        case sirokko::Block::MAINTENANCE:title="Обслуживание";detail="Управление через браузер";break;
        case sirokko::Block::STALE_COMMAND:title="Команда устарела";detail="Повторите действие";break;
        case sirokko::Block::BUSY:detail="Операция выполняется";break;
        case sirokko::Block::ENVELOPE_HARDWARE_MISMATCH:title="Установка изменена";detail="Пределы не подходят";break;
        case sirokko::Block::ENVELOPE_INVALID:title="Неверный диапазон";detail="Проверьте настройки";break;
        case sirokko::Block::STORAGE_UNCONFIRMED:title="Ошибка памяти";detail="Запись не подтверждена";break;
        case sirokko::Block::MODEL_UNAVAILABLE:title="Нужна калибровка";detail="Нет проверенной модели";break;
        case sirokko::Block::SENSOR_STALE:title="Данные устарели";detail="Проверьте датчики";break;
        case sirokko::Block::SENSOR_SKEW:title="Датчики не совпали";detail="Проверьте измерения";break;
        case sirokko::Block::NONE:break;
    }
    if(!f.view.storage_ok){title="Ошибка памяти";detail="Настройки недоступны";}
    message(c,f,"Запуск недоступен",title,detail,Icon::Settings,kOrange,kWarm);
}
void draw(Canvas& c,const Frame& f) {
    c.clear(0xffffff);
    switch(f.scene) {
        case Scene::Heat:case Scene::TimerRunning:case Scene::Above:case Scene::EditTemperature:draw_gauge(c,f);break;
        case Scene::Idle:draw_idle(c,f);break;
        case Scene::Manual:case Scene::EditManual:draw_manual(c,f);break;
        case Scene::Menu:case Scene::MenuService:draw_menu(c,f);break;
        case Scene::Mode:draw_mode(c,f);break;
        case Scene::TimerEdit:case Scene::TimerOff:draw_timer(c,f);break;
        case Scene::Diagnostics:draw_diagnostics(c,f);break;
        case Scene::Wifi:draw_wifi(c,f);break;
        case Scene::Ota:message(c,f,"Обновление","Через браузер","192.168.0.1",Icon::Upload,kBlue,kBlueBg,"После остановки и охлаждения");break;
        case Scene::Prepurge:message(c,f,"Запуск","Продувка","Перед розжигом",Icon::Fan,kBlue,kBlueBg,"Пуск/Стоп — отмена");break;
        case Scene::Preheat:message(c,f,"Запуск","Разогрев свечи","",Icon::Heat,kOrange,kWarm,"Пуск/Стоп — отмена");break;
        case Scene::Ignition:message(c,f,"Запуск","Розжиг","",Icon::Heat,kOrange,kWarm,"Пуск/Стоп — отмена");break;
        case Scene::Verify:message(c,f,"Запуск","Проверка запуска","",Icon::Thermometer,kOrange,kWarm,"Пуск/Стоп — отмена");break;
        case Scene::Cooldown:message(c,f,"Останов","Охлаждение","",Icon::Fan,kBlue,kBlueBg,"Не отключайте питание");break;
        case Scene::Recovery:message(c,f,"После включения","Продувка","Проверка температуры",Icon::Fan,kBlue,kBlueBg,"Не отключайте питание");break;
        case Scene::FaultCooling:
            if(f.view.control.faults&sirokko::FAULT_COMMISSIONING)
                message(c,f,"Авария",fault_title(f.view.control.faults),commissioning_reason_detail(f.view.control.commissioning.reason),
                        Icon::Warning,kRed,kRedBg,"Не отключайте питание","Охлаждение продолжается");
            else
                message(c,f,"Авария",fault_title(f.view.control.faults),"Охлаждение продолжается",Icon::Warning,kRed,kRedBg,"Не отключайте питание");
            break;
        case Scene::FaultLatched:
            if(f.view.control.faults&sirokko::FAULT_COMMISSIONING)
                message(c,f,"Авария",fault_title(f.view.control.faults),commissioning_reason_detail(f.view.control.commissioning.reason),
                        Icon::Warning,kRed,kRedBg,fault_reset_footer(f),"Охлаждение завершено");
            else
                message(c,f,"Авария",fault_title(f.view.control.faults),"Охлаждение завершено",Icon::Warning,kRed,kRedBg,fault_reset_footer(f));
            break;
        case Scene::SensorMissing:message(c,f,f.view.control.state==sirokko::State::IDLE?"Запуск недоступен":"Данные недоступны","Нет показаний","Комнатный датчик",Icon::Thermometer,kRed,kRedBg);break;
        case Scene::Calibrate:message(c,f,"Калибровка","Поиск диапазона","Проверка устойчивости",Icon::Settings,kOrange,kWarm,"Пуск/Стоп — прервать");break;
        case Scene::CalReference:message(c,f,"Калибровка","Исходный режим","Ожидание стабилизации",Icon::Settings,kOrange,kWarm,"Пуск/Стоп — прервать");break;
        case Scene::CalRepeat:message(c,f,"Калибровка","Повторная проверка","",Icon::Settings,kOrange,kWarm,"Пуск/Стоп — прервать");break;
        case Scene::CalCooling:message(c,f,"Калибровка","Охлаждение","Перед сохранением",Icon::Fan,kBlue,kBlueBg,"Не отключайте питание");break;
        case Scene::CalStore:message(c,f,"Калибровка","Сохранение","",Icon::Settings,kOrange,kWarm);break;
        case Scene::CalSaved:message(c,f,"Калибровка","Диапазон сохранён","",Icon::Check,kGreen,0xeff5ef);break;
        case Scene::CalCancelled:message(c,f,"Калибровка прервана","Охлаждение","Результат не сохранён",Icon::Fan,kBlue,kBlueBg,"Не отключайте питание");break;
        case Scene::Maintenance:message(c,f,"Обслуживание","Запуск недоступен","Управление через браузер",Icon::Settings);break;
        case Scene::NoData:message(c,f,"Данные недоступны","Нет состояния","Ожидание данных контроллера",Icon::Warning,kRed,kRedBg);break;
        case Scene::Blocked:draw_block(c,f);break;
        case Scene::Queued:message(c,f,"Настройки","Применение","",Icon::Clock,kSecond,0xf5f5f5);break;
        case Scene::Rejected:message(c,f,"Настройки","Не применено","Состояние изменилось",Icon::Warning,kRed,kRedBg);break;
        case Scene::CommandRejected:
            if(f.screen.notice==panel_ui::Notice::CALIBRATION_BLOCKED) {
                const auto block=calibration_block_text(f.view.control.commissioning.block);
                message(c,f,"Калибровка",block.title,block.detail,Icon::Warning,kRed,kRedBg,block.foot);
            } else if(f.screen.notice==panel_ui::Notice::CONFIRMATION_TIMEOUT)
                message(c,f,"Настройки","Нет подтверждения","Проверьте значение",Icon::Warning,kRed,kRedBg);
            else message(c,f,"Настройки","Команда отклонена","Значение не изменено",Icon::Warning,kRed,kRedBg);
            break;
    }
}
}  // namespace

bool sample_fresh(const sirokko::SensorSample& s,uint64_t now,uint64_t maximum_age) {
    return s.valid&&s.status==0&&now>=s.sample_ms&&now-s.sample_ms<=maximum_age;
}
void format_temperature(char* out,size_t cap,int32_t milli,bool units) {
    if(!out||!cap)return;
    const int64_t signed_value=milli;
    const uint64_t magnitude=static_cast<uint64_t>(signed_value<0?-signed_value:signed_value);
    std::snprintf(out,cap,"%s%llu,%llu%s",signed_value<0?"-":"",
                  static_cast<unsigned long long>(magnitude/1000),
                  static_cast<unsigned long long>((magnitude%1000)/100),units?" °C":"");
}
void format_duration(char* out,size_t cap,uint64_t ms) {
    if(!out||!cap)return;
    ms=std::min(ms,sirokko::kMaximumTimerMs);
    if(ms==0){std::snprintf(out,cap,"0 с");return;}
    if(ms<60000){std::snprintf(out,cap,"%llu с",static_cast<unsigned long long>((ms+999)/1000));return;}
    const uint64_t minutes=(ms+59999)/60000;
    if(minutes<60)std::snprintf(out,cap,"%llu мин",static_cast<unsigned long long>(minutes));
    else if(minutes%60==0)std::snprintf(out,cap,"%llu ч",static_cast<unsigned long long>(minutes/60));
    else std::snprintf(out,cap,"%llu ч %llu мин",static_cast<unsigned long long>(minutes/60),static_cast<unsigned long long>(minutes%60));
}
Scene select_scene(const panel_ui::Screen& s,const panel_ui::View& v,uint64_t now,bool show_saved) {
    using State=sirokko::State;using Page=panel_ui::Page;using Notice=panel_ui::Notice;
    if(s.notice==Notice::VIEW_UNAVAILABLE)return Scene::NoData;
    if(v.control.state==State::FAULT_PURGE)return Scene::FaultCooling;
    if(v.control.state==State::FAULT||v.control.faults)return Scene::FaultLatched;
    if(v.control.state==State::STOP_PURGE) {
        if(v.control.commissioning.phase==sirokko::CommissioningPhase::CANCELLED)return Scene::CalCancelled;
        if(v.control.commissioning.active||v.control.commissioning.phase==sirokko::CommissioningPhase::COMPLETE_PENDING_STOP)return Scene::CalCooling;
        return Scene::Cooldown;
    }
    switch(v.control.state) {
        case State::RECOVERY_PURGE:return Scene::Recovery;
        case State::PREPURGE:return Scene::Prepurge;
        case State::PREHEAT:return Scene::Preheat;
        case State::IGNITION:return Scene::Ignition;
        case State::VERIFY:return Scene::Verify;
        case State::MAINTENANCE:return Scene::Maintenance;
        default:break;
    }
    if(v.control.commissioning.active) {
        if(v.control.state==State::IDLE)return Scene::CalStore;
        const auto phase=v.control.commissioning.phase;
        if(phase==sirokko::CommissioningPhase::SETTLE_REPEAT_POINT||phase==sirokko::CommissioningPhase::WAIT_REPEAT_COMMAND)return Scene::CalRepeat;
        if(phase==sirokko::CommissioningPhase::SETTLE_POINT||phase==sirokko::CommissioningPhase::WAIT_DESCENT_COMMAND)return Scene::Calibrate;
        return Scene::CalReference;
    }
    if(show_saved&&v.control.commissioning.phase==sirokko::CommissioningPhase::SAVED&&s.page==Page::HOME)return Scene::CalSaved;
    if(s.notice==Notice::QUEUED)return Scene::Queued;
    if(s.notice==Notice::EXTERNAL_CHANGE)return Scene::Rejected;
    if(s.notice==Notice::CALIBRATION_BLOCKED||s.notice==Notice::COMMAND_REJECTED||s.notice==Notice::CONFIRMATION_TIMEOUT)
        return Scene::CommandRejected;
    // A stale room measurement must not linger on a live gauge, even during a page transition.
    if(v.control.state==State::RUN&&v.settings.mode==sirokko::Mode::TEMPERATURE&&
       !sample_fresh(v.sensors.room,now,sirokko::kRoomSensorStaleMs))return Scene::SensorMissing;
    switch(s.page) {
        case Page::MENU:return s.menu_first?Scene::MenuService:Scene::Menu;
        case Page::EDIT:
            switch(s.edit_field) {
                case panel_ui::EditField::MODE:return Scene::Mode;
                case panel_ui::EditField::MANUAL_POWER:return Scene::EditManual;
                case panel_ui::EditField::TEMPERATURE:return Scene::EditTemperature;
                case panel_ui::EditField::SHUTDOWN_TIMER:return s.edit_value?Scene::TimerEdit:Scene::TimerOff;
                default:break;
            }
            break;
        case Page::DIAGNOSTICS:return Scene::Diagnostics;
        case Page::WIFI:return Scene::Wifi;
        case Page::OTA:return Scene::Ota;
        case Page::HOME:break;
    }
    if(v.control.state==State::IDLE) {
        if(v.settings.mode==sirokko::Mode::TEMPERATURE&&
           !sample_fresh(v.sensors.room,now,sirokko::kRoomSensorStaleMs))return Scene::SensorMissing;
        if(v.control.start_block!=sirokko::Block::NONE||!v.storage_ok)return Scene::Blocked;
        return Scene::Idle;
    }
    if(v.settings.mode==sirokko::Mode::MANUAL)return Scene::Manual;
    if(v.control.room_excess)return Scene::Above;
    return v.control.duration_active?Scene::TimerRunning:Scene::Heat;
}
const char* scene_id(Scene s) {
    static constexpr const char* names[]={"heat","idle","manual","edit","edit-manual","timer-running","above",
        "prepurge","preheat","ignition","verify","cooldown","recovery","fault-cooling","fault-latched",
        "sensor","calibrate","cal-cooling","cal-saved","cal-reference","cal-repeat","cal-store","cal-cancelled",
        "menu","mode","timer-edit","diagnostics","wifi","ota","menu-service","timer-off","maintenance",
        "no-data","blocked","queued","rejected","command-rejected"};
    const size_t n=static_cast<size_t>(s);
    return n<sizeof(names)/sizeof(*names)?names[n]:"no-data";
}
bool urgent(Scene s) {
    return s==Scene::FaultCooling||s==Scene::FaultLatched||s==Scene::Cooldown||s==Scene::CalCooling||
           s==Scene::CalCancelled||s==Scene::NoData||s==Scene::SensorMissing||s==Scene::Recovery;
}
void Renderer::reset(){*this=Renderer{};}
void Renderer::update(const panel_ui::Screen& s,const panel_ui::View& v,
                      const panel_ui::LocalCredentials& credentials,uint64_t now) {
    if(initialized_&&now<frame_.at_ms)reset();
    const bool saved=v.control.commissioning.phase==sirokko::CommissioningPhase::SAVED;
    if(saved&&!saved_seen_)saved_started_=now;
    saved_seen_=saved;
    const bool show_saved=saved&&now>=saved_started_&&now-saved_started_<1600;
    Frame next{};next.screen=s;next.view=v;next.credentials=credentials;next.at_ms=now;
    next.scene=select_scene(s,v,now,show_saved);
    const float target=target_value(s,v,next.scene), current=v.sensors.room.milli_c/1000.0f, row=selected_row(s);
    const bool immediate=!initialized_||urgent(next.scene)||v.control.boot_id!=frame_.view.control.boot_id;
    const bool family_changed=initialized_&&!family_equal(next.scene,frame_.scene);
    const bool scroll_changed=initialized_&&(next.scene==Scene::Menu||next.scene==Scene::MenuService)&&s.menu_first!=frame_.screen.menu_first;
    const bool leaving_wifi=initialized_&&frame_.screen.page==panel_ui::Page::WIFI&&next.screen.page!=panel_ui::Page::WIFI;
    if(immediate) {
        target_from_=target_to_=target;current_from_=current_to_=current;menu_from_=menu_to_=row;
        target_started_=current_started_=menu_started_=now;
        previous_=next;transition_started_=now;transition_=1;
    } else {
        if(target!=target_to_||family_changed) {
            target_from_=family_changed?target:motion(target_from_,target_to_,now,target_started_,kMarkerMs);
            target_to_=target;target_started_=now;
        }
        if(current!=current_to_||family_changed) {
            current_from_=family_changed?current:motion(current_from_,current_to_,now,current_started_,kMarkerMs);
            current_to_=current;current_started_=now;
        }
        if(row!=menu_to_||scroll_changed||family_changed) {
            menu_from_=(scroll_changed||family_changed)?row:motion(menu_from_,menu_to_,now,menu_started_,kMenuMs);
            menu_to_=row;menu_started_=now;
        }
        if(family_changed||scroll_changed) {
            previous_=frame_;
            if(leaving_wifi) previous_.credentials={};
            transition_started_=now;transition_=0;
        } else if(transition_<1)transition_=eased(now,transition_started_,kTransitionMs);
    }
    next.target=motion(target_from_,target_to_,now,target_started_,kMarkerMs);
    next.current=motion(current_from_,current_to_,now,current_started_,kMarkerMs);
    next.menu_row=motion(menu_from_,menu_to_,now,menu_started_,kMenuMs);
    frame_=next;initialized_=true;
    if(transition_>=1)previous_=Frame{}; // clear former local credentials as soon as the transition ends
}
bool Renderer::draw_band(int y,int rows,uint16_t* pixels,uint16_t* scratch) const {
    if(!initialized_||!pixels||!scratch||pixels==scratch||y<0||rows<1||rows>kStripeRows||y>kHeight-rows)return false;
    Canvas c;c.begin(pixels,y,rows,kWidth);draw(c,frame_);
    apply_heat_pulse(pixels,y,rows,frame_);
    if(transition_<1) {
        Canvas old;old.begin(scratch,y,rows,kWidth);draw(old,previous_);
        apply_heat_pulse(scratch,y,rows,previous_);
        const unsigned t=static_cast<unsigned>(std::clamp(transition_,0.0f,1.0f)*255+0.5f);
        for(int i=0;i<kWidth*rows;++i) {
            const uint16_t a=scratch[i],b=pixels[i];
            const unsigned r=(((a>>11)&31U)*(255-t)+((b>>11)&31U)*t+127)/255;
            const unsigned g=(((a>>5)&63U)*(255-t)+((b>>5)&63U)*t+127)/255;
            const unsigned bl=((a&31U)*(255-t)+(b&31U)*t+127)/255;
            pixels[i]=static_cast<uint16_t>((r<<11)|(g<<5)|bl);
        }
    }
    return true;
}
bool Renderer::animated() const {
    if(!initialized_)return false;
    if(transition_<1||fan_scene(frame_.scene)||heat_pulse_eligible(frame_))return true;
    if((gauge_scene(frame_.scene)||manual_scene(frame_.scene))&&
       (frame_.target!=target_to_||frame_.current!=current_to_))return true;
    return (frame_.scene==Scene::Menu||frame_.scene==Scene::MenuService)&&frame_.menu_row!=menu_to_;
}
}  // namespace panel_render
