// Supplemental DRAFT-06 timing models. Not a contact/PCB measurement.
#define SIROKKO_FOUR_BUTTON_HOST_TEST
#include "runtime_tests.cpp"
int main() {
    unsigned passed{};
    // A is modeled only through independent SAFETY_OK feedback; B is GPIO6.
    for(unsigned scenario=0;scenario<8;++scenario) {
        prepare(); fake_idf::levels[5]=0;
        if(scenario==6) fake_idf::levels[6]=0; // Held before readiness.
        bool ready{}; uint32_t origin{};
        run([&] {
            check(off(g_frame.status.outputs),"RESET timing model enabled fuel/glow");
            if(!ready && ms()>=300) {
                check(g_frame.status.state==State::LOCKOUT && g_outputs.arm_applied(),"no reset readiness");
                ready=true; origin=ms();
            }
            if(!ready) return;
            const uint32_t t=ms()-origin;
            if(scenario==0) { // B only; A never accepted by the latch.
                fake_idf::levels[6]=t<100 || t>=300;
            } else if(scenario==1) { // B has already released when A finally succeeds.
                fake_idf::levels[6]=t<100 || t>=300; fake_idf::levels[5]=t>=500;
            } else if(scenario==2) { // B first, A while B remains held: valid, no fixed order required.
                fake_idf::levels[6]=t<100 || t>=500; fake_idf::levels[5]=t>=300;
            } else if(scenario==3) { // A first, B later: also valid.
                fake_idf::levels[5]=t>=100; fake_idf::levels[6]=t<300 || t>=500;
            } else if(scenario==4) { // Feedback lost before B release must cancel qualification.
                fake_idf::levels[6]=t<100 || t>=500; fake_idf::levels[5]=t>=200 && t<400;
            } else if(scenario==5) { // A never succeeds despite sustained GPIO6.
                fake_idf::levels[6]=t<100;
            } else if(scenario==6) { // Boot-held B cannot qualify when A later succeeds.
                fake_idf::levels[5]=t>=100; fake_idf::levels[6]=t>=500;
            } else { // START held at B edge; releasing START later cannot reuse that RESET.
                fake_idf::levels[7]=t<400?0:1;
                fake_idf::levels[6]=t<100 || t>=600; fake_idf::levels[5]=t>=200;
            }
            if(t>=1000) {
                const bool accepted=scenario==2 || scenario==3;
                check(g_frame.status.state==(accepted?State::WAIT_COOLDOWN:State::LOCKOUT),"wrong RESET pole-skew acceptance");
                throw rt_fake::Done{};
            }
        });
        ++passed; std::cout<<"PASS RESET_skew_scenario_"<<scenario<<'\n';
    }
    for(bool pg:{false,true}) for(bool wd:{false,true}) for(bool thermal:{false,true})
    for(bool arm:{false,true}) for(bool trip:{false,true}) {
        HardwareSafetyLatch latch; latch.power_good=pg; latch.watchdog_ok=wd;
        latch.thermal_ok=thermal; latch.stop_ok=true; // Worst case stuck-LOW GPIO8.
        Outputs out{}; out.arm_ready=arm; out.sw_trip=trip;
        latch.reset_rising_edge(out,true);
        check(latch.safety_ok(out)==(pg && wd && thermal && arm && !trip),"stuck LOW overrode independent barrier");
    }
    std::cout<<"SUPPLEMENTARY RESET RESULT "<<passed<<"/8 temporal scenarios; 32 independent-barrier combinations passed; host only\n";
}
