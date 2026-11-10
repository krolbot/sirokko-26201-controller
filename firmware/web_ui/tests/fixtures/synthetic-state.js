// SYNTHETIC TEST FIXTURE ONLY. These values are not runtime measurements and
// must never be imported by firmware/web UI source.

export function mergeSynthetic(base, patch = {}) {
  if (Array.isArray(base) || Array.isArray(patch)) return patch === undefined ? base : patch;
  if (base && typeof base === 'object' && patch && typeof patch === 'object') {
    const result = { ...base };
    for (const [key, value] of Object.entries(patch)) result[key] = key in base ? mergeSynthetic(base[key], value) : value;
    return result;
  }
  return patch === undefined ? base : patch;
}

export function syntheticState(patch = {}) {
  const base = {
    version: 'test-1.0.0',
    uptime_ms: 1_000,
    state_version: '22',
    boot_id: 'aa',
    run_id: '0',
    state: { code: 'IDLE', ru: 'Ожидание' },
    start_block: { code: 'NONE', ru: 'Нет' },
    faults: 0,
    storage_ok: true,
    sensors: {
      exhaust: { milli_c: 50_000, sample_ms: 900, status: 0, valid: true, fresh: true },
      air: { milli_c: 25_000, sample_ms: 900, status: 0, valid: true, fresh: true },
      room: { milli_c: 21_400, sample_ms: 900, status: 0, valid: true, fresh: true },
    },
    outputs: { fan_request: false, speed_code: 0, glow: false, fuel: false, command_percent: 0 },
    settings: {
      room_setpoint_milli_c: 22_000,
      duration_ms: 0,
      mode: 'TEMPERATURE',
      manual_percent: 45,
      slew_up_per_s: 0.001,
      slew_down_per_s: 0.001,
    },
    timer: { active: false, remaining_ms: 0, deadline_ms: 0, saved_duration_ms: 0 },
    delayed: { armed: false, remaining_ms: 0 },
    purge: { active: false, minimum_remaining_ms: 0, sensors_fresh: true, temperature_ready: true },
    envelope: { provisioned: true, fingerprint: '10' },
    learned_model: { valid: true, minimum_speed_code: 160, maximum_speed_code: 850, slew_up_per_s: 0.001, slew_down_per_s: 0.001, fingerprint: '11', provenance: { safety_envelope_fingerprint: '10', commissioning_started_ms: 1, commissioning_completed_ms: 2, revision: 1 }, measurement: { elapsed_ms: 1, tested_point_count: 2, observed_slew_up_codes_per_s: 1, observed_slew_down_codes_per_s: 1 } },
    commissioning: { active: false, phase: { code: 'IDLE', ru: 'ожидание' }, reason: { code: 'NONE', ru: 'нет причины' }, block: { code: 'NONE', ru: 'Нет' }, target_speed_code: 0, accepted_point_count: 0, elapsed_ms: 0, source_envelope_fingerprint: '10', installed_model_fingerprint: '11', installed_model_revision: 1 },
    events: [{ at_ms: 1_000, kind: 'START', kind_ru: 'Запуск принят автоматом', state: 'IDLE', state_ru: 'Ожидание', speed_code: 0, exhaust_milli_c: 50_000, air_milli_c: 25_000, faults: 0 }],
  };
  return mergeSynthetic(base, patch);
}
