#pragma once
#include "sirokko/control.hpp"

// The wire fields deliberately exclude provisioned: only a validated, durable save sets it.
template <class Envelope, class Visitor> void envelope_fields(Envelope &p, Visitor visit) {
    visit("recovery_purge_ms", p.recovery_purge_ms);
    visit("prepurge_ms", p.prepurge_ms);
    visit("preheat_ms", p.preheat_ms);
    visit("ignition_ms", p.ignition_ms);
    visit("verify_ms", p.verify_ms);
    visit("postpurge_ms", p.postpurge_ms);
    visit("cooling_exhaust_milli_c", p.cooling_exhaust_milli_c);
    visit("maximum_exhaust_milli_c", p.maximum_exhaust_milli_c);
    visit("maximum_air_milli_c", p.maximum_air_milli_c);
    visit("minimum_ignition_rise_milli_c", p.minimum_ignition_rise_milli_c);
    visit("minimum_run_exhaust_milli_c", p.minimum_run_exhaust_milli_c);
    visit("minimum_ignition_air_rise_milli_c", p.minimum_ignition_air_rise_milli_c);
    visit("purge_speed_code", p.purge_speed_code);
    visit("reference_speed_code", p.reference_speed_code);
    visit("exploration_floor_code", p.exploration_floor_code);
}
