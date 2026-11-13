import test from "node:test";
import assert from "node:assert/strict";
import {
  heaterDraft,
  heaterValidation,
  missingHeaterFields,
  heaterSetupText,
} from "../src/lib/heater.js";
import { blockedActionLink } from "../src/lib/presentation.js";
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
  maximum_speed_code: 2000,
  minimum_speed_code: 1000,
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
    ["maximum_speed_code", 4015],
    ["maximum_speed_code", 2000.5],
    ["minimum_speed_code", 2001],
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
    ["SENSOR_INVALID", "#/diagnostics"],
    ["SENSOR_STALE", "#/diagnostics"],
    ["LOCAL_CONTROLS_UNAVAILABLE", "#/diagnostics"],
  ]) {
    assert.equal(
      blockedActionLink({ commands: { START: { code } } }, "START").href,
      href,
    );
  }
  assert.equal(
    blockedActionLink({ commands: { START: { code: "BUSY" } } }, "START"),
    null,
  );
});
test("incomplete profile reports the actual missing fields", () => {
  const partial = { parameters: { ...parameters, minimum_speed_code: 0 } };
  assert.deepEqual(
    missingHeaterFields(partial).map(([key]) => key),
    ["minimum_speed_code"],
  );
  assert.equal(heaterSetupText(partial), "Не заданы: 1");
  assert.equal(heaterValidation(heaterDraft(partial), limits).complete, false);
  assert.equal(heaterSetupText({ parameters, provisioned: true }), "Заполнены");
  assert.equal(
    heaterSetupText({ parameters, provisioned: false }),
    "Проверьте параметры",
  );
});
test("queued configuration is confirmed only by the saved profile", () => {
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

test("fixed command and one-code interval are accepted consistently with the controller", () => {
  for (const minimum_speed_code of [1999, 2000]) {
    const result = heaterValidation(
      heaterDraft({ parameters: { ...parameters, minimum_speed_code } }),
      limits,
    );
    assert.equal(result.valid, true);
    assert.equal(result.complete, true);
  }
});
