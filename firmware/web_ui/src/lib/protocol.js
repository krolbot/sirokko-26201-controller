export const MINIMUM_SLEW_PER_S = 0.0001;
export const MAXIMUM_SLEW_PER_S = 0.005;
export const MAX_TIMER_MINUTES = 10_080;

export const ACTIVE_SESSION_STATES = new Set(["PREHEAT", "IGNITION", "RUN"]);
export const TIMER_CONFIRMATION_EARLY_MS = 1_000;
export const TIMER_CONFIRMATION_LATE_MS = 5_000;
export function isRecord(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

export function isHexString(value) {
  return typeof value === "string" && /^[0-9a-f]+$/i.test(value);
}

export function requireHexString(value, field) {
  if (!isHexString(value))
    throw new TypeError(`${field} must be a non-empty hexadecimal string`);
  return value;
}

export function cloneJson(value) {
  return JSON.parse(JSON.stringify(value));
}

export function makeCommandId(cryptoApi = globalThis.crypto) {
  if (!cryptoApi?.getRandomValues)
    throw new Error(
      "Браузер не предоставляет криптографический генератор идентификатора команды",
    );
  const words = new Uint32Array(2);
  cryptoApi.getRandomValues(words);
  return [...words].map((word) => word.toString(16).padStart(8, "0")).join("");
}

export function isActiveSession(snapshot) {
  return ACTIVE_SESSION_STATES.has(snapshot?.state?.code);
}

export function hasFault(snapshot) {
  const faults = snapshot?.faults;
  return typeof faults === "number"
    ? faults !== 0
    : Boolean(faults && faults !== "0");
}

export function formatDuration(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds < 0) return "—";
  const seconds = Math.ceil(milliseconds / 1000);
  const days = Math.floor(seconds / 86_400);
  const hours = Math.floor((seconds % 86_400) / 3_600);
  const minutes = Math.floor((seconds % 3_600) / 60);
  const remainder = seconds % 60;
  return [
    days ? `${days} д` : "",
    hours ? `${hours} ч` : "",
    minutes ? `${minutes} мин` : "",
    `${remainder} с`,
  ]
    .filter(Boolean)
    .join(" ");
}

export function minutesFromMilliseconds(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds < 0) return 0;
  return Math.min(MAX_TIMER_MINUTES, Math.ceil(milliseconds / 60_000));
}

export function millisecondsFromMinutes(value) {
  const minutes = Number(value);
  if (
    !Number.isInteger(minutes) ||
    minutes < 0 ||
    minutes > MAX_TIMER_MINUTES
  ) {
    throw new RangeError("Введите целое число минут от 0 до 10080");
  }
  return minutes * 60_000;
}

export function validateSettings(settings) {
  if (!isRecord(settings))
    throw new TypeError("Не получены настройки контроллера");
  const setpoint = Number(settings.room_setpoint_milli_c);
  const manual = Number(settings.manual_percent);
  const duration = Number(settings.duration_ms);
  const up = Number(settings.slew_up_per_s);
  const down = Number(settings.slew_down_per_s);
  if (
    !Number.isInteger(setpoint) ||
    setpoint < 15_000 ||
    setpoint > 30_000 ||
    setpoint % 1_000 !== 0
  )
    throw new RangeError("Температура помещения: 15…30 °C");
  if (
    !Number.isSafeInteger(duration) ||
    duration < 0 ||
    duration > MAX_TIMER_MINUTES * 60_000
  )
    throw new RangeError("Таймер остановки: 0…10080 минут");
  if (!["TEMPERATURE", "MANUAL"].includes(settings.mode))
    throw new RangeError("Неизвестный режим отопления");
  if (
    !Number.isInteger(manual) ||
    manual < 5 ||
    manual > 100 ||
    manual % 5 !== 0
  )
    throw new RangeError("Уровень двигателя: 5…100 % с шагом 5");
  if (
    !Number.isFinite(up) ||
    up < MINIMUM_SLEW_PER_S ||
    up > MAXIMUM_SLEW_PER_S
  )
    throw new RangeError("Темп увеличения ШИМ вне диапазона 0,0001…0,005 /с");
  if (
    !Number.isFinite(down) ||
    down < MINIMUM_SLEW_PER_S ||
    down > MAXIMUM_SLEW_PER_S
  )
    throw new RangeError("Темп уменьшения ШИМ вне диапазона 0,0001…0,005 /с");
  return settings;
}

