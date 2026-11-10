import { expect, test } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { SyntheticRouteApi } from '../fixtures/synthetic-route-api.js';
import { syntheticState } from '../fixtures/synthetic-state.js';

const fixtures = new WeakMap();
const audits = new WeakMap();

async function openLoggedIn(page) {
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Вход в точку доступа', exact: true })).toBeVisible();
  await page.getByLabel('Пароль AP').fill('12345678901234567890');
  await page.getByRole('button', { name: 'Войти', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Отопление', exact: true })).toBeVisible();
}

async function nav(page, name) {
  await page.getByRole('navigation').getByRole('button', { name, exact: true }).click();
}

async function openDetail(page, name) {
  await nav(page, 'Настройки');
  await page.getByRole('button', { name: new RegExp(`^${name}`) }).click();
}

async function waitForNextPoll(page) {
  await page.waitForTimeout(1_100);
}

test.beforeEach(async ({ page }) => {
  const fixture = new SyntheticRouteApi();
  fixtures.set(page, fixture);
  const audit = { errors: [], externalRequests: [] };
  audits.set(page, audit);
  page.on('pageerror', (error) => audit.errors.push(error.message));
  page.on('console', (message) => {
    const text = message.text();
    const expectedInitialAuthProbe = /server responded with a status of 401 \(Unauthorized\)/.test(text);
    const expectedOfflineFixture = fixture.offline && /Failed to load resource: net::ERR_FAILED/.test(text);
    if (message.type() === 'error' && !expectedInitialAuthProbe && !expectedOfflineFixture) audit.errors.push(text);
  });
  page.on('request', (request) => {
    const url = request.url();
    if (/^https?:/.test(url) && !url.startsWith('http://127.0.0.1:4173/') && !url.startsWith('http://sirokko.test/')) audit.externalRequests.push(url);
  });
  await page.route('**/api/**', (route) => fixture.handle(route));
});

test.afterEach(async ({ page }) => {
  expect(audits.get(page)).toEqual({ errors: [], externalRequests: [] });
});

test('synthetic fixture: production page starts at login, has no demo controls, and renders actual freshness', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.state = syntheticState({ sensors: { air: { milli_c: 25_000, sample_ms: 1, status: 0, valid: true, fresh: false } } });
  await openLoggedIn(page);
  await expect(page).toHaveTitle('Sirokko 262.01');
  await expect(page.locator('body')).not.toContainText('Демо');
  await openDetail(page, 'Показания и журнал');
  await expect(page.locator('.readings')).toContainText('25,0 °C · устарело');
  await expect(page.locator('meta[http-equiv="Content-Security-Policy"]')).toHaveAttribute('content', /connect-src 'self'/);
});

test('synthetic fixture: dirty settings freeze on an external state-version change instead of overwriting local input', async ({ page }) => {
  const fixture = fixtures.get(page);
  await openLoggedIn(page);
  await page.getByRole('button', { name: 'Увеличить температуру' }).click();
  await expect(page.getByText('Не применено', { exact: true })).toBeVisible();
  fixture.state = syntheticState({
    state_version: '23',
    settings: { room_setpoint_milli_c: 20_000, duration_ms: 0, mode: 'TEMPERATURE', manual_percent: 45, slew_up_per_s: 0.001, slew_down_per_s: 0.001 },
  });
  await waitForNextPoll(page);
  await expect(page.getByText('Настройки изменены внешне', { exact: true })).toBeVisible();
  await expect(page.getByTestId('setpoint')).toHaveText('23°C');
});

test('synthetic fixture: shutdown blocks START and never queues a restart after cooling', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.state = syntheticState({ state: { code: 'STOP_PURGE', ru: 'Охлаждение' }, start_block: { code: 'BUSY', ru: 'Идёт охлаждение' }, purge: { active: true }, outputs: { fan_request: true } });
  await openLoggedIn(page);
  const start = page.getByRole('button', { name: 'Запустить отопление', exact: true });
  await expect(start).toHaveCount(0);
  for (const code of ['FAULT_PURGE', 'FAULT', 'RECOVERY_PURGE']) {
    fixture.state = syntheticState({ state: { code, ru: code }, start_block: { code: 'BUSY', ru: 'Пуск запрещён' } });
    await waitForNextPoll(page);
    await expect(start).toHaveCount(0);
  }
  fixture.state = syntheticState({ state_version: '24' });
  await expect(start).toBeEnabled();
  expect(fixture.commands).toHaveLength(0);
  expect(fixture.challenges).toHaveLength(0);
  await start.click();
  await expect.poll(() => fixture.commands.filter(command => command.action === 'START').length).toBe(1);
});

