import test from "node:test";
import assert from "node:assert/strict";
import {
  commandAllowed,
  connectionReady,
  needsStop,
  temperature,
} from "../src/lib/presentation.js";
import { ControllerSession } from "../src/lib/controller.js";

test("missing admission never enables heat, settings admission is independent", () => {
  assert.equal(commandAllowed({}, "START"), false);
  const snapshot = {
    commands: { START: { allowed: false }, SETTINGS: { allowed: true } },
  };
  assert.equal(commandAllowed(snapshot, "START"), false);
  assert.equal(commandAllowed(snapshot, "SETTINGS"), true);
});
test("unavailable peripherals are not temperatures or an active heating cycle", () => {
  assert.equal(
    temperature({ valid: false, milli_c: 0, fresh: false }),
    "Нет показаний",
  );
  assert.equal(
    temperature({ valid: true, milli_c: 0, fresh: false }),
    "Данные устарели",
  );
  assert.equal(temperature({ valid: true, milli_c: 0, fresh: true }), "0 °C");
  assert.equal(
    needsStop({ state: { code: "UNCONFIGURED" }, outputs: {} }),
    false,
  );
  assert.equal(
    needsStop({
      state: { code: "RECOVERY_PURGE" },
      outputs: { fan_request: true },
    }),
    false,
  );
  assert.equal(
    needsStop({ state: { code: "PREHEAT" }, outputs: { glow: true } }),
    true,
  );
  assert.equal(needsStop({ state: { code: "RUN" }, outputs: {} }), true);
});
test("offline, pending and reboot block normal commands", () => {
  const ready = { auth: "authenticated", link: "online" };
  assert.equal(connectionReady(ready), true);
  for (const change of [
    { link: "offline" },
    { pending: {} },
    { requestInFlight: "SETTINGS" },
    { ota: { phase: "activation_queued" } },
  ])
    assert.equal(connectionReady({ ...ready, ...change }), false);
});
test("active fuel or glow always exposes STOP, completed cooling does not", () => {
  const snapshot = {
    state: { code: "IDLE" },
    outputs: { fuel: false, glow: false },
  };
  assert.equal(needsStop(snapshot), false);
  snapshot.outputs.fuel = true;
  assert.equal(needsStop(snapshot), true);
  snapshot.outputs.fuel = false;
  snapshot.outputs.glow = true;
  assert.equal(needsStop(snapshot), true);
});
test("blocked action never requests a grant", async () => {
  let calls = 0;
  const session = new ControllerSession({
    api: {
      state: async () => ({
        boot_id: "1",
        state_version: "1",
        run_id: "0",
        uptime_ms: 1,
        state: { code: "UNCONFIGURED" },
        settings: {},
        sensors: {},
        commands: { START: { allowed: false, reason: "Выходы отключены" } },
      }),
      challenge: async () => {
        calls++;
      },
    },
  });
  await session.restore();
  assert.equal((await session.submit("START")).status, "BLOCKED");
  assert.equal(calls, 0);
  assert.equal(session.view.message.text, "Выходы отключены");
});

test("timer countdown rounds the last second upward and keeps durations beyond a day", async () => {
  const { countdownText } = await import("../src/lib/presentation.js");
  assert.equal(countdownText(1), "00:00:01");
  assert.equal(countdownText(0), "00:00:00");
  assert.equal(countdownText(90 * 60000), "01:30:00");
  assert.equal(countdownText(7 * 86400000), "168:00:00");
  assert.equal(countdownText(NaN), "—");
});
