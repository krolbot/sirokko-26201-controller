import assert from 'node:assert/strict';
import test from 'node:test';

import { ApiClient } from '../../src/lib/api.js';
import { ControllerSession } from '../../src/lib/controller.js';
import {
  commissioningAllowed,
  commissioningBlock,
  commandMatchesSnapshot,
  fullSettingsPayload,
  modelRevisionMatches,
  modelRevisionPayload,
  sensorText,
  timerDeadlineMatches,
} from '../../src/lib/protocol.js';
import { sha256Hex } from '../../src/lib/sha256.js';
import { syntheticState } from '../fixtures/synthetic-state.js';

const clone = (value) => JSON.parse(JSON.stringify(value));
const waitForTasks = () => new Promise((resolve) => setTimeout(resolve, 0));

class SyntheticApi {
  constructor(state = syntheticState()) {
    this.current = clone(state);
    this.calls = { login: 0, state: 0, challenge: [], command: [], stop: [], prepareOta: 0, sendOta: 0 };
    this.challengeVersion = null;
    this.commandHandler = null;
    this.stateError = null;
    this.otaResponse = { status: 'VERIFIED_ACTIVATION_QUEUED', expected_version: 'test-1.1.0' };
  }

  async login() { this.calls.login += 1; return { status: 'OK' }; }
  async state() {
    this.calls.state += 1;
    if (this.stateError) throw this.stateError;
    return clone(this.current);
  }
  async challenge(payload) {
    this.calls.challenge.push(clone(payload));
    return { token: 'a1', state_version: this.challengeVersion ?? this.current.state_version, expires_ms: 'ffff', boot_id: this.current.boot_id };
  }
  async command(payload) {
    this.calls.command.push(clone(payload));
    return this.commandHandler ? this.commandHandler(payload) : { status: 'QUEUED' };
  }
  async stop(commandId) { this.calls.stop.push(commandId); return { status: 'STOP_REQUESTED' }; }
  async prepareOta(file, commandId) {
    this.calls.prepareOta += 1;
    return { file, payload: { action: 'OTA', command_id: commandId, upload_size: file.size, upload_sha256: '0'.repeat(64) }, grant: { token: 'a1', state_version: this.challengeVersion ?? this.current.state_version, expires_ms: 'ffff', boot_id: this.current.boot_id } };
  }
  async sendOta() { this.calls.sendOta += 1; return this.otaResponse; }
}

async function restored(api, clock = { now: 0 }) {
  let serial = 0;
  const session = new ControllerSession({
    api,
    now: () => clock.now,
    commandId: () => `c${(++serial).toString(16)}`,
  });
  await session.restore();
  assert.equal(session.view.auth, 'authenticated');
  assert.equal(session.view.link, 'online');
  return session;
}

function otaFile() {
  return { name: 'signed.sota', size: 3, arrayBuffer: async () => new TextEncoder().encode('abc').buffer };
}

