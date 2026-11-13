#pragma once
#include "sirokko/control.hpp"

// The wire fields deliberately exclude provisioned: only a validated, durable save sets it.
template <class Envelope, class Visitor> void envelope_fields(Envelope &p, Visitor visit) {
    visit("preheat_ms", p.preheat_ms);
    visit("ignition_timeout_ms", p.ignition_timeout_ms);
    visit("postpurge_ms", p.postpurge_ms);
    visit("cooling_exhaust_milli_c", p.cooling_exhaust_milli_c);
    visit("maximum_exhaust_milli_c", p.maximum_exhaust_milli_c);
    visit("minimum_run_exhaust_milli_c", p.minimum_run_exhaust_milli_c);
    visit("purge_speed_code", p.purge_speed_code);
    visit("reference_speed_code", p.reference_speed_code);
    visit("exploration_floor_code", p.exploration_floor_code);
}
