import test from "node:test";
import assert from "node:assert/strict";
import { Measurements } from "../src/lib/measurements.js";

const time = 1_800_000_000_000;
function snapshot(uptime = 1000) {
  const sensor = (milli_c) => ({
    milli_c,
    sample_ms: uptime - 100,
    status: 0,
    valid: true,
    fresh: true,
  });
  return {
    boot_id: "abcd",
    uptime_ms: uptime,
    version: "test",
    state: { code: "RUN" },
    faults: 0,
    envelope: { fingerprint: "fedc" },
    outputs: {
      fan_request: true,
      glow: false,
      fuel: true,
      speed_code: 2400,
      command_percent: 58.6,
    },
    sensors: {
      exhaust: sensor(310250),
      air: sensor(75100),
      room: sensor(19800),
    },
  };
}
function csvRows(recorder) {
  // Test fixtures here have no embedded quotes or newlines; escaping is checked separately.
  const rows = recorder
    .csv()
    .slice(1)
    .trimEnd()
    .split("\r\n")
    .map((line) => line.slice(1, -1).split('";"'));
  return rows
    .slice(1)
    .map((row) => Object.fromEntries(rows[0].map((key, i) => [key, row[i]])));
}

test("capture copies readings, quality, timestamps and output commands without changing the controller", () => {
  const s = snapshot(),
    before = structuredClone(s),
    r = new Measurements();
  assert(r.start(s, true, time));
  r.observe(s, true, time + 1);
  assert.equal(r.samples, 1);
  assert.deepEqual(s, before);
  s.sensors.exhaust.milli_c = 0;
  const row = csvRows(r)[0];
  assert.equal(row.exhaust_c, "310.25");
  assert.equal(row.exhaust_sample_ms, "900");
  assert.equal(row.air_c, "75.1");
  assert.equal(row.motor_pwm_code, "2400");
  assert.equal(row.fuel_command, "true");
  assert.equal(row.profile_fingerprint, "fedc");
  assert.equal(row.time_utc, new Date(time).toISOString());
});

test("invalid, stale and faulted sensors produce empty temperatures, never false zeroes", () => {
  const s = snapshot(),
    r = new Measurements();
  s.sensors.exhaust.valid = false;
  s.sensors.air.fresh = false;
  s.sensors.room.status = 4;
  r.start(s, true, time);
  const row = csvRows(r)[0];
  assert.equal(row.exhaust_c, "");
  assert.equal(row.air_c, "");
  assert.equal(row.room_c, "");
  assert.equal(row.room_status, "4");
  s.sensors.exhaust = { valid: true, fresh: true, status: 0, milli_c: NaN };
  s.uptime_ms++;
  r.observe(s, true, time + 1);
  assert.equal(csvRows(r)[1].exhaust_c, "");
});

test("connection and sampling gaps are explicit and do not duplicate retained readings", () => {
  const r = new Measurements(),
    s = snapshot();
  r.start(s, true, time);
  r.observe(s, false, time + 1000);
  r.observe(s, false, time + 2000);
  assert.equal(r.samples, 1);
  r.observe(snapshot(5000), true, time + 4000);
  r.observe(snapshot(10000), true, time + 9000);
  assert.deepEqual(
    csvRows(r).map((x) => x.event),
    [
      "sample",
      "disconnected",
      "reconnected",
      "sample",
      "sampling_gap",
      "sample",
    ],
  );
  assert.equal(csvRows(r)[1].exhaust_c, "");
  assert.equal(r.status().elapsedMs, 9000);
});

test("reboot and regressing controller time stop recording without mixing sessions", () => {
  for (const changed of [{ ...snapshot(20), boot_id: "ef" }, snapshot(500)]) {
    const r = new Measurements();
    r.start(snapshot(), true, time);
    r.observe(changed, true, time + 1000);
    assert.equal(r.active, false);
    assert.equal(r.reason, "restart");
    assert.equal(r.samples, 1);
    assert.equal(csvRows(r).at(-1).event, "restart");
    r.observe(snapshot(2000), true, time + 2000);
    assert.equal(r.samples, 1);
  }
});

test("bounded capture stops at capacity, retaining its first and last records", () => {
  const r = new Measurements(3);
  r.start(snapshot(), true, time);
  r.note("Холодный отопитель", time + 1);
  r.observe(snapshot(2000), true, time + 1000);
  assert.equal(r.reason, "limit");
  assert.equal(r.active, false);
  r.observe(snapshot(3000), true, time + 2000);
  assert.equal(r.rows.length, 3);
  assert.equal(r.samples, 2);
  assert.equal(csvRows(r).at(-1).uptime_ms, "2000");
});

test("a gap reaching capacity cannot append one more sample", () => {
  const r = new Measurements(2);
  r.start(snapshot(), true, time);
  r.observe(snapshot(6000), true, time + 5000);
  assert.equal(r.rows.length, 2);
  assert.equal(r.samples, 1);
  assert.equal(r.reason, "limit");
});

