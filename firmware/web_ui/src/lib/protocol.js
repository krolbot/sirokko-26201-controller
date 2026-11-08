export const SETTINGS_EDITABLE_FIELDS = Object.freeze([
  'room_setpoint_milli_c',
  'mode',
  'manual_percent',
  'slew_up_per_s',
  'slew_down_per_s',
]);

export const PROCESS_PROFILE_FIELDS = Object.freeze([
  'qualified',
  'recovery_purge_ms',
  'prepurge_ms',
  'preheat_ms',
  'ignition_ms',
  'verify_ms',
  'postpurge_ms',
  'maximum_purge_exit_exhaust_milli_c',
  'maximum_purge_exit_air_milli_c',
  'maximum_exhaust_milli_c',
  'maximum_air_milli_c',
  'minimum_ignition_rise_milli_c',
  'minimum_run_exhaust_milli_c',
  'purge_speed_code',
  'run_speed_code',
  'calibration_floor_speed_code',
  'minimum_ignition_air_rise_milli_c',
]);

export const ACTIVE_SESSION_STATES = new Set(['PREPURGE', 'PREHEAT', 'IGNITION', 'VERIFY', 'RUN']);
export const TIMER_CONFIRMATION_EARLY_MS = 1_000;
export const TIMER_CONFIRMATION_LATE_MS = 5_000;

export function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

export function isHexString(value) {
  return typeof value === 'string' && /^[0-9a-f]+$/i.test(value);
}

export function requireHexString(value, field) {
  if (!isHexString(value)) throw new TypeError(`${field} must be a non-empty hexadecimal string`);
  return value;
}

export function cloneJson(value) {
  return JSON.parse(JSON.stringify(value));
}

export function makeCommandId(cryptoApi = globalThis.crypto) {
  if (!cryptoApi?.getRandomValues) throw new Error('Браузер не предоставляет криптографический генератор идентификатора команды');
  const words = new Uint32Array(2);
  cryptoApi.getRandomValues(words);
  return [...words].map((word) => word.toString(16).padStart(8, '0')).join('');
}

export function isActiveSession(snapshot) {
  return ACTIVE_SESSION_STATES.has(snapshot?.state?.code);
}

export function hasFault(snapshot) {
  const faults = snapshot?.faults;
  return typeof faults === 'number' ? faults !== 0 : Boolean(faults && faults !== '0');
}

export function formatDuration(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds < 0) return '—';
  const seconds = Math.ceil(milliseconds / 1000);
  const days = Math.floor(seconds / 86_400);
  const hours = Math.floor((seconds % 86_400) / 3_600);
  const minutes = Math.floor((seconds % 3_600) / 60);
  const remainder = seconds % 60;
  return [
    days ? `${days} д` : '',
    hours ? `${hours} ч` : '',
    minutes ? `${minutes} мин` : '',
    `${remainder} с`,
  ].filter(Boolean).join(' ');
}

export function minutesFromMilliseconds(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds < 0) return 0;
  return Math.min(10_080, Math.ceil(milliseconds / 60_000));
}

export function millisecondsFromMinutes(value) {
  const minutes = Number(value);
  if (!Number.isInteger(minutes) || minutes < 0 || minutes > 10_080) {
    throw new RangeError('Введите целое число минут от 0 до 10080');
  }
  return minutes * 60_000;
}

export function validateSettings(settings) {
  if (!isRecord(settings)) throw new TypeError('Нет полного снимка настроек');
  const setpoint = Number(settings.room_setpoint_milli_c);
  const manual = Number(settings.manual_percent);
  const duration = Number(settings.duration_ms);
  const up = Number(settings.slew_up_per_s);
  const down = Number(settings.slew_down_per_s);
  if (!Number.isInteger(setpoint) || setpoint < 15_000 || setpoint > 30_000) throw new RangeError('Уставка: 15…30 °C');
  if (!Number.isSafeInteger(duration) || duration < 0 || duration > 604_800_000) throw new RangeError('Длительность: 0…10080 минут');
  if (!['TEMPERATURE', 'MANUAL'].includes(settings.mode)) throw new RangeError('Неизвестный режим');
  if (!Number.isInteger(manual) || manual < 5 || manual > 100 || manual % 5 !== 0) throw new RangeError('Ручная команда: 5…100 % с шагом 5');
  if (!Number.isFinite(up) || up < 0.0001 || up > 0.005) throw new RangeError('Нарастание: 0,0001…0,005 /с');
  if (!Number.isFinite(down) || down < 0.0001 || down > 0.005) throw new RangeError('Снижение: 0,0001…0,005 /с');
  return settings;
}

