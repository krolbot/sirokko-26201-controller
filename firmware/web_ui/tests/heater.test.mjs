import test from "node:test";
import assert from "node:assert/strict";
import { heaterDraft, heaterValidation } from "../src/lib/heater.js";
import { blockedActionLink, calibrationText } from "../src/lib/presentation.js";
import { commandMatchesSnapshot } from "../src/lib/protocol.js";

const limits = {
  preheat_min_ms: 30000,
  preheat_max_ms: 60000,
  postpurge_min_ms: 150000,
  firing_max_ms: 90000,
  timer_max_ms: 604800000,
  temperature_max_milli_c: 600000,
  pwm_max: 4014,
};
// Synthetic protocol values, not physical heater settings.
const parameters = {
  recovery_purge_ms: 100,
  prepurge_ms: 100,
  preheat_ms: 30000,
  ignition_ms: 100,
  verify_ms: 100,
  postpurge_ms: 150000,
  maximum_purge_exit_exhaust_milli_c: 50000,
  maximum_purge_exit_air_milli_c: 50000,
  maximum_exhaust_milli_c: 300000,
  maximum_air_milli_c: 150000,
  minimum_ignition_rise_milli_c: 1000,
  minimum_ignition_air_rise_milli_c: 1000,
  minimum_run_exhaust_milli_c: 40000,
  purge_speed_code: 1500,
  reference_speed_code: 2000,
  exploration_floor_code: 1000,
};
test("only supplied defaults appear; missing fields cannot produce an accepted profile", () => {
  const draft = heaterDraft({
    parameters: { preheat_ms: 30000, postpurge_ms: 150000 },
  });
  assert.equal(draft.preheat_ms, 30);
  assert.equal(draft.postpurge_ms, 150);
  assert.equal(draft.maximum_exhaust_milli_c, "");
  const result = heaterValidation(draft, limits);
  assert.equal(result.valid, false);
  assert.equal(Object.keys(result.errors).length, 14);
  assert.equal(heaterValidation(draft, undefined).valid, false);
});
test("form units round trip without changing any accepted field", () => {
  const result = heaterValidation(heaterDraft({ parameters }), limits);
  assert.equal(result.valid, true);
  assert.deepEqual(result.parameters, parameters);
});
test("coupled limits, precision, missing values and nonfinite numbers block saving", () => {
  const base = heaterDraft({ parameters });
  for (const [field, value] of [
    ["preheat_ms", 29.999],
    ["preheat_ms", 60.001],
    ["postpurge_ms", 149],
    ["prepurge_ms", ""],
    ["prepurge_ms", NaN],
    ["prepurge_ms", Infinity],
    ["prepurge_ms", -1],
    ["prepurge_ms", 0.0001],
    ["prepurge_ms", 604801],
    ["maximum_air_milli_c", 601],
    ["maximum_purge_exit_air_milli_c", 150],
    ["minimum_run_exhaust_milli_c", 300],
    ["minimum_ignition_rise_milli_c", 300],
    ["minimum_ignition_air_rise_milli_c", 150],
    ["reference_speed_code", 4015],
    ["reference_speed_code", 2000.5],
    ["exploration_floor_code", 1999],
  ]) {
    const result = heaterValidation({ ...base, [field]: value }, limits);
    assert.equal(result.valid, false, `${field}: ${value}`);
    assert.ok(result.errors[field], `${field}: ${value}`);
  }
  assert.equal(
    heaterValidation({ ...base, ignition_ms: 60, verify_ms: 31 }, limits).valid,
    false,
  );
  assert.equal(
    heaterValidation({ ...base, ignition_ms: 60, verify_ms: 30 }, limits).valid,
    true,
  );
});
test("blocked action leads to the actual remedy rather than a generic readiness page", () => {
  for (const [code, href] of [
    ["ENVELOPE_MISSING", "#/heater"],
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
    calibrationText({ learned_model: { valid: false } }),
    "Не выполнена",
  );
  assert.equal(
    calibrationText({
      learned_model: { valid: true },
      commissioning: { active: true },
    }),
    "Выполняется",
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
  for (const changes of [
    { boot_id: "2" },
    { storage_ok: false },
    { learned_model: { valid: true } },
    { envelope: { provisioned: false, parameters } },
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
