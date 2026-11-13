import test from "node:test";
import assert from "node:assert/strict";
import { commandMatchesSnapshot } from "../src/lib/protocol.js";
import { needsStop, routeFromHash, errorText } from "../src/lib/presentation.js";

const baseline = { boot_id: "1", state_version: "1", state: { code: "UNCONFIGURED" } };
const snapshot = { ...baseline, state_version: "2", state: { code: "DEBUG" }, outputs: { speed_code: 1200, glow: true, fuel: false } };

test("debug commands require controller confirmation; stale data, reboot and cooling cannot acknowledge them", () => {
  const enter = { action: "DEBUG_ENTER", payload: {}, baseline };
  assert(commandMatchesSnapshot(enter, snapshot));
  const set = { action: "DEBUG_SET", payload: { speed_code: 1200, glow: true, fuel: false }, baseline };
  assert(commandMatchesSnapshot(set, snapshot));
  for (const change of [
    { state_version: "1" }, { boot_id: "2" }, { state: { code: "STOP_PURGE" } },
    { outputs: { ...snapshot.outputs, fuel: true } },
    { outputs: { ...snapshot.outputs, speed_code: 0 } },
  ]) assert.equal(commandMatchesSnapshot(set, { ...snapshot, ...change }), false);
});

test("STOP remains visible for a debug session with every actuator off", () => {
  assert(needsStop({ state: { code: "DEBUG" }, outputs: { speed_code: 0, glow: false, fuel: false } }));
  assert.equal(routeFromHash("#/debug"), "debug");
});

test("rejected manual heat explains the actual missing condition", () => {
  assert.equal(errorText({ code: "DEBUG_HEAT" }, "Ошибка", { debug: { heat_reason: "Проверьте датчики температуры" } }), "Проверьте датчики температуры");
});
