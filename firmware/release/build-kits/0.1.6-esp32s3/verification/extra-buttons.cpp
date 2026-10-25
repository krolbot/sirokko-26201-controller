// Supplemental acceptance probe, compiled with the exact production sources in the kit.
// Peripheral/RTOS boundaries remain host-only. Does not operate hardware.
#define main baseline_suite_entry
#include "sim_tests.cpp"
#undef main
int main() {
    unsigned checked{};
    for (State target : {State::BOOT_SELF_TEST, State::RECOVERY_PURGE, State::PREPURGE,
            State::PREHEAT, State::SPINUP, State::IGNITION_TRIAL, State::FLAME_CONFIRM,
            State::RUN_HIGH, State::RUN_LOW, State::POST_PURGE}) {
        Sim normal; drive_to_state(normal, target); Sim pressed=normal;
        pressed.in.reset_request=true; pressed.in.heat_enable=true;
        normal.tick(); pressed.tick();
        const auto& a=normal.out; const auto& b=pressed.out;
        check(normal.state()==pressed.state() && normal.faults()==pressed.faults(),
              "START/RESET changed a protected running/recovery/purge stage");
        check(a.fuel_command==b.fuel_command && a.glow_command==b.glow_command &&
              a.fuel_permit==b.fuel_permit && a.fan_request==b.fan_request &&
              a.speed_permille==b.speed_permille && a.sw_trip==b.sw_trip && a.arm_ready==b.arm_ready,
              "START/RESET changed stage output commands");
        std::cout << "PASS supplementary START+RESET " << state_name(target) << '\n';
        ++checked;
    }
    std::cout << "SUPPLEMENTARY RESULT " << checked << "/10 stage comparisons; injected inputs, no physical board\n";
}
