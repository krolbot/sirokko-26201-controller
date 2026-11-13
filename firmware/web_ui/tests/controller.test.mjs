import test from "node:test";
import assert from "node:assert/strict";
import { ControllerSession } from "../src/lib/controller.js";
import {
  fullSettingsPayload,
  commandMatchesSnapshot,
  manualMinimum,
} from "../src/lib/protocol.js";
const state = () => ({
  boot_id: "1",
  run_id: "0",
  state_version: "2",
  uptime_ms: 1000,
  version: "1.0",
  state: { code: "IDLE" },
  commands: { START: { allowed: true }, OTA: { allowed: true } },
  settings: {},
  sensors: {},
  faults: 0,
});

const settings = () => ({
  room_setpoint_milli_c: 23000,
  duration_ms: 0,
  mode: "TEMPERATURE",
  manual_percent: 100,
  slew_up_per_s: 0.005,
  slew_down_per_s: 0.005,
});
test("settings confirmation tolerates subsequent unrelated panel edits", () => {
  const baseline = { ...state(), settings: settings() };
  const requested = { ...settings(), room_setpoint_milli_c: 24000 };
  const observed = {
    ...baseline,
    state_version: "3",
    settings: { ...requested, duration_ms: 120000 },
  };
  const pending = {
    action: "SETTINGS",
    baseline,
    payload: { settings: requested },
  };
  assert.equal(commandMatchesSnapshot(pending, observed), true);
  observed.settings.room_setpoint_milli_c = 25000;
  assert.equal(commandMatchesSnapshot(pending, observed), false);
  observed.settings = requested;
  observed.state_version = baseline.state_version;
  assert.equal(commandMatchesSnapshot(pending, observed), false);
});
test("manual handoff confirms the controller's actual level, then requires explicit level edits to match", () => {
  const baseline = {
    ...state(),
    state: { code: "RUN" },
    run_id: "1",
    settings: settings(),
  };
  const requested = { ...settings(), mode: "MANUAL", manual_percent: 75 };
  const observed = {
    ...baseline,
    state_version: "3",
    settings: { ...requested, manual_percent: 80 },
  };
  const pending = {
    action: "SETTINGS",
    baseline,
    payload: { settings: requested },
  };
  assert.equal(commandMatchesSnapshot(pending, observed), true);
  baseline.settings.mode = "MANUAL";
  assert.equal(commandMatchesSnapshot(pending, observed), false);
  observed.settings.manual_percent = 75;
  assert.equal(commandMatchesSnapshot(pending, observed), true);
  observed.boot_id = "2";
  assert.equal(commandMatchesSnapshot(pending, observed), false);
});
test("manual control respects the same configured minimum and rounding as the encoder", () => {
  const parameters = {
    valid: true,
    minimum_speed_code: 1201,
    maximum_speed_code: 2000,
  };
  assert.equal(manualMinimum(parameters), 65);
  assert.equal(
    manualMinimum({ maximum_speed_code: 4014, minimum_speed_code: 0 }),
    5,
  );
  const baseline = { ...state(), settings: { ...settings(), mode: "MANUAL" } };
  const requested = { ...baseline.settings, manual_percent: 5 };
  const observed = {
    ...baseline,
    state_version: "3",
    envelope: { parameters },
    settings: { ...requested, manual_percent: 65 },
  };
  assert.equal(
    commandMatchesSnapshot(
      { action: "SETTINGS", baseline, payload: { settings: requested } },
      observed,
    ),
    true,
  );
});
test("timer cancellation cannot be confirmed by a stopped heater or a different run", () => {
  const baseline = {
    ...state(),
    state: { code: "RUN" },
    run_id: "1",
    timer: { deadline_ms: 60000 },
  };
  const pending = { action: "TIMER", baseline, payload: { delay_ms: 0 } };
  const observed = {
    ...baseline,
    state_version: "3",
    timer: { deadline_ms: 0 },
  };
  assert.equal(commandMatchesSnapshot(pending, observed), true);
  observed.run_id = "2";
  assert.equal(commandMatchesSnapshot(pending, observed), false);
  observed.run_id = "1";
  observed.state = { code: "STOP_PURGE" };
  assert.equal(commandMatchesSnapshot(pending, observed), false);
});
test("local STOP interrupts a pending web timer change", async () => {
  let snapshot = {
    ...state(),
    state: { code: "RUN" },
    run_id: "1",
    commands: { TIMER: { allowed: true } },
  };
  const session = new ControllerSession({
    api: {
      state: async () => snapshot,
      challenge: async () => ({ boot_id: "1", state_version: "2" }),
      command: async () => ({ status: "QUEUED" }),
    },
    commandId: () => "abcd",
  });
  await session.restore();
  await session.submit("TIMER", { delay_ms: 0 });
  assert.ok(session.view.pending);
  snapshot = {
    ...snapshot,
    state_version: "3",
    state: { code: "STOP_PURGE" },
    timer: { deadline_ms: 0 },
  };
  await session.poll();
  assert.equal(session.view.pending, null);
  assert.equal(session.view.message.kind, "warning");
  assert.equal(
    session.view.message.text,
    "Действие прервано остановкой отопителя",
  );
});
test("OTA success requires startup confirmation, not only the version string", async () => {
  let snapshot = state();
  const session = new ControllerSession({
    api: { state: async () => snapshot },
  });
  await session.restore();
  session._view.ota = {
    phase: "activation_queued",
    beforeBootId: "1",
    expectedVersion: "1.1",
    deadline: Date.now() + 120000,
  };
  snapshot = {
    ...state(),
    boot_id: "3",
    version: "1.1",
    hardware: { startup_ready: false },
  };
  await session.poll();
  assert.equal(session.view.ota.phase, "checking_startup");
  snapshot.hardware.startup_ready = true;
  await session.poll();
  assert.equal(session.view.ota.phase, "running_expected_version");
});
test("another client changing state prevents stale command submission", async () => {
  let sent = 0;
  const session = new ControllerSession({
    api: {
      state: async () => state(),
      challenge: async () => ({ boot_id: "1", state_version: "3" }),
      command: async () => {
        sent++;
      },
    },
    commandId: () => "abcd",
  });
  await session.restore();
  assert.equal((await session.submit("START")).status, "STALE_SNAPSHOT");
  assert.equal(sent, 0);
});
test("HTTP queue acceptance is not reported as applied settings", async () => {
  const session = new ControllerSession({
    api: {
      state: async () => state(),
      challenge: async () => ({ boot_id: "1", state_version: "2" }),
      command: async () => ({ status: "QUEUED" }),
    },
    commandId: () => "abcd",
  });
  await session.restore();
  await session.submit("START");
  assert.ok(session.view.pending);
  assert.notEqual(session.view.message?.kind, "ok");
});
test("frontend rejects fractional-degree setpoints just like firmware", () => {
  const settings = {
    room_setpoint_milli_c: 23000,
    duration_ms: 0,
    mode: "MANUAL",
    manual_percent: 50,
    slew_up_per_s: 0.005,
    slew_down_per_s: 0.005,
  };
  assert.equal(fullSettingsPayload(settings).room_setpoint_milli_c, 23000);
  assert.throws(() =>
    fullSettingsPayload(settings, { room_setpoint_milli_c: 23500 }),
  );
});

test("unexpected command response is explained without its technical code", async () => {
  const session = new ControllerSession({
    api: {
      state: async () => state(),
      challenge: async () => ({ boot_id: "1", state_version: "2" }),
      command: async () => ({ status: "REJECTED", code: "INTERNAL_UNKNOWN" }),
    },
    commandId: () => "abcd",
  });
  await session.restore();
  await session.submit("START");
  assert.equal(session.view.message.text, "Контроллер отклонил действие");
});

test("a new fault cancels an unconfirmed action and explains the interruption", async () => {
  let snapshot = state();
  const session = new ControllerSession({
    api: {
      state: async () => snapshot,
      challenge: async () => ({ boot_id: "1", state_version: "2" }),
      command: async () => ({ status: "QUEUED" }),
    },
    commandId: () => "abcd",
  });
  await session.restore();
  await session.submit("START");
  assert.ok(session.view.pending);
  snapshot = {
    ...state(),
    faults: 8,
    state: { code: "FAULT" },
    state_version: "3",
  };
  await session.poll();
  assert.equal(session.view.pending, null);
  assert.equal(session.view.message.kind, "warning");
  assert.equal(
    session.view.message.text,
    "Действие прервано из-за ошибки отопителя",
  );
});