test('synthetic fixture: queued command confirms only from newer matching state', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.onCommand = (payload) => {
    fixture.state = syntheticState({
      state_version: '23',
      run_id: '1',
      state: { code: 'PREPURGE', ru: 'Предпродувка' },
      outputs: { fan_request: true, speed_code: 240, glow: false, fuel: false, command_percent: 20 },
    });
    return { status: 'QUEUED' };
  };
  await openLoggedIn(page);
  await page.getByRole('button', { name: 'Запустить отопление', exact: true }).click();
  await expect(page.getByRole('status')).toContainText('Новый снимок соответствует запросу.');
  await expect(page.locator('.device-status')).toContainText('Предпродувка');
});

test('synthetic fixture: five-second pending timeout is unknown, not rejected', async ({ page }) => {
  await openLoggedIn(page);
  await page.getByRole('button', { name: 'Запустить отопление', exact: true }).click();
  await expect(page.locator('.feedback')).toContainText('Команда принята в очередь');
  await expect(page.locator('.feedback')).toContainText('Результат неизвестен, а не отклонён.', { timeout: 7_000 });
});

test('synthetic fixture: STOP bypasses a pending challenge and does not revive the old action', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.deferChallenge = true;
  await openLoggedIn(page);
  await page.getByRole('button', { name: 'Увеличить температуру' }).click();
  await page.getByRole('button', { name: 'Применить', exact: true }).click();
  await expect(page.getByRole('status')).toContainText('Получаем одноразовое подтверждение');
  await page.getByRole('button', { name: 'Передать СТОП', exact: true }).click();
  await expect.poll(() => fixture.stops.length).toBe(1);
  fixture.releaseChallenge();
  await page.waitForTimeout(100);
  expect(fixture.commands).toEqual([]);
  await expect(page.getByRole('status')).toContainText('СТОП передан автомату');
});

test('synthetic fixture: boot change and reconnect cancel old work without queued retry', async ({ page }) => {
  const fixture = fixtures.get(page);
  await openLoggedIn(page);
  await page.getByRole('button', { name: 'Запустить отопление', exact: true }).click();
  await expect(page.locator('.feedback')).toContainText('Команда принята в очередь');
  fixture.state = syntheticState({ boot_id: 'bb', state_version: '23' });
  await waitForNextPoll(page);
  await expect(page.locator('.feedback')).toContainText('Контроллер перезапущен');
  fixture.offline = true;
  await waitForNextPoll(page);
  await expect(page.getByTestId('connection-status')).toHaveText('Нет связи');
  fixture.offline = false;
  fixture.state = syntheticState({ boot_id: 'bb', state_version: '24' });
  await waitForNextPoll(page);
  await expect(page.getByTestId('connection-status')).toHaveText('Связь подтверждена');
  expect(fixture.commands).toHaveLength(1);
});