export function fullSettingsPayload(currentSettings, edits = {}) {
  if (!isRecord(currentSettings) || !isRecord(edits))
    throw new TypeError("Не получены настройки контроллера");
  const payload = { ...cloneJson(currentSettings), ...edits };
  validateSettings(payload);
  return payload;
}

export const MODEL_REVISION_FIELDS = Object.freeze([
  "expected_revision",
  "minimum_speed_code",
  "maximum_speed_code",
  "slew_up_per_s",
  "slew_down_per_s",
]);

function safeRevision(value) {
  return Number.isSafeInteger(value) && value >= 0 && value <= 0xffff_ffff;
}

function safeSpeedCode(value) {
  return Number.isSafeInteger(value) && value >= 0 && value <= 0xffff;
}

function finiteSlew(value) {
  return (
    Number.isFinite(value) &&
    value >= MINIMUM_SLEW_PER_S &&
    value <= MAXIMUM_SLEW_PER_S
  );
}

/**
 * Build the only public model-correction DTO from the currently observed model.
 * A correction may only narrow the learned operating interval and slow either
 * slew; it cannot manufacture an initial profile or relax a learned limit.
 */
export function modelRevisionPayload(currentModel, edits = {}) {
  if (
    !isRecord(currentModel) ||
    currentModel.valid !== true ||
    !isRecord(currentModel.provenance)
  ) {
    throw new TypeError(
      "Нет сохранённой калибровки для изменения диапазона ШИМ",
    );
  }
  if (
    !safeRevision(currentModel.provenance.revision) ||
    !safeSpeedCode(currentModel.minimum_speed_code) ||
    !safeSpeedCode(currentModel.maximum_speed_code) ||
    currentModel.minimum_speed_code >= currentModel.maximum_speed_code ||
    !finiteSlew(currentModel.slew_up_per_s) ||
    !finiteSlew(currentModel.slew_down_per_s)
  ) {
    throw new TypeError("Контроллер передал некорректные данные калибровки");
  }
  if (!isRecord(edits))
    throw new TypeError("Неверный формат изменений калибровки");
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
  if (
    keys.length !== expected.length ||
    keys.some((key, index) => key !== expected[index])
  ) {
    throw new TypeError("Неверный набор параметров для изменения калибровки");
  }
  if (
    !safeRevision(revision.expected_revision) ||
    revision.expected_revision >= 0xffffffff ||
    revision.expected_revision !== currentModel.provenance.revision
  ) {
    throw new RangeError("Калибровка уже изменена. Загрузите текущие значения");
  }
  if (
    !safeSpeedCode(revision.minimum_speed_code) ||
    !safeSpeedCode(revision.maximum_speed_code) ||
    revision.minimum_speed_code >= revision.maximum_speed_code
  ) {
    throw new RangeError(
      "Границы ШИМ: целые числа 0…65535; минимум должен быть меньше максимума",
    );
  }
  if (
    !finiteSlew(revision.slew_up_per_s) ||
    !finiteSlew(revision.slew_down_per_s)
  ) {
    throw new RangeError("Темп изменения ШИМ: 0,0001…0,005 /с");
  }
  if (
    revision.minimum_speed_code < currentModel.minimum_speed_code ||
    revision.maximum_speed_code > currentModel.maximum_speed_code ||
    revision.slew_up_per_s > currentModel.slew_up_per_s ||
    revision.slew_down_per_s > currentModel.slew_down_per_s
  ) {
    throw new RangeError(
      "Можно только сузить диапазон ШИМ и уменьшить темп его изменения",
    );
  }
  return revision;
}

export function modelRevisionMatches(requested, observedModel) {
  if (
    !isRecord(requested) ||
    !isRecord(observedModel) ||
    observedModel.valid !== true ||
    !isRecord(observedModel.provenance)
  )
    return false;
  return (
    observedModel.provenance.revision === requested.expected_revision + 1 &&
    observedModel.minimum_speed_code === requested.minimum_speed_code &&
    observedModel.maximum_speed_code === requested.maximum_speed_code &&
    observedModel.slew_up_per_s === requested.slew_up_per_s &&
    observedModel.slew_down_per_s === requested.slew_down_per_s
  );
}

export function deepEqual(left, right) {
  if (left === right) return true;
  if (typeof left !== typeof right || left === null || right === null)
    return false;
  if (Array.isArray(left) || Array.isArray(right)) {
    return (
      Array.isArray(left) &&
      Array.isArray(right) &&
      left.length === right.length &&
      left.every((item, index) => deepEqual(item, right[index]))
    );
  }
  if (typeof left !== "object") return false;
  const leftKeys = Object.keys(left).sort();
  const rightKeys = Object.keys(right).sort();
  return (
    leftKeys.length === rightKeys.length &&
    leftKeys.every(
      (key, index) =>
        key === rightKeys[index] && deepEqual(left[key], right[key]),
    )
  );
}

