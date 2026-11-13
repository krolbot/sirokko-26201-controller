import test from "node:test";
import assert from "node:assert/strict";
import { Measurements } from "../src/lib/measurements.js";
import { trialSuggestion } from "../src/lib/trial-suggestion.js";

function fixture({
  gap = false,
  unstable = false,
  ignitionTooLate = false,
} = {}) {
  const r = new Measurements();
  const s = {
    boot_id: "boot",
    uptime_ms: 0,
    version: "test",
    faults: 0,
    state: { code: "UNCONFIGURED" },
    trial: { active: false },
    envelope: {
      fingerprint: "profile",
      parameters: {
        maximum_speed_code: 4014,
        maximum_exhaust_milli_c: 0,
        cooling_exhaust_milli_c: 40000,
        ignition_timeout_ms: ignitionTooLate ? 11000 : 90000,
      },
      limits: { temperature_confirmation_ms: 1000 },
    },
    outputs: { fuel: false, glow: false, fan_request: false, speed_code: 0 },
    sensors: {},
  };
  const sensor = (c, t) => ({
    milli_c: c * 1000,
    sample_ms: t,
    valid: true,
    fresh: true,
    status: 0,
  });
  const sample = (t, state, fuel, pwm, c, active = true) => {
    s.uptime_ms = t;
    s.state.code = state;
    s.trial.active = active;
    s.outputs = {
      fuel,
      glow: state === "PREHEAT" || state === "IGNITION",
      fan_request: fuel,
      speed_code: pwm,
    };
    s.sensors = {
      exhaust: sensor(c, t),
      air: sensor(20, t),
      room: sensor(20, t),
    };
    r.observe(s, true, t);
  };
  s.sensors.exhaust = sensor(20, 0);
  r.startTrial(s, true, 0);
  for (let t = 1000; t <= 400000; t += 1000) {
    if (gap && t === 180000) {
      r.observe(s, false, t);
      continue;
    }
    if (t < 30000) sample(t, "PREHEAT", false, 0, 20);
    else if (t < 40000) sample(t, "IGNITION", true, 4014, 70);
    else if (t < 60000) sample(t, "RUN", true, 4014, 120 + t / 1000);
    else if (t < 300000)
      sample(
        t,
        "RUN",
        true,
        2000,
        unstable ? 300 - (t - 60000) / 3000 : 200 + (t % 2000 ? 1 : 0),
      );
    else sample(t, "POSTPURGE", false, 4014, 200 - (t - 300000) / 1000);
    if (t === 290000) assert(r.selectPwm(s));
  }
  sample(401000, "UNCONFIGURED", false, 0, 30, false);
  return { r, s, sample };
}

test("first-run recording proposes both settings after cooling without CSV export", () => {
  const { r, s } = fixture();
  const p = trialSuggestion(r, s);
  assert.equal(p.ready, true);
  assert.equal(p.complete, true);
  assert.equal(p.pwm, 2000);
  assert.equal(p.temperatureC, 100); // midpoint of 40 C reference and 160 C confirmed-run minimum
  assert.equal(r.exportedRows, 0);
  assert.equal(s.envelope.parameters.minimum_run_exhaust_milli_c, undefined);
});

test("missing data, drifting selected PWM and late ignition cannot produce a saveable proposal", () => {
  for (const options of [
    { gap: true },
    { unstable: true },
    { ignitionTooLate: true },
  ]) {
    const { r, s } = fixture(options);
    assert.equal(trialSuggestion(r, s).ready, false, JSON.stringify(options));
  }
});

test("faults, another boot or edited profile invalidate recorded recommendations", () => {
  for (const change of [
    (r, s) => {
      s.boot_id = "new";
    },
    (r, s) => {
      s.envelope.fingerprint = "new";
    },
    (r, s, sample) => {
      r.active = true;
      s.faults = 1;
      sample(402000, "FAULT", false, 4014, 30);
    },
  ]) {
    const { r, s, sample } = fixture();
    change(r, s, sample);
    assert.equal(trialSuggestion(r, s).ready, false);
  }
});

test("selection requires confirmed live operation; discarding does not change the heater profile", () => {
  const { r, s } = fixture();
  assert.equal(r.selectPwm(s), false);
  const before = structuredClone(s);
  assert(r.discard());
  assert.equal(r.rows.length, 0);
  assert.equal(r.selectedPwm, null);
  assert.deepEqual(s, before);
  assert.equal(trialSuggestion(r, s).ready, false);
});

test("rejected lower PWM points are excluded, but later drift at the selected PWM is not ignored", async () => {
  const { columns } = await import("../src/lib/measurements.js");
  const idx = (key) => columns.indexOf(key);
  const { r, s } = fixture();
  for (const row of r.rows) {
    const t = row[idx("uptime_ms")];
    if (row[idx("event")] === "sample" && t >= 60000 && t < 70000) {
      row[idx("motor_pwm_code")] = 1000;
      row[idx("exhaust_c")] = 50;
    }
  }
  assert.equal(trialSuggestion(r, s).temperatureC, 100);
  for (const row of r.rows) {
    const t = row[idx("uptime_ms")];
    if (row[idx("event")] === "sample" && t >= 280000 && t < 300000)
      row[idx("exhaust_c")] = 200 - (t - 280000) / 1000;
  }
  assert.equal(trialSuggestion(r, s).ready, false);
});

test("a frozen sensor timestamp or invalid reading cannot stand in for steady measurements", async () => {
  const { columns } = await import("../src/lib/measurements.js");
  const idx = (key) => columns.indexOf(key);
  for (const frozen of [true, false]) {
    const { r, s } = fixture();
    for (const row of r.rows) {
      if (row[idx("event")] === "sample" && row[idx("motor_pwm_code")] === 2000)
        row[idx(frozen ? "exhaust_sample_ms" : "exhaust_c")] = frozen
          ? 60000
          : null;
    }
    assert.equal(trialSuggestion(r, s).ready, false);
  }
});
