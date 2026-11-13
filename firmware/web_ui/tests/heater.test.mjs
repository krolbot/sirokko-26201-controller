import test from "node:test";
import assert from "node:assert/strict";
import {
  heaterDraft,
  heaterValidation,
  missingHeaterFields,
  heaterSetupText,
} from "../src/lib/heater.js";
import {
  blockedActionLink,
  calibrationStatus,
} from "../src/lib/presentation.js";
import { commandMatchesSnapshot } from "../src/lib/protocol.js";

const limits = {
  preheat_min_ms: 30000,
  preheat_max_ms: 60000,
  postpurge_min_ms: 150000,
  firing_max_ms: 90000,
  temperature_confirmation_ms: 1000,
  timer_max_ms: 604800000,
  temperature_max_milli_c: 600000,
  pwm_max: 4014,
};
// Synthetic protocol values, not physical heater settings.
const parameters = {
  preheat_ms: 30000,
  ignition_timeout_ms: 5000,
  postpurge_ms: 150000,
  cooling_exhaust_milli_c: 30000,
  maximum_exhaust_milli_c: 300000,
  minimum_run_exhaust_milli_c: 40000,
  purge_speed_code: 1500,
  reference_speed_code: 2000,
  exploration_floor_code: 1000,
};
test("partial parameters can be saved without enabling automatic heating", () => {
  const draft = heaterDraft({
    parameters: {
      preheat_ms: 30000,
      postpurge_ms: 150000,
      cooling_exhaust_milli_c: 40000,
    },
  });
  assert.equal(draft.preheat_ms, 30);
  assert.equal(draft.postpurge_ms, 150);
  assert.equal(draft.maximum_exhaust_milli_c, "");
  const result = heaterValidation(draft, limits);
  assert.equal(result.valid, true);
  assert.equal(result.complete, false);
  assert.equal(result.parameters.minimum_run_exhaust_milli_c, 0);
  assert.equal(heaterValidation(draft, undefined).valid, false);
});
test("form units round trip without changing any accepted field", () => {
  const result = heaterValidation(heaterDraft({ parameters }), limits);
  assert.equal(result.valid, true);
  assert.deepEqual(result.parameters, parameters);
});
test("coupled limits, precision and nonfinite numbers block saving", () => {
  const base = heaterDraft({ parameters });
  for (const [field, value] of [
    ["preheat_ms", 29.999],
    ["preheat_ms", 60.001],
    ["postpurge_ms", 149],
    ["ignition_timeout_ms", NaN],
    ["ignition_timeout_ms", Infinity],
    ["ignition_timeout_ms", -1],
    ["ignition_timeout_ms", 0.0001],
    ["ignition_timeout_ms", 604801],
    ["maximum_exhaust_milli_c", 601],
    ["maximum_exhaust_milli_c", 600],
    ["cooling_exhaust_milli_c", 300],
    ["minimum_run_exhaust_milli_c", 300],
    ["minimum_run_exhaust_milli_c", 30],
    ["ignition_timeout_ms", 1],
    ["reference_speed_code", 4015],
    ["reference_speed_code", 2000.5],
    ["exploration_floor_code", 1999],
  ]) {
    const result = heaterValidation({ ...base, [field]: value }, limits);
    assert.equal(result.valid, false, `${field}: ${value}`);
    assert.ok(result.errors[field], `${field}: ${value}`);
  }
  assert.equal(
    heaterValidation({ ...base, ignition_timeout_ms: 90.001 }, limits).valid,
    false,
  );
  assert.equal(
    heaterValidation({ ...base, ignition_timeout_ms: 90 }, limits).valid,
    true,
  );
});
test("blocked action leads to the actual remedy rather than a generic readiness page", () => {
  for (const [code, href] of [
    ["ENVELOPE_MISSING", "#/heater"],
    ["CALIBRATION_TOO_SLOW", "#/regulation"],
    ["MODEL_UNAVAILABLE", "#/calibration"],
    ["SENSOR_INVALID", "#/diagnostics"],
    ["SENSOR_STALE", "#/diagnostics"],
    ["LOCAL_CONTROLS_UNAVAILABLE", "#/diagnostics"],
  ]) {
    assert.equal(
      blockedActionLink({ commands: { CALIBRATE: { code } } }, "CALIBRATE")
        .href,
      href,
    );
  }
  assert.equal(
    blockedActionLink(
      { commands: { CALIBRATE: { code: "BUSY" } } },
      "CALIBRATE",
    ),
    null,
  );
  assert.equal(
    calibrationStatus({ learned_model: { valid: false } }).summary,
    "Не выполнена",
  );
  assert.equal(
    calibrationStatus({
      learned_model: { valid: true },
      commissioning: { active: true },
    }).summary,
    "Выполняется",
  );
});
test("incomplete profile reports the actual missing fields", () => {
  const partial = { parameters: { ...parameters, exploration_floor_code: 0 } };
  assert.deepEqual(
    missingHeaterFields(partial).map(([key]) => key),
    ["exploration_floor_code"],
  );
  assert.equal(heaterSetupText(partial), "Не заданы: 1");
  assert.equal(heaterValidation(heaterDraft(partial), limits).complete, false);
  assert.equal(heaterSetupText({ parameters, provisioned: true }), "Заполнены");
  assert.equal(
    heaterSetupText({ parameters, provisioned: false }),
    "Проверьте параметры",
  );
});
test("calibration shows ignition and cooldown before analysis and saving", () => {
  const snapshot = {
    state: { code: "PREHEAT", ru: "Накал свечи" },
    commissioning: {
      active: true,
      phase: { code: "STARTING", ru: "Запуск отопителя для калибровки" },
    },
  };
  assert.equal(calibrationStatus(snapshot).title, "Накал свечи");
  snapshot.state = { code: "IGNITION", ru: "Розжиг" };
  assert.equal(calibrationStatus(snapshot).title, "Розжиг");
  snapshot.state = { code: "RUN", ru: "Нагрев" };
  snapshot.commissioning.phase = {
    code: "SETTLE_POINT",
    ru: "Ожидание стабильных температур при сниженном ШИМ",
  };
  assert.equal(
    calibrationStatus(snapshot).title,
    snapshot.commissioning.phase.ru,
  );
  snapshot.state = { code: "STOP_PURGE", ru: "Охлаждение" };
  snapshot.purge = { active: true };
  assert.equal(
    calibrationStatus(snapshot).title,
    "Охлаждение после калибровки",
  );
  snapshot.state = { code: "IDLE", ru: "Остановлен" };
  snapshot.purge.active = false;
  snapshot.commissioning.phase = {
    code: "COMPLETE_PENDING_STOP",
    ru: "Ожидание остановки",
  };
  assert.equal(
    calibrationStatus(snapshot).title,
    "Ожидание сохранения калибровки",
  );
  snapshot.commissioning.phase = {
    code: "PERSIST_COMMITTING",
    ru: "Сохранение калибровки",
  };
  assert.equal(calibrationStatus(snapshot).title, "Сохранение калибровки");
});
test("an older model does not turn a failed repeat into a successful calibration", () => {
  for (const [code, summary] of [
    ["FAILED", "Не завершена"],
    ["CANCELLED", "Отменена"],
    ["PERSIST_FAILED", "Не сохранена"],
  ]) {
    const snapshot = {
      learned_model: { valid: true },
      commissioning: { active: false, phase: { code } },
    };
    assert.equal(calibrationStatus(snapshot).summary, summary);
    assert.notEqual(calibrationStatus(snapshot).icon, "check");
  }
  assert.equal(
    calibrationStatus({
      learned_model: { valid: true },
      commissioning: { phase: { code: "SAVED" } },
    }).icon,
    "check",
  );
});
test("queued configuration is confirmed only by the saved profile and invalidated old model", () => {
  const baseline = {
    boot_id: "1",
    state_version: "1",
    uptime_ms: 100,
    run_id: "0",
  };
  const pending = {
    action: "PROFILE",
    payload: { envelope: parameters },
    baseline,
  };
  const saved = {
    ...baseline,
    state_version: "2",
    uptime_ms: 120,
    storage_ok: true,
    envelope: { provisioned: true, parameters },
    learned_model: { valid: false },
  };
  assert.equal(commandMatchesSnapshot(pending, saved), true);
  const partial = { ...parameters, maximum_exhaust_milli_c: 0 };
  assert.equal(
    commandMatchesSnapshot(
      { ...pending, payload: { envelope: partial } },
      { ...saved, envelope: { provisioned: false, parameters: partial } },
    ),
    true,
  );
  for (const changes of [
    { boot_id: "2" },
    { storage_ok: false },
    { learned_model: { valid: true } },
    {
      envelope: {
        provisioned: true,
        parameters: { ...parameters, preheat_ms: 40000 },
      },
    },
  ])
    assert.equal(
      commandMatchesSnapshot(pending, { ...saved, ...changes }),
      false,
    );
});
