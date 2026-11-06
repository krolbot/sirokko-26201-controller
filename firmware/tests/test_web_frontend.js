#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto').webcrypto;
const fs = require('node:fs');
const vm = require('node:vm');

const sourcePath = process.argv[2] || 'firmware/main/web.cpp';
const source = fs.readFileSync(sourcePath, 'utf8');
const begin = source.indexOf('R"SIROKKO(');
const end = source.indexOf(')SIROKKO"', begin);
assert(begin >= 0 && end > begin, 'production web.cpp must contain the self-contained page raw string');
const html = source.slice(begin + 'R"SIROKKO('.length, end);
const scriptMatch = html.match(/<script>([\s\S]*)<\/script>/);
assert(scriptMatch, 'production page must contain its frontend script');
assert(!/<script[^>]+\bsrc=/i.test(html) && !/<link[^>]+\bhref=/i.test(html), 'frontend is offline self-contained: no external script or stylesheet');

class FakeClassList {
  constructor() { this.values = new Set(); }
  toggle(name, force) {
    const enabled = force === undefined ? !this.values.has(name) : Boolean(force);
    if (enabled) this.values.add(name); else this.values.delete(name);
  }
  contains(name) { return this.values.has(name); }
}

class FakeElement {
  constructor(id = '') {
    this.id = id;
    this.classList = new FakeClassList();
    this.textContent = '';
    this.value = '';
    this.disabled = false;
    this.hidden = false;
    this.children = [];
    this.listeners = new Map();
    this.files = [];
  }
  addEventListener(type, callback) {
    const list = this.listeners.get(type) || [];
    list.push(callback);
    this.listeners.set(type, list);
  }
  emit(type) { for (const callback of this.listeners.get(type) || []) callback({target: this}); }
  appendChild(child) { this.children.push(child); return child; }
  removeChild(child) {
    const index = this.children.indexOf(child);
    if (index >= 0) this.children.splice(index, 1);
    return child;
  }
  get firstChild() { return this.children[0] || null; }
}

const elements = new Map();
for (const match of html.matchAll(/\bid="([^"]+)"/g)) elements.set(match[1], new FakeElement(match[1]));
const settingIds = [...html.matchAll(/\bid="([^"]+)"[^>]*\bdata-setting\b/g)].map(match => match[1]);
const onlineIds = [...html.matchAll(/<button\b[^>]*\bdata-online\b[^>]*>/g)].map((_, index) => `online-${index}`);
assert(onlineIds.length >= 6, 'production UI must mark all auto-action controls with data-online');
for (const id of onlineIds) elements.set(id, new FakeElement(id));
const document = {
  getElementById(id) {
    if (!elements.has(id)) elements.set(id, new FakeElement(id));
    return elements.get(id);
  },
  querySelectorAll(selector) {
    if (selector === '[data-setting]') return settingIds.map(id => document.getElementById(id));
    if (selector === '[data-online]') return onlineIds.map(id => document.getElementById(id));
    return [];
  },
  createElement() { return new FakeElement(); },
};

const context = {
  console,
  crypto,
  document,
  Uint32Array, Uint8Array, ArrayBuffer,
  Number,
  JSON,
  Date,
  Math,
  setInterval: () => 1,
  clearInterval: () => {},
};
context.window=context;
vm.createContext(context);
vm.runInContext(`${scriptMatch[1]}\nglobalThis.__sirokkoUi = {render, setOnline, settingsPayload, hashImage};`, context, {filename: 'sirokko-ui.js'});
const ui = context.__sirokkoUi;

const state = {
  version: 'test-fw',
  state_version: '22',
  state: {code: 'IDLE', ru: 'Ожидание'},
  start_block: {code: 'NONE', ru: 'Нет'},
  storage_ok: true,
  faults: 0,
  outputs: {fan_request: false, speed_code: 111, glow: false, fuel: false, command_percent: 37.5},
  sensors: {
    room: {valid: true, milli_c: 21000, status: 0},
    exhaust: {valid: true, milli_c: 50000, status: 0},
    air: {valid: true, milli_c: 25000, status: 0},
  },
  range: {calibrated: true, minimum_speed_code: 100, maximum_speed_code: 900, working_span_fraction: 0.4},
  room_excess: false,
  timer: {active: true, remaining_ms: 61000},
  delayed: {armed: false, remaining_ms: 0},
  purge: {active: true, minimum_remaining_ms: 5000, sensors_fresh: true, temperature_ready: false},
  calibration: {active: false, phase_ru: 'ожидание', reason_ru: 'нет причины', points: 0, elapsed_ms: 0},
  process: {configured: true, physical_tests: false},
  settings: {room_setpoint_milli_c: 20000, duration_ms: 3600000, mode: 'TEMPERATURE', manual_percent: 50, slew_up_per_s: 0.005, slew_down_per_s: 0.004},
  events: [{at_ms: 123, kind: 'START', kind_ru: 'Запуск принят автоматом', state: 'IDLE', state_ru: 'Ожидание', speed_code: 111, exhaust_milli_c: 50000, air_milli_c: 25000, faults: 0}],
};

ui.render(state);
assert.equal(document.getElementById('state').textContent, 'Ожидание (IDLE)');
assert.equal(document.getElementById('room').textContent, '21.0 °C');
assert.match(document.getElementById('command').textContent, /^37\.5 %; PWM 111$/);
assert.equal(document.getElementById('profileStatus').textContent, 'configured=true; physical_tests=false');
assert.equal(document.getElementById('events').children.length, 1, 'actual render() appends event journal row');
assert.equal(document.getElementById('setpoint').value, '20', 'readback fills clean editable fields');

const setpoint = document.getElementById('setpoint');
setpoint.value = '25';
setpoint.emit('input');
const changed = structuredClone(state);
changed.settings.room_setpoint_milli_c = 15000;
ui.render(changed);
assert.equal(setpoint.value, '25', 'readback never overwrites a dirty settings field');

ui.setOnline(false);
assert.equal(document.getElementById('stale').classList.contains('hidden'), false, 'offline status is visually stale');
for (const id of onlineIds) assert.equal(document.getElementById(id).disabled, true, `${id} is disabled when stale`);
assert.equal(document.getElementById('stopButton').disabled, false, 'STOP stays separately available when stale');

ui.setOnline(true);
for (const id of onlineIds) assert.equal(document.getElementById(id).disabled, false, `${id} is re-enabled only after fresh state`);
assert.equal(document.getElementById('stale').classList.contains('hidden'), true, 'stale warning clears only on fresh state');

async function verifyHashPaths() {
  const nativeHash=require('node:crypto').createHash;
  for(const length of [0,3,55,56,63,64,65,1000,1048576]) {
    const bytes=Uint8Array.from({length},(_,i)=>i%251);
    const expected=nativeHash('sha256').update(bytes).digest('hex');
    context.crypto=crypto;
    assert.equal(await ui.hashImage(bytes.buffer),expected,'Web Crypto path');
    context.crypto={getRandomValues:crypto.getRandomValues.bind(crypto)};
    assert.equal(await ui.hashImage(bytes.buffer),expected,'plain HTTP AP fallback without crypto.subtle');
  }
  console.log('PASS: production Russian frontend, dirty fields, stale actions and SHA-256 over plain HTTP AP');
}
verifyHashPaths().catch(error=>{console.error(error);process.exitCode=1;});
