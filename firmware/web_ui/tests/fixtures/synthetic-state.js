// SYNTHETIC TEST FIXTURE ONLY. These values are not runtime measurements and
// must never be imported by firmware/web UI source.
export const SYNTHETIC_PROCESS_PROFILE = Object.freeze({
  qualified: true,
  recovery_purge_ms: 30_000,
  prepurge_ms: 20_000,
  preheat_ms: 15_000,
  ignition_ms: 10_000,
  verify_ms: 8_000,
  postpurge_ms: 60_000,
  maximum_purge_exit_exhaust_milli_c: 70_000,
  maximum_purge_exit_air_milli_c: 50_000,
  maximum_exhaust_milli_c: 300_000,
  maximum_air_milli_c: 180_000,
  minimum_ignition_rise_milli_c: 15_000,
  minimum_run_exhaust_milli_c: 80_000,
  purge_speed_code: 220,
  run_speed_code: 640,
  calibration_floor_speed_code: 150,
  minimum_ignition_air_rise_milli_c: 8_000,
});

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
    range: { calibrated: true, minimum_speed_code: 160, maximum_speed_code: 850, working_span_fraction: 0.4 },
    room_excess: false,
    process: { configured: true, physical_tests: false, profile: { ...SYNTHETIC_PROCESS_PROFILE } },
    calibration: { active: false, phase: 'IDLE', phase_ru: 'ожидание', reason: 'NONE', reason_ru: 'нет причины', block: { code: 'NONE', ru: 'Нет' }, target_code: 0, points: 0, elapsed_ms: 0 },
    events: [{ at_ms: 1_000, kind: 'START', kind_ru: 'Запуск принят автоматом', state: 'IDLE', state_ru: 'Ожидание', speed_code: 0, exhaust_milli_c: 50_000, air_milli_c: 25_000, faults: 0 }],
  };
  return mergeSynthetic(base, patch);
}