test('synthetic fixture: a single timer maps to SETTINGS in IDLE and TIMER in an active run', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.onCommand = (payload) => {
    if (payload.action === 'SETTINGS') {
      fixture.state = syntheticState({ state_version: '23', settings: payload.settings });
    } else if (payload.action === 'TIMER') {
      fixture.state = syntheticState({
        state_version: '25',
        run_id: '1',
        state: { code: 'RUN', ru: 'Работа' },
        timer: { active: true, remaining_ms: payload.delay_ms, deadline_ms: 121_000, saved_duration_ms: 120_000 },
      });
    }
    return { status: 'QUEUED' };
  };
  await openLoggedIn(page);
  await nav(page, 'Таймер');
  await page.getByLabel('Минуты таймера').fill('2');
  await page.getByRole('button', { name: 'Сохранить таймер', exact: true }).click();
  await expect.poll(() => fixture.commands.length).toBe(1);
  expect(fixture.commands[0]).toMatchObject({ action: 'SETTINGS', settings: { duration_ms: 120_000 } });
  expect(JSON.stringify(fixture.commands[0])).not.toContain('ARM');
  await expect(page.locator('.feedback')).toContainText('Новый снимок соответствует запросу.');

  fixture.state = syntheticState({
    state_version: '24',
    run_id: '1',
    state: { code: 'RUN', ru: 'Работа' },
    timer: { active: false, remaining_ms: 0, deadline_ms: 0, saved_duration_ms: 120_000 },
  });
  await waitForNextPoll(page);
  await page.getByLabel('Минуты таймера').fill('2');
  await page.getByRole('button', { name: 'Установить таймер', exact: true }).click();
  await expect.poll(() => fixture.commands.length).toBe(2);
  expect(fixture.commands[1]).toMatchObject({ action: 'TIMER', delay_ms: 120_000 });
});

test('synthetic fixture: explicit commissioning block is visible and never reaches a challenge', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.state = syntheticState({
    commissioning: {
      block: {
        code: 'CALIBRATION_RANGE_INVALID',
        ru: 'Диапазон исследования калибровки недопустим',
      },
    },
  });
  await openLoggedIn(page);
  await openDetail(page, 'Ввод в эксплуатацию');
  const blocked = page.getByRole('button', { name: 'Ввод в эксплуатацию недоступен', exact: true });
  await expect(blocked).toBeDisabled();
  await expect(page.getByTestId('commissioning-availability')).toContainText('Диапазон исследования калибровки недопустим');
  await expect(page.getByTestId('commissioning-availability')).toContainText('CALIBRATION_RANGE_INVALID');
  expect(fixture.challenges).toEqual([]);
  expect(fixture.commands).toEqual([]);
});

test('synthetic fixture: model correction sends only MODEL_REVISION and confirms exact readback', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.onCommand = (payload) => {
    if (payload.action === 'MODEL_REVISION') {
      fixture.state = syntheticState({
        state_version: '23',
        learned_model: {
          minimum_speed_code: payload.revision.minimum_speed_code,
          maximum_speed_code: payload.revision.maximum_speed_code,
          slew_up_per_s: payload.revision.slew_up_per_s,
          slew_down_per_s: payload.revision.slew_down_per_s,
          provenance: { revision: payload.revision.expected_revision + 1 },
        },
      });
    }
    return { status: 'QUEUED' };
  };
  await openLoggedIn(page);
  await openDetail(page, 'Ввод в эксплуатацию');
  await page.getByLabel('Минимум PWM').fill('200');
  await page.getByLabel('Максимум PWM').fill('800');
  await page.getByLabel('Нарастание, код/с').fill('0.0008');
  await page.getByLabel('Снижение, код/с').fill('0.0007');
  await page.getByRole('button', { name: 'Сохранить коррекцию модели', exact: true }).click();
  await expect(page.locator('.feedback')).toContainText('Новый снимок соответствует запросу');
  expect(fixture.challenges[0]).toMatchObject({ action: 'MODEL_REVISION', revision: {
    expected_revision: 1, minimum_speed_code: 200, maximum_speed_code: 800, slew_up_per_s: 0.0008, slew_down_per_s: 0.0007,
  } });
  expect(fixture.commands[0]).toMatchObject(fixture.challenges[0]);
});

test('synthetic fixture: OTA stale challenge never uploads over a TFT-updated snapshot', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.challengeVersion = '23';
  await openLoggedIn(page);
  await openDetail(page, 'Обновление');
  await page.getByLabel('Пакет прошивки').setInputFiles({ name: 'fixture.sota', mimeType: 'application/octet-stream', buffer: Buffer.from('abc') });
  await page.getByRole('button', { name: 'Проверить и загрузить OTA', exact: true }).click();
  await expect(page.locator('.feedback')).toContainText('Состояние изменилось на другой панели. OTA не отправлена');
  expect(fixture.otaHeaders).toEqual([]);
});