test('SHA-256 fallback works without crypto.subtle and hashes the whole byte buffer', async () => {
  const digest = await sha256Hex(new TextEncoder().encode('abc').buffer, {});
  assert.equal(digest, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
});

test('API OTA preparation preserves opaque hex grants and sends complete-file digest headers', async () => {
  const calls = [];
  const fetchImpl = async (path, options) => {
    calls.push({ path, options });
    if (path === '/api/challenge') return new Response(JSON.stringify({ status: 'CHALLENGE', token: 'deadbeef', state_version: '22', expires_ms: 'a0', boot_id: 'aa' }), { status: 200 });
    if (path === '/api/ota') return new Response(JSON.stringify({ status: 'VERIFIED_ACTIVATION_QUEUED', expected_version: 'test-1.1.0' }), { status: 200 });
    throw new Error(`unexpected path ${path}`);
  };
  const client = new ApiClient({ fetchImpl, hash: (raw) => sha256Hex(raw, {}) });
  const prepared = await client.prepareOta(otaFile(), 'c0ffee');
  assert.deepEqual(prepared.grant, { token: 'deadbeef', state_version: '22', expires_ms: 'a0', boot_id: 'aa' });
  assert.equal(prepared.payload.upload_sha256, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  await client.sendOta(prepared);
  const upload = calls.at(-1);
  assert.equal(upload.options.headers['X-Sirokko-Token'], 'deadbeef');
  assert.equal(upload.options.headers['X-Sirokko-State-Version'], '22');
  assert.equal(upload.options.headers['X-Sirokko-Expires'], 'a0');
  assert.equal(upload.options.headers['X-Sirokko-Command-Id'], 'c0ffee');
  assert.equal(upload.options.headers['X-Sirokko-Upload-SHA256'], prepared.payload.upload_sha256);
});

test('generic client command refuses OTA before any /api/command request', () => {
  const calls = [];
  const client = new ApiClient({ fetchImpl: async (...arguments_) => { calls.push(arguments_); throw new Error('must not fetch'); } });
  assert.throws(() => client.command({ action: 'OTA', command_id: '1' }), /prepareOta\(\).*sendOta/);
  assert.equal(calls.length, 0);
});

test('calibration block fails closed before browser challenge or generic command', async () => {
  const block = { code: 'MODEL_UNAVAILABLE', ru: 'Сохранённая модель недоступна' };
  const api = new SyntheticApi(syntheticState({ commissioning: { block } }));
  const session = await restored(api);
  assert.deepEqual(commissioningBlock(session.view.snapshot), block);
  assert.equal(commissioningAllowed(session.view.snapshot), false);
  const result = await session.submit('CALIBRATE');
  assert.equal(result.status, 'CALIBRATION_BLOCKED');
  assert.equal(api.calls.challenge.length, 0);
  assert.equal(api.calls.command.length, 0);
  assert.match(session.view.message.text, /Сохранённая модель/);

  const legacySnapshot = syntheticState();
  delete legacySnapshot.commissioning.block;
  assert.equal(commissioningAllowed(legacySnapshot), false, 'missing contract field must not permit commissioning');
  assert.equal(commissioningBlock({}).code, 'ENVELOPE_MISSING');
  assert.equal(commissioningAllowed(syntheticState()), true, 'permitted fixture states set an explicit NONE block');
});

test('MODEL_REVISION builds an exact constrained DTO and confirms every observed field', async () => {
  const baseline = syntheticState();
  const revision = modelRevisionPayload(baseline.learned_model, {
    minimum_speed_code: 200,
    maximum_speed_code: 800,
    slew_up_per_s: 0.0008,
    slew_down_per_s: 0.0007,
  });
  assert.deepEqual(revision, {
    expected_revision: 1,
    minimum_speed_code: 200,
    maximum_speed_code: 800,
    slew_up_per_s: 0.0008,
    slew_down_per_s: 0.0007,
  });
  assert.throws(() => modelRevisionPayload(baseline.learned_model, { maximum_speed_code: 851 }), /только сузить/);
  assert.throws(() => modelRevisionPayload(baseline.learned_model, { minimum_speed_code: 800, maximum_speed_code: 800 }), /строго меньше/);
  for (const value of [0, 0.000099, NaN, Infinity]) {
    assert.throws(() => modelRevisionPayload(baseline.learned_model, { slew_up_per_s: value }), /Скорости модели/);
    assert.throws(() => modelRevisionPayload(baseline.learned_model, { slew_down_per_s: value }), /Скорости модели/);
  }
  assert.equal(modelRevisionPayload(baseline.learned_model, { slew_up_per_s: 0.0001 }).slew_up_per_s, 0.0001);
  assert.throws(() => modelRevisionPayload({ ...baseline.learned_model, provenance: { revision: 0xffffffff } }), /ревизия/);

  assert.throws(() => modelRevisionPayload({ ...baseline.learned_model, valid: false }, {}), /нет сохранённой/i);

  const observed = syntheticState({
    state_version: '23',
    learned_model: {
      minimum_speed_code: revision.minimum_speed_code,
      maximum_speed_code: revision.maximum_speed_code,
      slew_up_per_s: revision.slew_up_per_s,
      slew_down_per_s: revision.slew_down_per_s,
      provenance: { revision: 2 },
    },
  });
  assert.equal(modelRevisionMatches(revision, observed.learned_model), true);
  assert.equal(modelRevisionMatches(revision, { ...observed.learned_model, maximum_speed_code: 799 }), false);
  assert.equal(commandMatchesSnapshot({ action: 'MODEL_REVISION', payload: { revision }, baseline }, observed), true);
  assert.equal(commandMatchesSnapshot({ action: 'MODEL_REVISION', payload: { revision }, baseline }, { ...observed, learned_model: { ...observed.learned_model, slew_down_per_s: 0.0006 } }), false);

  const api = new SyntheticApi(baseline);
  api.commandHandler = (payload) => {
    api.current = observed;
    return { status: 'QUEUED' };
  };
  const session = await restored(api);
  assert.equal((await session.submit('MODEL_REVISION', { revision })).status, 'QUEUED');
  await waitForTasks();
  assert.deepEqual(api.calls.challenge[0].revision, revision);
  assert.deepEqual(api.calls.command[0].revision, revision);
  assert.equal(session.view.pending, null);
});

test('settings carry the complete current payload and confirm only on a newer matching snapshot', async () => {
  const api = new SyntheticApi();
  api.current.settings.unedited_backend_field = 'preserved';
  api.commandHandler = (payload) => {
    api.current = syntheticState({
      state_version: '23',
      settings: { ...payload.settings },
    });
    return { status: 'QUEUED' };
  };
  const session = await restored(api);
  const payload = fullSettingsPayload(session.view.snapshot.settings, { room_setpoint_milli_c: 23_000 });
  assert.equal(payload.unedited_backend_field, 'preserved');
  assert.equal((await session.submit('SETTINGS', { settings: payload })).status, 'QUEUED');
  await waitForTasks();
  assert.equal(session.view.pending, null);
  assert.equal(session.view.snapshot.settings.room_setpoint_milli_c, 23_000);
  assert.equal(session.view.message.kind, 'ok');
});

test('challenge state-version mismatch rejects a stale draft before command submit', async () => {
  const api = new SyntheticApi();
  api.challengeVersion = '23';
  const session = await restored(api);
  const result = await session.submit('SETTINGS', { settings: fullSettingsPayload(session.view.snapshot.settings, { room_setpoint_milli_c: 23_000 }) });
  assert.equal(result.status, 'STALE_SNAPSHOT');
  assert.equal(api.calls.command.length, 0);
  assert.match(session.view.message.text, /изменилось на другой панели/);
});

test('queued command becomes unknown after five seconds rather than rejected', async () => {
  const clock = { now: 0 };
  const api = new SyntheticApi();
  const session = await restored(api, clock);
  assert.equal((await session.submit('START')).status, 'QUEUED');
  await waitForTasks();
  assert.ok(session.view.pending);
  clock.now = 5_000;
  session.tick();
  assert.equal(session.view.pending, null);
  assert.match(session.view.message.text, /Результат неизвестен/);
  assert.match(session.view.message.text, /не отклонён/);
});

test('STOP cancels an in-flight challenged action and remains a direct attempt', async () => {
  const api = new SyntheticApi();
  let releaseChallenge;
  api.challenge = async (payload) => new Promise((resolve) => { releaseChallenge = () => resolve({ token: 'a1', state_version: api.current.state_version, expires_ms: 'ffff', boot_id: api.current.boot_id, payload }); });
  const session = await restored(api);
  const changing = session.submit('START');
  await waitForTasks();
  const stopped = await session.stop();
  releaseChallenge();
  assert.equal(stopped.status, 'STOP_REQUESTED');
  assert.equal((await changing).status, 'CANCELLED');
  assert.equal(api.calls.command.length, 0);
  assert.equal(api.calls.stop.length, 1);
});

test('boot change and lost link clear pending work without retrying it after reconnect', async () => {
  const api = new SyntheticApi();
  const session = await restored(api);
  assert.equal((await session.submit('START')).status, 'QUEUED');
  await waitForTasks();
  api.current = syntheticState({ boot_id: 'bb', state_version: '23' });
  await session.poll();
  assert.equal(session.view.pending, null);
  assert.match(session.view.message.text, /перезапущен/);
  assert.equal(api.calls.command.length, 1);

  api.stateError = { kind: 'network', message: 'test disconnect' };
  await session.poll();
  assert.equal(session.view.link, 'offline');
  assert.equal(session.view.pending, null);
  api.stateError = null;
  api.current = syntheticState({ boot_id: 'bb', state_version: '24' });
  await session.poll();
  assert.equal(session.view.link, 'online');
  assert.equal(api.calls.command.length, 1);
});

test('timer confirmation uses deadline window, not merely remaining <= requested', () => {
  const baseline = syntheticState({ uptime_ms: 1_000, state_version: '22' });
  const requested = 60_000;
  const matching = syntheticState({ state_version: '23', uptime_ms: 1_900, timer: { active: true, remaining_ms: 59_100, deadline_ms: 61_000 } });
  const tooEarly = syntheticState({ state_version: '23', uptime_ms: 1_900, timer: { active: true, remaining_ms: 1, deadline_ms: 1_901 } });
  assert.equal(timerDeadlineMatches(baseline, matching, requested), true);
  assert.equal(timerDeadlineMatches(baseline, tooEarly, requested), false);
  assert.equal(timerDeadlineMatches(baseline, syntheticState({ state_version: '23', timer: { active: false, remaining_ms: 0, deadline_ms: 0 } }), 0), true);
});

test('STOP observation requires explicit stopped/purging state with fuel and glow off', () => {
  const baseline = syntheticState({ state: { code: 'RUN' } });
  const pending = { action: 'STOP', payload: {}, baseline };
  for (const code of ['MAINTENANCE', 'RECOVERY_PURGE', 'UNKNOWN', 'RUN']) {
    assert.equal(commandMatchesSnapshot(pending, syntheticState({ state_version: '23', state: { code } })), false, code);
  }
  for (const code of ['STOP_PURGE', 'FAULT_PURGE', 'IDLE', 'FAULT']) {
    const stopped = syntheticState({ state_version: '23', state: { code } });
    assert.equal(commandMatchesSnapshot(pending, stopped), true, code);
    assert.equal(commandMatchesSnapshot(pending, { ...stopped, outputs: { ...stopped.outputs, fuel: true } }), false);
    assert.equal(commandMatchesSnapshot(pending, { ...stopped, outputs: { ...stopped.outputs, glow: true } }), false);
  }
});

test('OTA new expected version cannot override a fault warning', async () => {
  const api = new SyntheticApi();
  const session = await restored(api);
  await session.uploadOta(otaFile());
  api.current = syntheticState({ boot_id: 'bb', version: 'test-1.1.0', state_version: '23', faults: 1, state: { code: 'FAULT', ru: 'Авария' } });
  await session.poll();
  assert.equal(session.view.ota.phase, 'running_expected_version'); // Version observation is retained, not a health claim.
  assert.equal(session.view.message.kind, 'warning');
  assert.match(session.view.message.text, /аварию/);
});

test('freshness is rendered from state instead of inventing sensor data', () => {
  assert.equal(sensorText({ milli_c: 21_400, valid: true, fresh: true, status: 0 }), '21,4 °C');
  assert.equal(sensorText({ milli_c: 21_400, valid: true, fresh: false, status: 0 }), '21,4 °C · устарело');
  assert.equal(sensorText({ milli_c: 21_400, valid: false, fresh: false, status: 'NO_SIGNAL' }), 'Ошибка датчика (NO_SIGNAL)');
});

test('OTA refuses legacy or generic success statuses and waits for a new boot observation', async () => {
  for (const oldStatus of ['VERIFIED_REBOOTING', 'QUEUED']) {
    const api = new SyntheticApi();
    api.otaResponse = { status: oldStatus };
    const session = await restored(api);
    const result = await session.uploadOta(otaFile());
    assert.equal(result.status, 'REJECTED');
    assert.equal(session.view.ota.phase, 'idle');
  }

  const api = new SyntheticApi();
  const session = await restored(api);
  assert.equal((await session.uploadOta(otaFile())).status, 'VERIFIED_ACTIVATION_QUEUED');
  assert.equal(session.view.ota.phase, 'activation_queued');
  api.current = syntheticState({ boot_id: 'bb', version: 'test-1.1.0', state_version: '23' });
  await session.poll();
  assert.equal(session.view.ota.phase, 'running_expected_version');
  assert.equal(session.view.ota.observedVersion, 'test-1.1.0');
  assert.match(session.view.message.text, /Самопроверка и откат/);
});

test('OTA state-version mismatch never sends an upload body', async () => {
  const api = new SyntheticApi();
  api.challengeVersion = '23';
  const session = await restored(api);
  assert.equal((await session.uploadOta(otaFile())).status, 'STALE_SNAPSHOT');
  assert.equal(api.calls.sendOta, 0);
});

test('challenge boot mismatch blocks identical-version drafts after restart', async () => {
  const api = new SyntheticApi();
  const session = await restored(api);
  api.challenge = async () => ({ token: 'a1', state_version: '22', expires_ms: 'ffff', boot_id: 'bb' });
  assert.equal((await session.submit('START')).status, 'STALE_SNAPSHOT');
  assert.equal(api.calls.command.length, 0);
});

test('503 and malformed successful reads revoke automatic actions; STOP remains available', async () => {
  for (const broken of ['503', 'malformed']) {
    const api = new SyntheticApi();
    const session = await restored(api);
    if (broken === '503') api.stateError = { kind: 'http', status: 503, code: 'BUSY' };
    else api.current = {};
    await session.poll();
    assert.equal(session.view.link, 'offline');
    assert.equal((await session.submit('START')).status, 'OFFLINE');
    assert.equal((await session.stop()).status, 'STOP_REQUESTED');
    assert.equal(api.calls.stop.length, 1);
  }
});

test('suspended browser cannot submit from a stale snapshot', async () => {
  const clock = { now: 0 };
  const api = new SyntheticApi();
  const session = await restored(api, clock);
  clock.now = 6_000;
  assert.equal((await session.submit('START')).status, 'OFFLINE');
  assert.equal(api.calls.challenge.length, 0);
});

test('unchanged existing fault does not cancel RESET_FAULT before its actual result', async () => {
  const api = new SyntheticApi(syntheticState({ faults: 1, state: { code: 'FAULT', ru: 'Авария' } }));
  const session = await restored(api);
  assert.equal((await session.submit('RESET_FAULT')).status, 'QUEUED');
  await waitForTasks();
  assert.equal(session.view.pending?.action, 'RESET_FAULT');
  api.current = syntheticState({ state_version: '23', faults: 0 });
  await session.poll();
  assert.equal(session.view.message.kind, 'ok');
});

test('OTA pending survives post-reboot authentication loss and checks expected version', async () => {
  for (const version of ['test-1.1.0', 'old-version']) {
    const api = new SyntheticApi();
    const session = await restored(api);
    await session.uploadOta(otaFile());
    await waitForTasks();
    api.stateError = { kind: 'auth', status: 401 };
    await session.poll();
    assert.equal(session.view.auth, 'anonymous');
    assert.equal(session.view.ota.phase, 'activation_queued');
    api.stateError = null;
    api.current = syntheticState({ boot_id: 'bb', state_version: '23', version });
    await session.login('synthetic-fixture-only');
    assert.equal(session.view.ota.phase, version === 'test-1.1.0' ? 'running_expected_version' : 'different_version');
  }
});

test('OTA single HTTP server upload is not invalidated by competing status polls', async () => {
  const clock = { now: 0 };
  const api = new SyntheticApi();
  let finish;
  api.sendOta = () => new Promise(resolve => { finish = resolve; });
  const session = await restored(api, clock);
  const uploading = session.uploadOta(otaFile());
  await waitForTasks();
  const reads = api.calls.state;
  clock.now = 20_000;
  session.tick();
  await session.poll();
  assert.equal(api.calls.state, reads);
  finish({ status: 'VERIFIED_ACTIVATION_QUEUED', expected_version: 'test-1.1.0' });
  assert.equal((await uploading).status, 'VERIFIED_ACTIVATION_QUEUED');
  await waitForTasks();
  assert.equal((await session.uploadOta(otaFile())).status, 'BUSY');
  clock.now = 140_001;
  session.tick();
  assert.equal(session.view.ota.phase, 'confirmation_timeout');
});
