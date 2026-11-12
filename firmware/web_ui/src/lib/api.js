import { isHexString, makeCommandId } from './protocol.js';
import { sha256Hex } from './sha256.js';

export class ApiError extends Error {
  constructor(kind, message, details = {}) {
    super(message);
    this.name = 'ApiError';
    this.kind = kind;
    Object.assign(this, details);
  }
}

function codeFromBody(body) {
  return body && typeof body === 'object' && typeof body.code === 'string' ? body.code : null;
}

function statusError(response, body) {
  const status = Number(response?.status) || 0;
  const code = codeFromBody(body);
  return new ApiError(status === 401 ? 'auth' : 'http', code || `HTTP ${status}`, { status, body, code });
}

function protocolError(message, body) {
  return new ApiError('protocol', message, { body, code: codeFromBody(body) });
}

function assertChallenge(body) {
  if (!body || body.status !== 'CHALLENGE') throw protocolError(body?.code || 'Неверный ответ challenge', body);
  for (const field of ['token', 'state_version', 'expires_ms', 'boot_id']) {
    if (!isHexString(body[field])) throw protocolError(`Некорректное шестнадцатеричное поле challenge: ${field}`, body);
  }
  return { token: body.token, state_version: body.state_version, expires_ms: body.expires_ms, boot_id: body.boot_id };
}

export class ApiClient {
  constructor({ fetchImpl = globalThis.fetch, timeoutMs = 3_500, hash = sha256Hex } = {}) {
    if (typeof fetchImpl !== 'function') throw new TypeError('fetch недоступен');
    this.fetchImpl = (path, options) => fetchImpl.call(globalThis, path, options);
    this.timeoutMs = timeoutMs;
    this.hash = hash;
  }

  async request(path, options = {}) {
    const controller = new AbortController();
    const timeout = Number.isFinite(options.timeoutMs) ? options.timeoutMs : this.timeoutMs;
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      controller.abort();
    }, timeout);
    try {
      let response;
      try {
        response = await this.fetchImpl(path, {
          credentials: 'same-origin',
          cache: 'no-store',
          ...options,
          signal: controller.signal,
        });
      } catch (cause) {
        throw new ApiError(timedOut ? 'timeout' : 'network', timedOut ? 'Истекло время ожидания сети' : 'Сетевая ошибка', { cause });
      }
      let raw = '';
      try {
        raw = await response.text();
      } catch (cause) {
        throw new ApiError('network', 'Не удалось прочитать ответ сервера', { cause, status: response.status });
      }
      let body;
      try {
        body = raw ? JSON.parse(raw) : {};
      } catch (_) {
        body = { status: 'HTTP_ERROR', code: raw || response.statusText };
      }
      if (!response.ok) throw statusError(response, body);
      return body;
    } finally {
      clearTimeout(timer);
    }
  }

  postJson(path, payload) {
    return this.request(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
  }

  async login(password) {
    const body = await this.request('/api/login', {
      method: 'POST',
      headers: { 'X-Sirokko-Password': password },
    });
    if (body.status !== 'OK') throw protocolError(body.code || 'Вход не подтверждён', body);
    return body;
  }

  state() {
    return this.request('/api/state', { method: 'GET' });
  }

  async saveWifi(password) {
    const body = await this.request('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream', 'X-Sirokko-Config': 'wifi' },
      body: password,
    });
    if (body.status !== 'SAVED') throw protocolError(body.code || 'Пароль не сохранён', body);
    return body;
  }

  async challenge(payload) {
    return assertChallenge(await this.postJson('/api/challenge', payload));
  }

  command(payload) {
    if (payload?.action === 'OTA') {
      throw new TypeError('OTA передаётся только через prepareOta() и sendOta(), а не /api/command');
    }
    if (!payload.grant) return this.postJson('/api/command', payload);
    const { token, state_version, expires_ms } = payload.grant;
    // boot_id is checked by the client, not an additional field in the strict command parser.
    return this.postJson('/api/command', { ...payload, grant: { token, state_version, expires_ms } });
  }

  stop(commandId = makeCommandId()) {
    return this.command({ action: 'STOP', command_id: commandId });
  }

  async prepareOta(file, commandId = makeCommandId()) {
    if (!file || typeof file.arrayBuffer !== 'function' || !Number.isSafeInteger(file.size) || file.size <= 0) {
      throw new TypeError('Выберите непустой файл .sota');
    }
    const raw = await file.arrayBuffer();
    const uploadSha256 = await this.hash(raw);
    const payload = {
      action: 'OTA',
      command_id: commandId,
      upload_size: file.size,
      upload_sha256: uploadSha256,
    };
    return { file, payload, grant: await this.challenge(payload) };
  }

  sendOta(prepared) {
    const { file, payload, grant } = prepared || {};
    if (!file || !payload || !grant) throw new TypeError('OTA не подготовлена');
    return this.request('/api/ota', {
      method: 'POST',
      timeoutMs: 120_000,
      headers: {
        'X-Sirokko-Token': grant.token,
        'X-Sirokko-State-Version': grant.state_version,
        'X-Sirokko-Expires': grant.expires_ms,
        'X-Sirokko-Command-Id': payload.command_id,
        'X-Sirokko-Upload-Size': String(payload.upload_size),
        'X-Sirokko-Upload-SHA256': payload.upload_sha256,
      },
      body: file,
    });
  }

  async uploadOta(file, commandId = makeCommandId()) {
    const prepared = await this.prepareOta(file, commandId);
    return { ...prepared, response: await this.sendOta(prepared) };
  }
}
