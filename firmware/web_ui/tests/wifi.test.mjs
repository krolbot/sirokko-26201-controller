import test from "node:test";
import assert from "node:assert/strict";
import { ApiClient } from "../src/lib/api.js";
import { ControllerSession } from "../src/lib/controller.js";

for (const password of ["", "Example!123", "A".repeat(63)]) {
  test(`save Wi-Fi password (${password.length} characters)`, async () => {
    const api = new ApiClient({
      fetchImpl: async (path, request) => {
        assert.equal(path, "/api/wifi");
        assert.equal(request.method, "POST");
        assert.equal(request.body, password);
        assert.equal(request.headers["X-Sirokko-Config"], "wifi");
        assert.equal(
          request.headers["Content-Type"],
          "application/octet-stream",
        );
        assert.equal(request.credentials, "same-origin");
        return { ok: true, text: async () => '{"status":"SAVED"}' };
      },
    });
    assert.equal((await api.saveWifi(password)).status, "SAVED");
  });
}
test("does not report success on NVS failure", async () => {
  const api = new ApiClient({
    fetchImpl: async () => ({
      ok: false,
      status: 503,
      text: async () => '{"code":"STORAGE"}',
    }),
  });
  await assert.rejects(api.saveWifi("Example!123"), {
    code: "STORAGE",
    status: 503,
  });
});
test("protected mode requires login when restoring a session", async () => {
  const api = new ApiClient({
    fetchImpl: async () => ({
      ok: false,
      status: 401,
      text: async () => '{"code":"AUTH"}',
    }),
  });
  const session = new ControllerSession({ api });
  assert.equal(await session.restore(), false);
  assert.equal(session.view.auth, "anonymous");
});
test("open mode loads the dashboard without a login request", async () => {
  const snapshot = {
    boot_id: "1",
    state_version: "2",
    run_id: "0",
    uptime_ms: 1000,
    state: { code: "IDLE" },
    settings: {},
    sensors: {},
    faults: 0,
    wifi: {
      password_required: false,
      next_boot_password_required: false,
      restart_required: false,
    },
  };
  const api = new ApiClient({
    fetchImpl: async (path) => {
      assert.equal(path, "/api/state");
      return { ok: true, text: async () => JSON.stringify(snapshot) };
    },
  });
  const session = new ControllerSession({ api });
  assert.equal(await session.restore(), true);
  assert.equal(session.view.auth, "authenticated");
  assert.equal(session.view.link, "online");
  assert.equal(session.view.snapshot.wifi.password_required, false);
});
