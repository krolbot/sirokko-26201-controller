export const SETTINGS_EDITABLE_FIELDS = Object.freeze([
  'room_setpoint_milli_c',
  'mode',
  'manual_percent',
  'slew_up_per_s',
  'slew_down_per_s',
]);


export const ACTIVE_SESSION_STATES = new Set(['PREPURGE', 'PREHEAT', 'IGNITION', 'VERIFY', 'RUN']);
export const TIMER_CONFIRMATION_EARLY_MS = 1_000;
export const TIMER_CONFIRMATION_LATE_MS = 5_000;
const MISSING_COMMISSIONING_BLOCK = Object.freeze({
  code: 'ENVELOPE_MISSING',
  ru: 'Не заданы подтверждённые ограничения запуска',
});

export function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

// A missing field is never permission to start automatic calibration. Older
// snapshots fail closed to the core contract's default block instead.
export function commissioningBlock(snapshot) {
  const block = snapshot?.commissioning?.block;
  if (!isRecord(block) || typeof block.code !== 'string' || block.code.length === 0) return MISSING_COMMISSIONING_BLOCK;
  return {
    code: block.code,
    ru: typeof block.ru === 'string' && block.ru.length > 0 ? block.ru : block.code,
  };
}

export function commissioningAllowed(snapshot) {
  return commissioningBlock(snapshot).code === 'NONE';
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

export const MODEL_REVISION_FIELDS = Object.freeze([
  'expected_revision',
  'minimum_speed_code',
  'maximum_speed_code',
  'slew_up_per_s',
  'slew_down_per_s',
]);

function safeRevision(value) {
  return Number.isSafeInteger(value) && value >= 0 && value <= 0xffff_ffff;
}

function safeSpeedCode(value) {
  return Number.isSafeInteger(value) && value >= 0 && value <= 0xffff;
}

function finiteSlew(value) {
  return Number.isFinite(value) && value >= 0.0001 && value <= 0.005;
}

/**
 * Build the only public model-correction DTO from the currently observed model.
 * A correction may only narrow the learned operating interval and slow either
 * slew; it cannot manufacture an initial profile or relax a learned limit.
 */
export function modelRevisionPayload(currentModel, edits = {}) {
  if (!isRecord(currentModel) || currentModel.valid !== true || !isRecord(currentModel.provenance)) {
    throw new TypeError('Нет сохранённой LearnedOperatingModel для ручной коррекции');
  }
  if (!safeRevision(currentModel.provenance.revision) || !safeSpeedCode(currentModel.minimum_speed_code) ||
      !safeSpeedCode(currentModel.maximum_speed_code) || currentModel.minimum_speed_code >= currentModel.maximum_speed_code ||
      !finiteSlew(currentModel.slew_up_per_s) || !finiteSlew(currentModel.slew_down_per_s)) {
    throw new TypeError('Снимок LearnedOperatingModel имеет недопустимые поля');
  }
  if (!isRecord(edits)) throw new TypeError('Коррекция модели должна быть объектом');
  const revision = {
    expected_revision: currentModel.provenance.revision,
    minimum_speed_code: currentModel.minimum_speed_code,
    maximum_speed_code: currentModel.maximum_speed_code,
    slew_up_per_s: currentModel.slew_up_per_s,
    slew_down_per_s: currentModel.slew_down_per_s,
    ...edits,
  };
  const keys = Object.keys(revision).sort();
  const expected = [...MODEL_REVISION_FIELDS].sort();
  if (keys.length !== expected.length || keys.some((key, index) => key !== expected[index])) {
    throw new TypeError('Коррекция модели должна содержать ровно поля MODEL_REVISION');
  }
  if (!safeRevision(revision.expected_revision) || revision.expected_revision >= 0xffffffff || revision.expected_revision !== currentModel.provenance.revision) {
    throw new RangeError('Ожидаемая ревизия модели не соответствует снимку');
  }
  if (!safeSpeedCode(revision.minimum_speed_code) || !safeSpeedCode(revision.maximum_speed_code) || revision.minimum_speed_code >= revision.maximum_speed_code) {
    throw new RangeError('Границы модели: целые PWM 0…65535, минимум строго меньше максимума');
  }
  if (!finiteSlew(revision.slew_up_per_s) || !finiteSlew(revision.slew_down_per_s)) {
    throw new RangeError('Скорости модели: конечные значения 0,0001…0,005 /с');
  }
  if (revision.minimum_speed_code < currentModel.minimum_speed_code || revision.maximum_speed_code > currentModel.maximum_speed_code ||
      revision.slew_up_per_s > currentModel.slew_up_per_s || revision.slew_down_per_s > currentModel.slew_down_per_s) {
    throw new RangeError('Разрешено только сузить сохранённый диапазон и замедлить нарастание/снижение');
  }
  return revision;
}

export function modelRevisionMatches(requested, observedModel) {
  if (!isRecord(requested) || !isRecord(observedModel) || observedModel.valid !== true || !isRecord(observedModel.provenance)) return false;
  return observedModel.provenance.revision === requested.expected_revision + 1 &&
    observedModel.minimum_speed_code === requested.minimum_speed_code &&
    observedModel.maximum_speed_code === requested.maximum_speed_code &&
    observedModel.slew_up_per_s === requested.slew_up_per_s &&
    observedModel.slew_down_per_s === requested.slew_down_per_s;
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
      return snapshot?.commissioning?.active === true;
    case 'MODEL_REVISION':
      return modelRevisionMatches(payload.revision, snapshot?.learned_model);
    case 'RESET_FAULT':
      return !hasFault(snapshot);

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