export function timerDeadlineMatches(baseline, snapshot, delayMs) {
  if (!baseline || !snapshot || !Number.isSafeInteger(delayMs) || delayMs < 0)
    return false;
  const deadline = snapshot?.timer?.deadline_ms;
  if (!Number.isSafeInteger(deadline) || deadline < 0) return false;
  if (delayMs === 0) return deadline === 0;
  if (deadline === 0 || !Number.isSafeInteger(baseline.uptime_ms)) return false;
  const expected = baseline.uptime_ms + delayMs;
  return (
    deadline >= Math.max(0, expected - TIMER_CONFIRMATION_EARLY_MS) &&
    deadline <= expected + TIMER_CONFIRMATION_LATE_MS
  );
}

export function snapshotIsFresh(baseline, snapshot) {
  return Boolean(
    baseline &&
    snapshot &&
    isHexString(baseline.boot_id) &&
    baseline.boot_id === snapshot.boot_id &&
    isHexString(baseline.state_version) &&
    baseline.state_version !== snapshot.state_version,
  );
}

export function settingsMatch(left, right) {
  if (!isRecord(left) || !isRecord(right)) return false;
  const keys = Object.keys(left);
  return (
    keys.length === Object.keys(right).length &&
    keys.every((key) =>
      ["slew_up_per_s", "slew_down_per_s"].includes(key)
        ? Number.isFinite(left[key]) &&
          Number.isFinite(right[key]) &&
          Math.abs(left[key] - right[key]) <= 1e-10
        : deepEqual(left[key], right[key]),
    )
  ); // %.8g double serialization, not a control-value tolerance.
}

export function manualMinimum(model) {
  return model?.valid && model.maximum_speed_code > 0
    ? Math.ceil((20 * model.minimum_speed_code) / model.maximum_speed_code) * 5
    : 5;
}

export function settingsChangesMatch(baseline, requested, snapshot) {
  const before = baseline?.settings;
  const actual = snapshot?.settings;
  if (!isRecord(before) || !isRecord(requested) || !isRecord(actual))
    return false;
  if (settingsMatch(before, requested)) return settingsMatch(requested, actual);
  const handoff =
    isActiveSession(baseline) &&
    before.mode === "TEMPERATURE" &&
    requested.mode === "MANUAL";
  const expected = {};
  const observed = {};
  for (const key of Object.keys(requested)) {
    if (
      deepEqual(before[key], requested[key]) ||
      (key === "manual_percent" && handoff)
    )
      continue;
    expected[key] =
      key === "manual_percent"
        ? Math.max(requested[key], manualMinimum(snapshot.learned_model))
        : requested[key];
    observed[key] = actual[key];
  }
  return settingsMatch(expected, observed);
}

export function commandMatchesSnapshot(pending, snapshot) {
  if (!pending || !snapshot || !snapshotIsFresh(pending.baseline, snapshot))
    return false;
  const { action, payload, baseline } = pending;
  switch (action) {
    case "SETTINGS":
      return settingsChangesMatch(baseline, payload.settings, snapshot);
    case "START":
      return isActiveSession(snapshot) && snapshot.run_id !== baseline.run_id;
    case "TIMER":
      return (
        isActiveSession(snapshot) &&
        snapshot.run_id === baseline.run_id &&
        timerDeadlineMatches(baseline, snapshot, payload.delay_ms)
      );
    case "CALIBRATE":
      return snapshot?.commissioning?.active === true;
    case "MODEL_REVISION":
      return modelRevisionMatches(payload.revision, snapshot?.learned_model);
    case "PROFILE":
      return (
        snapshot.storage_ok === true &&
        deepEqual(payload.envelope, snapshot.envelope?.parameters) &&
        snapshot.learned_model?.valid === false
      );
    case "RESET_FAULT":
      return snapshot.state?.code === "IDLE" && !hasFault(snapshot);

    case "STOP":
      return (
        ["STOP_PURGE", "FAULT_PURGE", "IDLE", "FAULT", "UNCONFIGURED"].includes(
          snapshot?.state?.code,
        ) &&
        snapshot.outputs?.fuel === false &&
        snapshot.outputs?.glow === false
      );
    default:
      return false;
  }
}