test("new capture requires exported data; live exports become unsaved again on the next row", () => {
  const r = new Measurements();
  assert.equal(r.start(snapshot(), false, time), false);
  assert.equal(r.start(null, true, time), false);
  r.start(snapshot(), true, time);
  r.markExported();
  assert.equal(r.unsaved, false);
  r.observe(snapshot(2000), true, time + 1000);
  assert.equal(r.unsaved, true);
  r.stop("stopped", time + 2000);
  assert.equal(r.start(snapshot(), true, time), false);
  assert.equal(r.note("Late note", time), false);
  r.markExported();
  assert(r.start(snapshot(), true, time + 3000));
  assert.equal(r.samples, 1);
  assert.equal(r.rows.length, 1);
});

test("CSV escapes notes, neutralizes formulas and keeps operator events separate from samples", () => {
  const r = new Measurements();
  r.start(snapshot(), true, time);
  assert.equal(r.note("  ", time), false);
  r.note('=SUM(1;2) "test"\nline', time + 500);
  assert(r.csv().startsWith("\uFEFF"));
  assert(r.csv().includes('"\'=SUM(1;2) ""test""\nline"'));
  assert.equal(r.rows[1].length, 5);
  r.note("x".repeat(1000), time + 600);
  assert.equal(r.rows[2][2].length, 160);
  assert.equal(r.samples, 1);
});

test("trial captures arm, actuation and cooldown, stopping only after the cycle completes", () => {
  const r = new Measurements();
  const initial = {
    ...snapshot(),
    trial: { active: false },
    state: { code: "UNCONFIGURED" },
  };
  assert(r.startTrial(initial, true, time));
  const armed = {
    ...snapshot(1100),
    trial: { active: true },
    state: { code: "TRIAL_READY" },
  };
  r.observe(armed, true, time + 100);
  r.cancelTrialRequest(time + 101); // A late browser timeout must not discard an observed trial.
  assert(r.active);
  r.observe(
    { ...snapshot(1200), trial: { active: true }, state: { code: "IGNITION" } },
    true,
    time + 200,
  );
  r.observe(
    {
      ...snapshot(1300),
      trial: { active: true },
      state: { code: "STOP_PURGE" },
    },
    true,
    time + 300,
  );
  assert(r.active);
  r.observe(
    {
      ...snapshot(1400),
      trial: { active: false },
      state: { code: "UNCONFIGURED" },
    },
    true,
    time + 400,
  );
  assert.equal(r.active, false);
  assert.equal(r.reason, "trial_finished");
  assert.equal(
    csvRows(r).find((row) => row.state === "IGNITION").trial_active,
    "true",
  );
  const before = r.csv();
  assert.equal(r.startTrial(initial, true, time + 500), false);
  assert.equal(r.csv(), before);
  r.markExported();
  assert(r.startTrial(initial, true, time + 600));
});

test("trial cannot overwrite an active recording and rejection or reboot retains its collected data", () => {
  const r = new Measurements();
  const initial = { ...snapshot(), trial: { active: false } };
  assert(r.start(initial, true, time));
  const before = r.csv();
  assert.equal(r.startTrial(initial, true, time + 100), false);
  assert.equal(r.csv(), before);
  r.stop();
  r.markExported();
  assert(r.startTrial(initial, true, time + 200));
  r.cancelTrialRequest(time + 300);
  assert.equal(r.active, false);
  assert.equal(r.reason, "trial_not_started");
  assert(r.unsaved);
  r.markExported();
  assert(r.startTrial(initial, true, time + 400));
  r.observe(
    { ...snapshot(20), boot_id: "new", trial: { active: false } },
    true,
    time + 500,
  );
  assert.equal(r.active, false);
  assert.equal(r.reason, "restart");
  assert(r.unsaved);
});

test("CSV keeps accepted PWM steps between polls with their original controller times and temperatures", () => {
  const r = new Measurements();
  r.startTrial(snapshot(1000), true, time);
  const event = (kind, at_ms, speed_code, exhaust_milli_c) => ({
    kind,
    at_ms,
    state: "RUN",
    speed_code,
    exhaust_milli_c,
    air_milli_c: 85000,
    faults: 0,
  });
  const s = {
    ...snapshot(2000),
    trial: { active: true },
    envelope: { fingerprint: "fedc", limits: { pwm_max: 4014 } },
    events: [
      event("TRIAL_PWM_CHANGED", 999, 1000, 250000),
      event("TRIAL_CONFIRMED", 1100, 4014, 310000),
      event("TRIAL_PWM_CHANGED", 1200, 2800, 320000),
      event("TRIAL_PWM_CHANGED", 1500, 2400, 300000),
    ],
  };
  r.observe(s, true, time + 1000);
  r.observe({ ...s, uptime_ms: 2100 }, true, time + 1100);
  const changes = csvRows(r).filter((row) => row.event === "TRIAL_PWM_CHANGED");
  assert.deepEqual(
    changes.map((row) => [
      row.uptime_ms,
      row.motor_pwm_code,
      row.exhaust_c,
      row.air_c,
    ]),
    [
      ["1200", "2800", "320", "85"],
      ["1500", "2400", "300", "85"],
    ],
  );
  assert.equal(changes[0].time_utc, new Date(time + 200).toISOString());
  assert.equal(changes[0].exhaust_sample_ms, "");
  assert.equal(changes[0].fuel_command, "");
  assert.equal(
    csvRows(r).filter((row) => row.event === "TRIAL_CONFIRMED").length,
    1,
  );
  assert.equal(r.samples, 3);
});
