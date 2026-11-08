#!/usr/bin/env node
'use strict';
// Contract check of the exact resource and JSON emitted by production web.cpp.
// State values come from explicit host fixtures, not from a physical controller.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { gunzipSync } = require('node:zlib');
const { createHash, webcrypto } = require('node:crypto');

async function main() {
  const firmware = path.resolve(__dirname, '..');
  const exported = process.argv[2];
  assert(exported, 'actual-handler export directory is required');
  const read = name => fs.readFileSync(path.join(exported, name));
  const source = fs.readFileSync(path.join(firmware, 'main/web.cpp'), 'utf8');
  assert(source.includes('#include "web_ui_data.inc"') && !source.includes('R"SIROKKO('));
  const embedded = fs.readFileSync(path.join(firmware, 'main/web_ui_data.inc'), 'utf8');
  const bytes = Buffer.from([...embedded.matchAll(/0x([0-9a-f]{2})/g)].map(match => parseInt(match[1], 16)));
  assert.deepEqual(read('page.bin'), bytes, 'handler must send the current embedded bytes');
  const html = gunzipSync(bytes);
  const declared = embedded.match(/HTML SHA-256: ([0-9a-f]{64})/)[1];
  assert.equal(createHash('sha256').update(html).digest('hex'), declared);
  const dist = path.join(firmware, 'web_ui/dist/index.html');
  if (fs.existsSync(dist)) assert.deepEqual(html, fs.readFileSync(dist), 'shipped page equals built frontend');
  const text = html.toString('utf8');
  assert(text.includes('Связь потеряна') && text.includes('.sota') && text.includes('VERIFIED_ACTIVATION_QUEUED'));
  assert(!/CANCEL_ARM|VERIFIED_REBOOTING|<script[^>]+\bsrc\s*=/i.test(text));
  const headers = JSON.parse(read('headers.json'));
  assert.equal(headers['Content-Encoding'], 'gzip');
  assert.match(headers['Content-Security-Policy'], /font-src 'self' data:/);
  assert.match(headers['Content-Security-Policy'], /connect-src 'self'/);

  const load = relative => import(pathToFileURL(path.join(firmware, 'web_ui/src/lib', relative)));
  const { fullSettingsPayload, validateProcessProfile, sensorText } = await load('protocol.js');
  const { ControllerSession } = await load('controller.js');
  const { sha256Hex } = await load('sha256.js');
  const actual = JSON.parse(read('state.json'));
  assert.equal(actual.state.ru, 'Ожидание');
  assert.equal(actual.process.physical_tests, false);
  assert.deepEqual(actual.calibration.block, { code: 'NONE', ru: 'Нет' });
  assert.equal(actual.events[0].kind_ru, 'Запуск принят автоматом');
  assert.deepEqual(fullSettingsPayload(actual.settings), actual.settings);
  assert.deepEqual(validateProcessProfile(actual.process.profile), actual.process.profile);
  assert.equal(sensorText(actual.sensors.room), '21,0 °C');
  assert.match(sensorText(actual.sensors.air), /устарело/);
  const otaResponse = JSON.parse(read('ota-response.json'));
  const session = new ControllerSession({ api: {
    state: async () => actual,
    prepareOta: async () => ({ grant: { state_version: actual.state_version, boot_id: actual.boot_id }, expectedVersion: 'test-new-version' }),
    sendOta: async () => otaResponse,
  }, commandId: () => '123' });
  await session.restore();
  assert.equal((await session.uploadOta({})).status, 'VERIFIED_ACTIVATION_QUEUED');
  assert.equal(session.view.ota.phase, 'activation_queued', 'actual handler response is queued, never installed');

  for (const length of [0, 3, 55, 56, 63, 64, 65, 1000, 1048576]) {
    const vector = Uint8Array.from({ length }, (_, i) => i % 251);
    const expected = createHash('sha256').update(vector).digest('hex');
    assert.equal(await sha256Hex(vector, webcrypto), expected, 'native SHA');
    assert.equal(await sha256Hex(vector, {}), expected, 'plain-HTTP SHA fallback');
  }
  console.log('PASS: production HTTP gzip/headers/JSON/OTA contract, complete settings/profile and both SHA paths');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