test('synthetic fixture: OTA uses SHA fallback and rejects the retired success status', async ({ page }) => {
  const fixture = fixtures.get(page);
  await page.addInitScript(() => {
    try { Object.defineProperty(globalThis.crypto, 'subtle', { value: undefined, configurable: true }); } catch (_) {}
  });
  fixture.otaResponse = { status: 'VERIFIED_REBOOTING' };
  await openLoggedIn(page);
  await openDetail(page, 'Обновление');
  const input = page.getByLabel('Пакет прошивки');
  await input.setInputFiles({ name: 'fixture.sota', mimeType: 'application/octet-stream', buffer: Buffer.from('abc') });
  await page.getByRole('button', { name: 'Проверить и загрузить OTA', exact: true }).click();
  await expect(page.locator('.feedback')).toContainText('OTA не подтверждена: VERIFIED_REBOOTING');
  expect(fixture.otaHeaders[0].sha256).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');

  fixture.otaResponse = { status: 'VERIFIED_ACTIVATION_QUEUED' };
  await page.getByRole('button', { name: 'Проверить и загрузить OTA', exact: true }).click();
  await expect(page.getByTestId('ota-status')).toContainText('Подпись проверена; активация поставлена в очередь');
  await expect(page.locator('.feedback')).toContainText('образ ещё не объявлен установленным');
});

test('OTA view keeps the global fault banner and warning above a matching version observation', async ({ page }, testInfo) => {
  const fixture = fixtures.get(page);
  fixture.otaResponse = { status: 'VERIFIED_ACTIVATION_QUEUED', expected_version: 'test-next' };
  await openLoggedIn(page);
  await openDetail(page, 'Обновление');
  await page.getByLabel('Пакет прошивки').setInputFiles({ name: 'fixture.sota', mimeType: 'application/octet-stream', buffer: Buffer.from('abc') });
  await page.getByRole('button', { name: 'Проверить и загрузить OTA', exact: true }).click();
  await expect(page.getByTestId('ota-status')).toContainText('активация поставлена в очередь');
  fixture.state = syntheticState({ boot_id: 'bb', version: 'test-next', state_version: '23', faults: 1, state: { code: 'FAULT', ru: 'Авария' } });
  await expect(page.getByRole('alert')).toContainText('Автомат сообщает аварию');
  await expect(page.locator('.feedback')).toHaveClass(/warning/);
  await expect(page.locator('.feedback')).toContainText('аварию');
  await expect(page.getByTestId('ota-status')).toContainText('test-next');
  await page.evaluate(() => window.scrollTo(0, document.documentElement.scrollHeight));
  await expect.poll(async () => page.evaluate(() => {
    const status = document.querySelector('[data-testid="ota-status"]').getBoundingClientRect();
    const notice = document.querySelector('.feedback').getBoundingClientRect();
    return status.bottom <= notice.top;
  })).toBe(true); // Last content can scroll above both fixed STOP and multi-line notice.
  await page.screenshot({ path: `evidence/screenshots/${testInfo.project.name}-ota-fault.png`, fullPage: true });
});

test('synthetic fixture: layout remains within 320 px at 200% text', async ({ page }) => {
  await openLoggedIn(page);
  await page.setViewportSize({ width: 320, height: 736 });
  await page.evaluate(() => { document.documentElement.style.fontSize = '200%'; });
  const dimensions = await page.evaluate(() => ({ width: innerWidth, document: document.documentElement.scrollWidth, body: document.body.scrollWidth }));
  expect(dimensions.document).toBeLessThanOrEqual(dimensions.width + 1);
  expect(dimensions.body).toBeLessThanOrEqual(dimensions.width + 1);
});