export function fullSettingsPayload(currentSettings, edits = {}) {
  if (!isRecord(currentSettings) || !isRecord(edits)) throw new TypeError('Нет полного снимка настроек');
  const payload = { ...cloneJson(currentSettings), ...edits };
  validateSettings(payload);
  return payload;
}

export function validateProcessProfile(profile) {
  if (!isRecord(profile)) throw new TypeError('Профиль должен быть JSON-объектом');
  const keys = Object.keys(profile).sort();
  const expected = [...PROCESS_PROFILE_FIELDS].sort();
  if (keys.length !== expected.length || keys.some((key, index) => key !== expected[index])) {
    throw new TypeError('Профиль должен содержать ровно 17 полей ProcessProfile без добавлений и пропусков');
  }
  if (typeof profile.qualified !== 'boolean') throw new TypeError('qualified должен быть boolean');
  for (const field of PROCESS_PROFILE_FIELDS.filter((field) => field !== 'qualified')) {
    if (!Number.isSafeInteger(profile[field])) throw new TypeError(`${field} должен быть целым числом`);
  }
  return profile;
}

export function deepEqual(left, right) {
  if (left === right) return true;
  if (typeof left !== typeof right || left === null || right === null) return false;
  if (Array.isArray(left) || Array.isArray(right)) {
    return Array.isArray(left) && Array.isArray(right) && left.length === right.length && left.every((item, index) => deepEqual(item, right[index]));
  }
  if (typeof left !== 'object') return false;
  const leftKeys = Object.keys(left).sort();
  const rightKeys = Object.keys(right).sort();
  return leftKeys.length === rightKeys.length && leftKeys.every((key, index) => key === rightKeys[index] && deepEqual(left[key], right[key]));
}

export function timerDeadlineMatches(baseline, snapshot, delayMs) {
  if (!baseline || !snapshot || !Number.isSafeInteger(delayMs) || delayMs < 0) return false;
  const deadline = snapshot?.timer?.deadline_ms;
  if (!Number.isSafeInteger(deadline) || deadline < 0) return false;
  if (delayMs === 0) return deadline === 0;
  if (deadline === 0 || !Number.isSafeInteger(baseline.uptime_ms)) return false;
  const expected = baseline.uptime_ms + delayMs;
  return deadline >= Math.max(0, expected - TIMER_CONFIRMATION_EARLY_MS) &&
    deadline <= expected + TIMER_CONFIRMATION_LATE_MS;
}

export function snapshotIsFresh(baseline, snapshot) {
  return Boolean(
    baseline && snapshot &&
    isHexString(baseline.boot_id) && baseline.boot_id === snapshot.boot_id &&
    isHexString(baseline.state_version) && baseline.state_version !== snapshot.state_version,
  );
}

export function settingsMatch(left, right) {
  if (!isRecord(left) || !isRecord(right)) return false;
  const keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every(key =>
    ['slew_up_per_s', 'slew_down_per_s'].includes(key)
      ? Number.isFinite(left[key]) && Number.isFinite(right[key]) && Math.abs(left[key] - right[key]) <= 1e-10
      : deepEqual(left[key], right[key])); // %.8g double serialization, not a control-value tolerance.
}

export function commandMatchesSnapshot(pending, snapshot) {
  if (!pending || !snapshot || !snapshotIsFresh(pending.baseline, snapshot)) return false;
  const { action, payload, baseline } = pending;
  switch (action) {
    case 'SETTINGS':
      return settingsMatch(payload.settings, snapshot.settings);
    case 'START':
      return isActiveSession(snapshot) && snapshot.run_id !== baseline.run_id;
    case 'TIMER':
      return timerDeadlineMatches(baseline, snapshot, payload.delay_ms);
    case 'CALIBRATE':
      return snapshot?.calibration?.active === true;
    case 'RESET_FAULT':
      return !hasFault(snapshot);
    case 'PROCESS_CONFIG':
      return snapshot?.process?.configured === true && deepEqual(payload.process, snapshot?.process?.profile);
    case 'STOP':
      return ['STOP_PURGE', 'FAULT_PURGE', 'IDLE', 'FAULT'].includes(snapshot?.state?.code) &&
        snapshot.outputs?.fuel === false && snapshot.outputs?.glow === false;
    default:
      return false;
  }
}

export function sensorText(sensor) {
  if (!isRecord(sensor)) return 'Нет данных';
  const value = Number(sensor.milli_c);
  const actual = Number.isFinite(value) ? `${(value / 1000).toFixed(1).replace('.', ',')} °C` : 'Нет значения';
  if (sensor.valid !== true) return `Ошибка датчика (${sensor.status ?? '—'})`;
  return sensor.fresh === true ? actual : `${actual} · устарело`;
}
