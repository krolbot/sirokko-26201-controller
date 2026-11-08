// Real production task passes and runtime API, with deterministic ESP-IDF I/O.
// Fixtures are synthetic; this is not a physical NVS or FreeRTOS timing test.
#include "fake_runtime_idf.hpp"
#include "../main/app_main.cpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

bool SensorSuite::begin() {return true;}
void SensorSuite::poll(uint64_t) {}
sirokko::Sensors SensorSuite::readings() const {return {};}
bool panel_begin() {return fake_runtime_idf::panel_ok;}
bool web_start(const char*) {return fake_runtime_idf::web_ok;}

namespace {
using namespace sirokko;
namespace io=fake_runtime_idf;
ProcessProfile synthetic_profile() {
    ProcessProfile p{};p.qualified=true;
    p.recovery_purge_ms=100;p.prepurge_ms=100;p.preheat_ms=100;
    p.ignition_ms=100;p.verify_ms=100;p.postpurge_ms=100;
    p.maximum_purge_exit_exhaust_milli_c=60000;p.maximum_purge_exit_air_milli_c=50000;
    p.maximum_exhaust_milli_c=500000;p.maximum_air_milli_c=250000;
    p.minimum_ignition_rise_milli_c=10000;p.minimum_ignition_air_rise_milli_c=5000;
    p.minimum_run_exhaust_milli_c=110000;
    p.purge_speed_code=1400;p.run_speed_code=1000;p.calibration_floor_speed_code=400;
    return p;
}
Sensors samples(uint64_t now,int exhaust=20000,int air=20000) {
    return {{exhaust,now,0,true},{air,now,0,true},{20000,now,0,true}};
}
void reset_globals() {
    delete g_controller;g_controller=nullptr;io::reset();
    g_gate={};g_output_driver={};g_ota_activation.reset();g_range_commit.reset();g_maintenance_epoch=0;
    g_view={};g_stop=false;g_operator_problem=false;g_exit_maintenance=false;g_output_driver_fault=false;
    g_storage_busy=false;g_storage_faults=0;g_process_pending=false;g_range_pending=false;g_boot_ready=false;
    g_web_ready=false;g_credentials_ready=false;g_control_passes=0;
    std::memset(g_ap_ssid,0,sizeof(g_ap_ssid));std::memset(g_ap_password,0,sizeof(g_ap_password));
    g_ota_activation_queue=g_sensor_queue=g_command_queue=g_settings_queue=g_process_queue=g_storage_jobs=g_store_results=nullptr;
    g_view_mutex=g_gate_mutex=nullptr;
}
void step(ControlWork& work,uint64_t now,bool bad_exhaust=false) {
    io::clock_ms=now;auto sensor=samples(now);sensor.exhaust.valid=!bad_exhaust;
    assert(xQueueOverwrite(g_sensor_queue,&sensor)==pdTRUE);control_pass(work);
}
void next(ControlWork& work,bool bad_exhaust=false) {step(work,io::clock_ms+20,bad_exhaust);}
bool submit(Action action,const Settings* settings=nullptr) {
    Grant grant{};assert(runtime_issue(action,123,&grant));return runtime_submit(grant,action,0,settings);
}
void configure(ControlWork& work) {
    reset_globals();app_main();
    assert(g_controller->update_process_profile(synthetic_profile(),0));
    g_boot_ready=true;step(work,1000);assert(g_view.control.state==State::IDLE);
}
void reboot() {
    auto flash=io::flash;reset_globals();io::flash=std::move(flash);app_main();
}
OperatingRange stored_range() {
    OperatingRange range{};assert(load_record("range_v2",kRangeMagic,&range));return range;
}
bool event(const char* kind) {
    for(size_t i=0;i<g_controller->event_count();++i)
        if(!std::strcmp(g_controller->events()[i].kind,kind))return true;
    return false;
}
void fresh_boot_and_credentials() {
    reset_globals();app_main();
    assert(g_controller && !g_controller->process_profile().qualified && storage_healthy());
    ControlWork work{};step(work,1000);
    assert(g_view.control.state==State::RECOVERY_PURGE);
    assert(g_view.control.start_block==Block::PROFILE_NOT_QUALIFIED);
    assert(!g_view.control.outputs.fuel && !g_view.control.outputs.glow);
    assert(io::gpio[board::kFuel]==0 && io::gpio[board::kGlow]==0);
    char ssid[33]{},password[21]{};
    assert(runtime_network_credentials(ssid,sizeof(ssid),password,sizeof(password)));
    g_web_ready=false;assert(!runtime_network_credentials(ssid,sizeof(ssid),password,sizeof(password)));
    g_web_ready=true;g_credentials_ready=false;
    assert(!runtime_network_credentials(ssid,sizeof(ssid),password,sizeof(password)));
    std::memset(password,0,sizeof(password));std::memset(ssid,0,sizeof(ssid));
    std::cout<<"PASS actual app_main: empty NVS safe boot and AP+HTTP credential readiness\n";
}
void settings_start_barriers() {
    ControlWork control{};configure(control);StorageWork storage{};
    Settings settings{};settings.mode=Mode::MANUAL;
    assert(submit(Action::SETTINGS,&settings));next(control);
    step(control,io::clock_ms+1500);assert(g_storage_busy && !g_view.control.outputs.fuel);
    bool start_accepted=true;unsigned passes=0;
    io::before_commit=[&](const std::string& key) {
        if(key!="sirokko/settings_v2")return;
        start_accepted=submit(Action::START);runtime_stop();
        for(unsigned i=0;i<40;++i) {next(control);++passes;assert(!g_view.control.outputs.fuel && g_storage_busy);}
    };
    storage_pass(storage);io::before_commit={};
    assert(!start_accepted && passes==40 && g_storage_busy);
    next(control);assert(!g_storage_busy && g_view.control.state==State::IDLE);
    assert(submit(Action::START));next(control);assert(g_view.control.state==State::PREPURGE);
    assert(io::maximum_queue_wait==0);

    // START queued before the grant, behind another consumed queue entry.
    ControlWork queued{};configure(queued);StorageWork worker{};
    assert(submit(Action::SETTINGS,&settings));next(queued);
    RuntimeCommand dummy{};assert(xQueueSend(g_command_queue,&dummy,0)==pdTRUE);
    assert(submit(Action::START));step(queued,io::clock_ms+1500);
    assert(g_storage_busy);next(queued);assert(g_view.control.state==State::IDLE);
    storage_pass(worker);next(queued);assert(!g_storage_busy);
    std::cout<<"PASS actual Settings/START: API and queued-command barriers, STOP during write, 40 continuing control passes, zero blocking queue waits\n";
}
void settings_revision_and_ack_backpressure() {
    ControlWork control{};configure(control);StorageWork storage{};
    Settings first{};first.mode=Mode::MANUAL;first.manual_percent=90;
    assert(submit(Action::SETTINGS,&first));next(control);step(control,io::clock_ms+1500);
    Settings second=first;second.manual_percent=80;
    io::before_commit=[&](const std::string& key) {
        if(key=="sirokko/settings_v2") {assert(submit(Action::SETTINGS,&second));next(control);}
    };
    storage_pass(storage);io::before_commit={};next(control);
    Settings read{};assert(load_record("settings_v2",kSettingsMagic,&read) && read.manual_percent==90);
    assert(control.settings_pending && !g_storage_busy);
    step(control,io::clock_ms+1500);storage_pass(storage);next(control);
    assert(load_record("settings_v2",kSettingsMagic,&read) && read.manual_percent==80);

    assert(submit(Action::SETTINGS,&first));next(control);step(control,io::clock_ms+1500);
    StoreResult stale{control.inflight.id+1,StoreKind::SETTINGS,true};
    assert(xQueueSend(g_store_results,&stale,0)==pdTRUE);
    storage_pass(storage);assert(storage.result_waiting && g_storage_busy);
    next(control);assert(g_storage_busy && (g_storage_faults.load()&STORAGE_INTERNAL));
    storage_pass(storage);next(control);
    assert(!g_storage_busy && !storage.result_waiting && io::maximum_queue_wait==0);
    std::cout<<"PASS Settings revisions preserved; full result queue retries nonblocking; stale ACK cannot release a live grant\n";
}

struct Prepared {
    Controller controller{synthetic_profile(),{},7};
    uint64_t now{};
    OperatingRange candidate{};
};
const Prepared& calibrated_pending_fixture() {
    static const Prepared result=[] {
        Prepared p;
        OperatingRange old{true,700,1000,0.005,0.005,process_fingerprint(synthetic_profile())};
        assert(p.controller.install_range(old));
        auto tick=[&](uint64_t t,int exh=20000,int air=20000,bool start=false) {
            TickInput i{};i.now_ms=t;i.sensors=samples(t,exh,air);i.start_calibration=start;return p.controller.tick(i);
        };
        assert(tick(1000).state==State::IDLE);
        assert(tick(1001,20000,20000,true).state==State::PREPURGE);
        tick(1101);tick(1201);tick(1301,270000,70000);
        auto state=tick(1401,270000,70000);assert(state.state==State::RUN);
        double exh=270000,air=70000;TickInput input{};input.sensors=samples(1401,270000,70000);
        for(uint64_t t=1421;t<21600000;t+=20) {
            const double te=state.outputs.fuel?160000+110.0*state.outputs.speed_code:20000;
            const double ta=state.outputs.fuel?70000+15.0*(1000-state.outputs.speed_code):20000;
            exh+=(te-exh)*(-std::expm1(-0.02/5.0));air+=(ta-air)*(-std::expm1(-0.02/5.0));
            input.now_ms=t;
            if(t-input.sensors.exhaust.sample_ms>=200) input.sensors=samples(t,std::lround(exh),std::lround(air));
            state=p.controller.tick(input);assert(state.state!=State::FAULT && state.state!=State::FAULT_PURGE);
            if(p.controller.pending_calibration_range(&p.candidate)) {p.now=t;break;}
        }
        assert(p.now && p.candidate.valid && p.candidate.minimum_speed_code<700);
        assert(p.controller.operating_range().minimum_speed_code==700);
        return p;
    }();
    return result;
}
void pending(ControlWork& work) {
    const auto& fixture=calibrated_pending_fixture();reset_globals();app_main();
    assert(save_record("process_v2",kProcessMagic,synthetic_profile()));
    assert(save_record("range_v2",kRangeMagic,fixture.controller.operating_range()));
    delete g_controller;g_controller=new Controller(fixture.controller);g_boot_ready=true;
    step(work,fixture.now);
    assert(g_storage_busy && g_range_pending && work.inflight.kind==StoreKind::RANGE_STAGE);
    assert(!std::strcmp(g_view.control.calibration_phase,"PERSIST_STAGING"));
    assert(stored_range().minimum_speed_code==700);
}
void sealed(ControlWork& work,StorageWork& storage) {
    pending(work);storage_pass(storage);assert(stored_range().minimum_speed_code==700);
    next(work);assert(g_range_commit.claimed() && g_storage_busy);
    assert(!std::strcmp(g_view.control.calibration_phase,"PERSIST_COMMITTING"));
    assert(g_controller->operating_range().minimum_speed_code==700);
}
void assert_boot_range(uint16_t minimum) {
    reboot();assert(storage_healthy());assert(g_controller->operating_range().valid);
    assert(g_controller->operating_range().minimum_speed_code==minimum);
    assert(!g_controller->snapshot(0).outputs.fuel && !g_controller->snapshot(0).outputs.glow);
}
void range_normal_and_precommit_cancellations() {
    const auto minimum=calibrated_pending_fixture().candidate.minimum_speed_code;
    {ControlWork c{};StorageWork s{};sealed(c,s);storage_pass(s);
     assert(g_controller->operating_range().minimum_speed_code==700);next(c);
     assert(!g_storage_busy && !g_range_pending && !std::strcmp(g_view.control.calibration_phase,"SAVED"));
     assert(g_controller->operating_range().minimum_speed_code==minimum);assert_boot_range(minimum);}
    for(unsigned point=0;point<5;++point) {
        ControlWork c{};StorageWork s{};pending(c);
        if(point==0) {runtime_stop();next(c);storage_pass(s);next(c);}
        if(point==1) {
            io::before_commit=[&](const std::string& key) {if(key=="sirokko/range_stage3") {runtime_stop(true);next(c);}};
            storage_pass(s);io::before_commit={};next(c);assert(event("OPERATOR_COMBUSTION_PROBLEM"));
        }
        if(point>=2) {
            storage_pass(s);
            if(point==2)runtime_stop();
            if(point==4)runtime_stop(true);
            next(c,point>=3);
            if(point==4)assert(event("OPERATOR_COMBUSTION_PROBLEM"));
        }
        assert(!g_storage_busy && !g_range_pending && !g_controller->snapshot(io::clock_ms).calibration_active);
        assert(stored_range().minimum_speed_code==700 && g_controller->operating_range().minimum_speed_code==700);
        assert_boot_range(700);
    }
    std::cout<<"PASS actual calibration storage: stage is non-active, verified activation only; STOP before/during/after stage ACK and simultaneous operator+sensor fault preserve old range across reboot\n";
}
void range_after_boundary() {
    const auto minimum=calibrated_pending_fixture().candidate.minimum_speed_code;
    {ControlWork c{};StorageWork s{};sealed(c,s);runtime_stop(true);next(c);
     assert(g_range_commit.claimed() && g_controller->snapshot(io::clock_ms).calibration_active);
     assert(event("OPERATOR_COMBUSTION_PROBLEM"));storage_pass(s);next(c);
     assert(!std::strcmp(g_view.control.calibration_phase,"SAVED"));assert_boot_range(minimum);}
    {ControlWork c{};StorageWork s{};sealed(c,s);g_output_driver_fault=true;next(c,true);storage_pass(s);next(c,true);
     assert(g_storage_busy && g_view.control.state==State::FAULT_PURGE);
     assert(g_controller->operating_range().minimum_speed_code==700);
     for(unsigned i=0;i<10 && g_storage_busy;++i)next(c);
     assert(!g_storage_busy && g_view.control.state==State::FAULT && g_view.control.faults);
     assert(!g_view.control.outputs.fuel && g_controller->operating_range().minimum_speed_code==minimum);
     assert_boot_range(minimum);}
    std::cout<<"PASS after range claim: STOP still de-energizes, does not falsely cancel committed write; sensor fault keeps purge and defers RAM acceptance until FAULT\n";
}
void storage_failures_and_unknown_outcome() {
    {ControlWork c{};StorageWork s{};pending(c);io::nvs_set_error=ESP_FAIL;storage_pass(s);io::nvs_set_error=ESP_OK;next(c);
     assert(!g_storage_busy && !storage_healthy() && stored_range().minimum_speed_code==700);assert_boot_range(700);}
    {ControlWork c{};StorageWork s{};sealed(c,s);io::nvs_commit_error=ESP_FAIL;storage_pass(s);io::nvs_commit_error=ESP_OK;next(c);
     assert(!g_storage_busy && !storage_healthy());assert_boot_range(700);}
    {ControlWork c{};StorageWork s{};sealed(c,s);
     io::after_commit=[](const std::string& key){if(key=="sirokko/range_v2")io::nvs_get_error=ESP_FAIL;};
     storage_pass(s);io::after_commit={};io::nvs_get_error=ESP_OK;next(c);
     assert(!g_storage_busy && !storage_healthy() && !g_view.control.calibration_active);
     assert(!std::strcmp(g_view.control.calibration_phase,"PERSIST_FAILED"));
     assert(!std::strcmp(g_view.control.calibration_reason,"STORAGE_UNCONFIRMED"));
     assert(g_controller->operating_range().minimum_speed_code==700);
     assert_boot_range(calibrated_pending_fixture().candidate.minimum_speed_code);}
    std::cout<<"PASS staging/set/commit/readback failures: old RAM range retained, fault latched, ambiguous durable outcome not labelled cancelled or saved\n";
}
struct PowerCut {};
void power_cut_boundaries() {
    const auto minimum=calibrated_pending_fixture().candidate.minimum_speed_code;
    for(unsigned point=0;point<5;++point) {
        ControlWork c{};StorageWork s{};
        if(point<2)pending(c);else sealed(c,s);
        const auto key=point<2?"sirokko/range_stage3":"sirokko/range_v2";
        const auto cut=[key](const std::string& actual){if(actual==key)throw PowerCut{};};
        if(point==0 || point==2)io::before_commit=cut;
        else if(point==1 || point==3)io::after_commit=cut;
        else {io::durable_at_set=true;io::after_set=cut;}
        bool interrupted=false;try {storage_pass(s);}catch(const PowerCut&){interrupted=true;}
        assert(interrupted);assert_boot_range(point>=3?minimum:700);
    }
    std::cout<<"PASS reset injection at five real storage boundaries, including set-already-durable backend: boot uses only intact old or exact activated candidate\n";
}
void corruption_and_explicit_profile_repair() {
    ControlWork c{};configure(c);
    assert(save_record("process_v2",kProcessMagic,synthetic_profile()));
    assert(save_record("settings_v2",kSettingsMagic,Settings{}));
    auto flash=io::flash;
    io::flash["sirokko/settings_v2"][0]^=1;reboot();
    assert(!storage_healthy() && (g_storage_faults.load()&STORAGE_SETTINGS));
    step(c,1000);assert(g_view.control.faults&FAULT_STORAGE);
    assert(save_record("process_v2",kProcessMagic,synthetic_profile()));assert(!storage_healthy());
    assert(save_record("settings_v2",kSettingsMagic,Settings{}));assert(storage_healthy());
    reset_globals();io::flash=flash;io::flash["sirokko/process_v2"][0]^=1;app_main();
    assert(!storage_healthy() && !g_controller->process_profile().qualified);

    ControlWork r{};StorageWork s{};sealed(r,s);storage_pass(s);next(r);
    io::flash["sirokko/range_stage3"][0]^=1;reboot();
    assert(!storage_healthy() && !g_controller->operating_range().valid);
    ControlWork recovery{};step(recovery,1000);
    // The explicit process-config command invalidates obsolete/corrupt calibration
    // records; unrelated valid settings are not erased and no erase_flash occurs.
    Grant grant{};assert(runtime_issue(Action::PROCESS_CONFIG,456,&grant));
    assert(runtime_configure_process(grant,synthetic_profile()));next(recovery);
    StorageWork repair{};storage_pass(repair);next(recovery);
    assert(storage_healthy() && !g_process_pending && !io::flash.count("sirokko/range_stage3") && !io::flash.count("sirokko/range_v2"));
    reboot();assert(storage_healthy() && !g_controller->operating_range().valid);
    std::cout<<"PASS boot distinguishes missing/corrupt records, record-specific diagnostics, explicit profile reconfiguration clears obsolete calibration without flash erase\n";
}
void queued_ota(ControlWork& c) {
    configure(c);assert(submit(Action::OTA));next(c);
    assert(g_view.control.state==State::MAINTENANCE);
    const auto lease=runtime_maintenance_lease();assert(lease);
    assert(runtime_activate_ota(lease,esp_ota_get_next_update_partition(nullptr)));next(c);
}
void ota_claim_and_cancellation() {
    for(unsigned which=0;which<3;++which) {
        ControlWork c{};queued_ota(c);
        if(which==0)runtime_stop();else if(which==1)runtime_exit_maintenance();
        step(c,io::clock_ms+200,which==2);
        assert(!io::boot_select_count && !io::restart_count && !runtime_maintenance_lease());
    }
    {ControlWork c{};queued_ota(c);
     io::before_boot_select=[] {runtime_stop();assert(g_ota_activation.commit_started());assert(!g_ota_activation.revoke_precommit());};
     step(c,io::clock_ms+200);io::before_boot_select={};
     assert(io::boot_select_count==1 && io::restart_count==1 && g_stop && !runtime_maintenance_lease());}
    {ControlWork c{};queued_ota(c);io::boot_select_error=ESP_FAIL;step(c,io::clock_ms+200);
     assert(io::boot_select_count==1 && !io::restart_count && !storage_healthy());}
    {ControlWork c{};configure(c);assert(submit(Action::OTA));next(c);
     int invalid_partition{};assert(runtime_activate_ota(runtime_maintenance_lease(),&invalid_partition));next(c);step(c,io::clock_ms+200);
     assert(!io::boot_select_count && !io::restart_count && !storage_healthy());}
    std::cout<<"PASS actual OTA owner: STOP/exit/fault before claim prevent boot write; STOP after claim is not false cancellation; invalid slot and ESP failure never reboot\n";
}
}
int main() {
    fresh_boot_and_credentials();settings_start_barriers();settings_revision_and_ack_backpressure();
    range_normal_and_precommit_cancellations();range_after_boundary();storage_failures_and_unknown_outcome();
    power_cut_boundaries();corruption_and_explicit_profile_repair();ota_claim_and_cancellation();
    delete g_controller;g_controller=nullptr;
}