test('actual serializer fixture: production gzip renders real JSON fields, journal and blocked sensor freshness', async ({ page }, testInfo) => {
  fixtures.get(page).state = JSON.parse(readFileSync('../build-host/web-contract/state.json', 'utf8'));
  await openLoggedIn(page);
  await expect(page.locator('.current-reading')).toContainText('21,0 °C');
  if (testInfo.project.name === 'chromium') await page.screenshot({ path: 'evidence/production-home.png', fullPage: true });
  await nav(page, 'Таймер');
  await expect(page.getByRole('heading', { name: 'Отключить через' })).toBeVisible();
  if (testInfo.project.name === 'chromium') await page.screenshot({ path: 'evidence/production-timer.png', fullPage: true });
  await openDetail(page, 'Показания и журнал');
  await expect(page.locator('.readings')).toContainText('25,0 °C · устарело');
  await expect(page.locator('.event-list')).toContainText('Запуск принят автоматом');
  if (testInfo.project.name === 'chromium') await page.screenshot({ path: 'evidence/production-diagnostics.png', fullPage: true });
});

test('approved slew screen sends complete SETTINGS and confirms the readback', async ({ page }) => {
  const fixture = fixtures.get(page);
  fixture.onCommand = payload => {
    fixture.state = syntheticState({ state_version: '23', settings: { ...payload.settings, slew_up_per_s: 0.0007 } });
    return { status: 'QUEUED' };
  };
  await openLoggedIn(page);
  await openDetail(page, 'Плавность регулирования');
  await page.getByLabel('Нарастание, %/с').fill('0.07');
  await page.getByRole('button', { name: 'Сохранить плавность' }).click();
  await expect(page.locator('.feedback')).toContainText('Новый снимок соответствует запросу');
  expect(fixture.commands[0].settings).toMatchObject({ duration_ms: 0, mode: 'TEMPERATURE', manual_percent: 45 });
  expect(fixture.commands[0].settings.slew_up_per_s).toBeCloseTo(0.0007, 10);
});

test('actual HTTP AP origin model: no secure context, offline SHA-256 fallback still hashes complete upload', async ({ page }) => {
  const html = gunzipSync(readFileSync('../build-host/web-contract/page.bin'));
  await page.route('http://sirokko.test/', route => route.fulfill({ status: 200, contentType: 'text/html; charset=utf-8', body: html }));
  await page.goto('http://sirokko.test/');
  expect(await page.evaluate(() => isSecureContext)).toBe(false);
  expect(await page.evaluate(() => Boolean(crypto.subtle))).toBe(false);
  await page.getByLabel('Пароль AP').fill('synthetic-test-only');
  await page.getByRole('button', { name: 'Войти', exact: true }).click();
  await openDetail(page, 'Обновление');
  await page.getByLabel('Пакет прошивки').setInputFiles({ name: 'fixture.sota', mimeType: 'application/octet-stream', buffer: Buffer.from('abc') });
  await page.getByRole('button', { name: 'Проверить и загрузить OTA', exact: true }).click();
  await expect(page.getByTestId('ota-status')).toContainText('активация поставлена в очередь');
  expect(fixtures.get(page).otaHeaders[0].sha256).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
});

test('dirty timer can be explicitly refreshed after external edit; STOP stays visible in maintenance', async ({ page }) => {
  const fixture = fixtures.get(page);
  await openLoggedIn(page);
  await nav(page, 'Таймер');
  await page.getByLabel('Минуты таймера').fill('2');
  fixture.state = syntheticState({ state_version: '23', settings: { ...fixture.state.settings, duration_ms: 60000 } });
  await expect(page.getByRole('button', { name: 'Перечитать таймер' })).toBeVisible();
  await page.getByRole('button', { name: 'Перечитать таймер' }).click();
  await expect(page.getByLabel('Минуты таймера')).toHaveValue('1');
  fixture.state = syntheticState({ state_version: '24', state: { code: 'MAINTENANCE', ru: 'Обслуживание' } });
  await nav(page, 'Главная');
  await expect(page.locator('.persistent-stop')).toContainText('Обслуживание');
  await page.locator('.persistent-stop').getByRole('button').click();
  await expect.poll(() => fixture.stops.length).toBe(1);
});
