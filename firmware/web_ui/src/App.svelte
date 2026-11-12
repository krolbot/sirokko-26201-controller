<script>
  import { onMount } from 'svelte';
  import Icon from './Icon.svelte';
  import { ApiClient } from './lib/api.js';
  import { ControllerSession } from './lib/controller.js';
  import {
    commissioningAllowed,
    commissioningBlock,
    cloneJson,
    deepEqual,
    formatDuration,
    fullSettingsPayload,
    hasFault,
    isActiveSession,
    isRecord,
    modelRevisionPayload,
    millisecondsFromMinutes,
    minutesFromMilliseconds,
    sensorText,
  } from './lib/protocol.js';

  let tab = $state('home');
  let detail = $state('');
  let dark = $state(false);
  let ui = $state({ auth: 'anonymous', link: 'offline', snapshot: null, pending: null, requestInFlight: null, message: null, ota: { phase: 'idle' } });
  let password = $state('');
  let wifiPassword = $state('');
  let wifiConfirmation = $state('');
  let wifiSaving = $state(false);
  let settingsDraft = $state(null);
  let settingsDirty = $state([]);
  let settingsBaseVersion = $state(null);
  let settingsConflict = $state(false);
  let timerMinutes = $state(0);
  let timerDirty = $state(false);
  let timerBaseVersion = $state(null);
  let timerConflict = $state(false);
  let modelDraft = $state(null);
  let modelDirty = $state(false);
  let modelBaseVersion = $state(null);
  let modelConflict = $state(false);

  let otaFile = $state(null);
  let localMessage = $state(null);
  let bottomNav = $state();
  let stopPanel = $state();
  let feedbackPanel = $state();
  let session;

  const snapshot = $derived(ui.snapshot);
  const authenticated = $derived(ui.auth === 'authenticated');
  const online = $derived(ui.link === 'online');
  const active = $derived(isActiveSession(snapshot));
  const automaticReady = $derived(authenticated && online && !ui.pending && !ui.requestInFlight && ui.ota.phase !== 'activation_queued');
  const feedback = $derived(localMessage ?? ui.message);
  const timerMode = $derived(timerContext(snapshot));

  $effect(() => {
    document.documentElement.dataset.theme = dark ? 'dark' : 'light';
    document.documentElement.style.colorScheme = dark ? 'dark' : 'light';
  });

  $effect(() => {
    if (!bottomNav) return;
    const update = () => {
      document.documentElement.style.setProperty('--nav-height', `${bottomNav.getBoundingClientRect().height}px`);
      document.documentElement.style.setProperty('--action-height', `${stopPanel?.getBoundingClientRect().height || 0}px`);
      document.documentElement.style.setProperty('--feedback-height', `${feedbackPanel?.getBoundingClientRect().height || 0}px`);
    };
    const observer = new ResizeObserver(update);
    observer.observe(bottomNav);
    if (stopPanel) observer.observe(stopPanel);
    if (feedbackPanel) observer.observe(feedbackPanel);
    update();
    return () => observer.disconnect();
  });

  function message(kind, text) {
    localMessage = { kind, text };
  }

  async function saveWifi(remove = false) {
    if (!automaticReady || wifiSaving) return;
    const next = remove ? '' : wifiPassword;
    if (!remove && (!/^[\x21-\x7e]{8,63}$/.test(next) || next !== wifiConfirmation)) {
      message('error', 'Введите одинаковый пароль в оба поля: 8–63 латинских букв, цифр или символов без пробелов.');
      return;
    }
    wifiSaving = true;
    try {
      await session.api.saveWifi(next);
      wifiPassword = ''; wifiConfirmation = '';
      message('success', 'Сохранено. Настройка Wi-Fi и входа в браузере применится после следующего включения питания.');
      await session.poll();
    } catch (error) {
      message('error', `Не удалось сохранить пароль: ${error.code || error.message}`);
    } finally {
      wifiSaving = false;
    }
  }

  function navigate(to) {
    tab = to;
    detail = '';
    window.scrollTo({ top: 0, behavior: 'instant' });
  }

  function openDetail(to) {
    tab = 'settings';
    detail = to;
    window.scrollTo({ top: 0, behavior: 'instant' });
  }

  function timerContext(state) {
    if (!state) return 'unavailable';
    if (isActiveSession(state)) return 'active';
    return state?.state?.code === 'IDLE' ? 'next' : 'unavailable';
  }

  function stateLabel(state) {
    if (!state?.state) return 'Нет снимка';
    return `${state.state.ru || state.state.code || 'Неизвестно'}${state.state.code ? ` (${state.state.code})` : ''}`;
  }

  function blockLabel(state) {
    if (!state?.start_block) return '—';
    return `${state.start_block.ru || state.start_block.code || '—'}${state.start_block.code ? ` (${state.start_block.code})` : ''}`;
  }

  function settingsValue(field, fallback = '') {
    return settingsDraft?.[field] ?? fallback;
  }

  function draftValue(field, value) {
    if (!settingsDraft) return;
    settingsDraft = { ...settingsDraft, [field]: value };
    if (!settingsDirty.includes(field)) settingsDirty = [...settingsDirty, field];
    settingsConflict = settingsBaseVersion !== snapshot?.state_version;
  }

  function chooseMode(mode) {
    if (!automaticReady || settingsConflict) return;
    draftValue('mode', mode);
  }

  function restoreSettings() {
    if (!snapshot?.settings) return;
    settingsDraft = cloneJson(snapshot.settings);
    settingsDirty = [];
    settingsBaseVersion = snapshot.state_version;
    settingsConflict = false;
  }

  function reconcileSettings(state, confirmedAction) {
    if (!isRecord(state?.settings)) return;
    const incoming = cloneJson(state.settings);
    if (!settingsDraft || confirmedAction === 'SETTINGS') {
      settingsDraft = incoming;
      settingsDirty = [];
      settingsBaseVersion = state.state_version;
      settingsConflict = false;
      return;
    }
    if (settingsDirty.length === 0) {
      settingsDraft = incoming;
      settingsBaseVersion = state.state_version;
      settingsConflict = false;
      return;
    }
    if (deepEqual(settingsDraft, incoming)) {
      settingsDraft = incoming;
      settingsDirty = [];
      settingsBaseVersion = state.state_version;
      settingsConflict = false;
      return;
    }
    const merged = { ...settingsDraft };
    for (const [key, value] of Object.entries(incoming)) {
      if (!settingsDirty.includes(key)) merged[key] = value;
    }
    settingsDraft = merged;
    if (settingsBaseVersion !== state.state_version) settingsConflict = true;
  }

  function reconcileTimer(state, confirmedAction) {
    if (!state) return;
    if (confirmedAction === 'TIMER' || confirmedAction === 'SETTINGS' || !timerDirty) {
      timerMinutes = timerContext(state) === 'active'
        ? minutesFromMilliseconds(state?.timer?.remaining_ms)
        : minutesFromMilliseconds(state?.settings?.duration_ms);
      timerDirty = false;
      timerConflict = false;
      timerBaseVersion = state.state_version;
      return;
    }
    if (timerBaseVersion !== state.state_version) timerConflict = true;
  }

  function restoreModel() {
    if (!snapshot?.learned_model?.valid || !isRecord(snapshot.learned_model.provenance)) return;
    modelDraft = {
      minimum_speed_code: snapshot.learned_model.minimum_speed_code,
      maximum_speed_code: snapshot.learned_model.maximum_speed_code,
      slew_up_per_s: snapshot.learned_model.slew_up_per_s,
      slew_down_per_s: snapshot.learned_model.slew_down_per_s,
    };
    modelDirty = false;
    modelConflict = false;
    modelBaseVersion = snapshot.state_version;
  }

  function reconcileModel(state, confirmedAction) {
    const model = state?.learned_model;
    if (!model?.valid || !isRecord(model.provenance)) {
      modelDraft = null;
      modelDirty = false;
      modelConflict = false;
      modelBaseVersion = state?.state_version ?? null;
      return;
    }
    if (!modelDraft || confirmedAction === 'MODEL_REVISION' || !modelDirty) {
      modelDraft = {
        minimum_speed_code: model.minimum_speed_code,
        maximum_speed_code: model.maximum_speed_code,
        slew_up_per_s: model.slew_up_per_s,
        slew_down_per_s: model.slew_down_per_s,
      };
      modelDirty = false;
      modelConflict = false;
      modelBaseVersion = state.state_version;
      return;
    }
    if (modelBaseVersion !== state.state_version) modelConflict = true;
  }

  function editModel(field, value) {
    if (!modelDraft) return;
    modelDraft = { ...modelDraft, [field]: value };
    modelDirty = true;
    modelConflict = modelBaseVersion !== snapshot?.state_version;
  }

  async function saveModel() {
    if (!snapshot?.learned_model || !modelDraft) return;
    if (modelConflict || modelBaseVersion !== snapshot.state_version) {
      modelConflict = true;
      message('warning', 'Сохранённая модель изменилась на другой панели. Черновик не отправлен; перечитайте снимок.');
      return;
    }
    try {
      const revision = modelRevisionPayload(snapshot.learned_model, modelDraft);
      await session.submit('MODEL_REVISION', { revision });
    } catch (error) {
      message('error', error.message || String(error));
    }
  }

  function receiveSession(next) {
    const previous = ui;
    if (next.auth !== 'authenticated' || (next.snapshot && previous.snapshot && next.snapshot.boot_id !== previous.snapshot.boot_id)) {
      settingsDraft = null; settingsDirty = []; settingsBaseVersion = null; settingsConflict = false;
      timerDirty = false; timerBaseVersion = null; timerConflict = false;
      modelDraft = null; modelDirty = false; modelBaseVersion = null; modelConflict = false;

    }
    const confirmedAction = previous?.pending?.action && !next.pending && next.message?.kind === 'ok'
      ? previous.pending.action
      : null;
    if (next.snapshot && next.snapshot !== previous?.snapshot) {
      reconcileSettings(next.snapshot, confirmedAction);
      reconcileTimer(next.snapshot, confirmedAction);
      reconcileModel(next.snapshot, confirmedAction);

    }
    ui = next;
    if (next.message) localMessage = null;
  }

  async function signIn() {
    if (!session || !password) {
      message('error', 'Введите пароль точки доступа.');
      return;
    }
    const input = password;
    password = '';
    await session.login(input);
  }

  async function saveSettings() {
    if (!snapshot?.settings || !settingsDraft) return;
    if (settingsConflict || settingsBaseVersion !== snapshot.state_version) {
      settingsConflict = true;
      message('warning', 'Настройки изменены на другой панели. Черновик не отправлен — перечитайте состояние.');
      return;
    }
    try {
      const settings = fullSettingsPayload(snapshot.settings, settingsDraft);
      await session.submit('SETTINGS', { settings });
    } catch (error) {
      message('error', error.message || String(error));
    }
  }

  async function start() {
    if (!snapshot || snapshot.state?.code !== 'IDLE') return;
    if (settingsDirty.length || settingsConflict) {
      message('warning', 'Сначала подтвердите или отмените изменённые настройки.');
      return;
    }
    await session.submit('START');
  }

  async function resetFault() {
    await session.submit('RESET_FAULT');
  }

  async function stop() {
    // STOP is intentionally not gated by link state or another pending command.
    await session.stop();
  }

  function updateTimer(value) {
    timerMinutes = value;
    timerDirty = true;
    timerConflict = timerBaseVersion !== snapshot?.state_version;
  }

  async function saveTimer() {
    if (!snapshot || timerMode === 'unavailable') return;
    if (timerConflict || (timerDirty && timerBaseVersion !== snapshot.state_version)) {
      timerConflict = true;
      message('warning', 'Таймер изменён на другой панели. Перечитайте состояние перед отправкой.');
      return;
    }
    try {
      const delay = millisecondsFromMinutes(timerMinutes);
      if (timerMode === 'active') {
        await session.submit('TIMER', { delay_ms: delay });
      } else {
        const settings = fullSettingsPayload(snapshot.settings, { duration_ms: delay });
        await session.submit('SETTINGS', { settings });
      }
    } catch (error) {
      message('error', error.message || String(error));
    }
  }

  async function startCalibration() {
    if (!commissioningAllowed(snapshot)) {
      message('warning', `Ввод в эксплуатацию недоступен: ${commissioningBlockLabel(snapshot)}.`);
      return;
    }
    await session.submit('CALIBRATE');
  }


  function chooseOta(event) {
    const file = event.currentTarget.files?.[0] || null;
    if (file && !file.name.toLowerCase().endsWith('.sota')) {
      otaFile = null;
      event.currentTarget.value = '';
      message('error', 'Нужен подписанный пакет .sota, а не исходный .bin.');
      return;
    }
    otaFile = file;
  }

  async function uploadOta() {
    if (!otaFile) {
      message('error', 'Выберите подписанный пакет .sota.');
      return;
    }
    await session.uploadOta(otaFile);
  }

  function commissioningLabel(state) {
    const commissioning = state?.commissioning;
    if (!commissioning) return 'Нет данных';
    const phase = commissioning.phase?.ru || commissioning.phase?.code || '—';
    const reason = commissioning.reason?.ru || commissioning.reason?.code || '—';
    return `${commissioning.active ? 'активна' : 'не активна'}; ${phase}; ${reason}`;
  }

  function commissioningBlockLabel(state) {
    const block = commissioningBlock(state);
    return `${block.ru}${block.code ? ` (${block.code})` : ''}`;
  }

  function otaLabel(ota) {
    if (ota?.phase === 'activation_queued') return 'Подпись проверена; активация поставлена в очередь. Ожидается новый boot_id/версия.';
    if (ota?.phase === 'running_expected_version') return `Новая загрузка: ${ota.observedVersion}. Версия совпала; самопроверка и откат проверяются отдельно.`;
    if (ota?.phase === 'different_version') return `После перезапуска версия ${ota.observedVersion}, ожидалась ${ota.expectedVersion || 'неизвестная'}. Обновление не подтверждено.`;
    if (ota?.phase === 'confirmation_timeout') return 'Нет подтверждения новой загрузки. Результат OTA неизвестен.';
    return 'Пакет ещё не загружался в этом сеансе.';
  }

  function toggleTheme() {
    dark = !dark;
    try { localStorage.setItem('sirokko-ui-theme', dark ? 'dark' : 'light'); } catch (_) {}
  }

  onMount(() => {
    try { dark = localStorage.getItem('sirokko-ui-theme') === 'dark'; } catch (_) {}
    session = new ControllerSession({ api: new ApiClient(), onChange: receiveSession });
    void session.restore();
    const ticker = setInterval(() => {
      session.tick();
      if (session.view.auth === 'authenticated') void session.poll();
    }, 1_000);
    const keepInputVisible = () => {
      const input = document.activeElement;
      if (input?.matches('input[type="number"], textarea')) input.scrollIntoView({ block: 'center', behavior: 'instant' });
    };
    document.addEventListener('focusin', keepInputVisible);
    window.visualViewport?.addEventListener('resize', keepInputVisible);
    return () => {
      clearInterval(ticker);
      document.removeEventListener('focusin', keepInputVisible);
      window.visualViewport?.removeEventListener('resize', keepInputVisible);
    };
  });
</script>

<svelte:head><meta name="theme-color" content={dark ? '#181b20' : '#ffffff'} /></svelte:head>

<div class="app-shell">
  <header class="app-header">
    <a href="#main" class="brand" aria-label="Sirokko, перейти к содержимому">Sirokko<span>262.01</span></a>
    <span class:danger={authenticated && !online} class="header-status" data-testid="connection-status">
      {authenticated ? (ui.requestInFlight === 'OTA' ? 'Загрузка OTA' : online ? 'Связь подтверждена' : 'Нет связи') : 'Вход'}
    </span>
  </header>

  <main id="main" tabindex="-1">
    {#if authenticated && snapshot && hasFault(snapshot)}
      <div class="fault-message" role="alert"><strong>Автомат сообщает аварию</strong><p>{online ? 'Текущий снимок' : 'Последний полученный снимок'} · Faults: {String(snapshot.faults)}. Запрет пуска: {blockLabel(snapshot)}</p><button class="btn text-button" disabled={!automaticReady} onclick={resetFault}>Сбросить аварию</button></div>
    {/if}
    {#if !authenticated}
      <section class="login-panel" aria-labelledby="login-title">
        <h1 id="login-title">Вход в точку доступа</h1>
        <p class="page-intro">Введите пароль AP. После входа интерфейс только читает фактический снимок автомата.</p>
        <label class="full-label">Пароль AP
          <input class="input" type="password" autocomplete="current-password" value={password} oninput={(event) => password = event.currentTarget.value} onkeydown={(event) => event.key === 'Enter' && signIn()} />
        </label>
        <button class="btn primary-button" disabled={ui.requestInFlight === 'LOGIN'} onclick={signIn}>{ui.requestInFlight === 'LOGIN' ? 'Проверяем…' : 'Войти'}</button>
      </section>
    {:else if !snapshot}
      <section class="connection-panel" aria-live="polite">
        <h1>Ожидание снимка состояния</h1>
        <p class="page-intro">Автоматические действия заблокированы до подтверждённого чтения. СТОП можно попытаться передать отдельной командой.</p>
        <button class="btn stop-button" disabled={ui.requestInFlight === 'STOP'} onclick={stop}><Icon name="stop" size={19} />Передать СТОП</button>
      </section>
    {:else if tab === 'home'}
      <div class="home-layout">
        <section class="thermostat" aria-label="Управление отоплением">
          <div class="section-heading"><h1>Отопление</h1><span class:danger={hasFault(snapshot)} class="device-status"><span class:running={active && online} class="status-mark"></span>{stateLabel(snapshot)}</span></div>
          {#if !online}<div class="inline-alert" role="status"><Icon name="offline" size={20} /><span>Связь потеряна; значения ниже — последний полученный снимок. Автоматические действия заблокированы.</span></div>{/if}
          <div class="mode-switch" role="group" aria-label="Режим управления">
            <button class:chosen={settingsValue('mode') === 'TEMPERATURE'} aria-pressed={settingsValue('mode') === 'TEMPERATURE'} disabled={!automaticReady || settingsConflict} onclick={() => chooseMode('TEMPERATURE')}>По температуре</button>
            <button class:chosen={settingsValue('mode') === 'MANUAL'} aria-pressed={settingsValue('mode') === 'MANUAL'} disabled={!automaticReady || settingsConflict} onclick={() => chooseMode('MANUAL')}>Ручной</button>
          </div>
          <div class="setpoint-block">
            <p class="value-label">{settingsValue('mode') === 'MANUAL' ? 'Заданный уровень' : 'Задано'}</p>
            <div class="setpoint-control">
              <button class="btn step-button" aria-label={settingsValue('mode') === 'MANUAL' ? 'Уменьшить уровень' : 'Уменьшить температуру'} disabled={!automaticReady || settingsConflict || (settingsValue('mode') === 'MANUAL' ? settingsValue('manual_percent') <= 5 : settingsValue('room_setpoint_milli_c') <= 15000)} onclick={() => settingsValue('mode') === 'MANUAL' ? draftValue('manual_percent', Number(settingsValue('manual_percent')) - 5) : draftValue('room_setpoint_milli_c', Number(settingsValue('room_setpoint_milli_c')) - 1000)}><Icon name="minus" /></button>
              <div class="setpoint-number" data-testid="setpoint"><span>{settingsValue('mode') === 'MANUAL' ? settingsValue('manual_percent') : Number(settingsValue('room_setpoint_milli_c')) / 1000}</span><span class="unit">{settingsValue('mode') === 'MANUAL' ? '%' : '°C'}</span></div>
              <button class="btn step-button" aria-label={settingsValue('mode') === 'MANUAL' ? 'Увеличить уровень' : 'Увеличить температуру'} disabled={!automaticReady || settingsConflict || (settingsValue('mode') === 'MANUAL' ? settingsValue('manual_percent') >= 100 : settingsValue('room_setpoint_milli_c') >= 30000)} onclick={() => settingsValue('mode') === 'MANUAL' ? draftValue('manual_percent', Number(settingsValue('manual_percent')) + 5) : draftValue('room_setpoint_milli_c', Number(settingsValue('room_setpoint_milli_c')) + 1000)}><Icon name="plus" /></button>
            </div>
            <div class="current-reading"><span>Сейчас в помещении</span><strong>{sensorText(snapshot.sensors?.room)}</strong></div>
          </div>
          {#if snapshot.room_excess}<p class="field-description" role="status">Температура выше уставки; автомат не гасит горение по комнате.</p>{/if}
          <div class="apply-area" aria-live="polite">
            {#if settingsConflict}
              <div class="draft-line"><span>Настройки изменены внешне</span><button class="btn text-button" onclick={restoreSettings}>Перечитать</button></div>
            {:else if settingsDirty.length}
              <div class="draft-line"><span>Не применено</span><button class="btn text-button" onclick={restoreSettings}>Отменить</button></div>
              <button class="btn primary-button" disabled={!automaticReady} onclick={saveSettings}>{ui.requestInFlight === 'SETTINGS' ? 'Отправляем…' : 'Применить'}</button>
            {:else}
              <p class="mode-explanation">{settingsValue('mode') === 'MANUAL' ? 'Постоянный уровень нагрева' : 'Поддержание заданной температуры'}</p>
            {/if}
          </div>
        </section>
        <aside class="session-panel" aria-label="Текущий сеанс">
          <button class="timer-summary" onclick={() => navigate('timers')}>
            <Icon name="clock" size={22} /><span><span class="summary-label">{'Таймер отключения'}</span><strong>{active ? (snapshot.timer?.active ? formatDuration(snapshot.timer.remaining_ms) : 'Не задан') : (snapshot.settings?.duration_ms ? formatDuration(snapshot.settings.duration_ms) : 'Не задан')}</strong></span><Icon name="right" size={18} />
          </button>
          <div class="run-action">
            {#if active || snapshot.state?.code === 'STOP_PURGE' || snapshot.state?.code === 'FAULT_PURGE'}
              <button class="btn stop-button" disabled={ui.requestInFlight === 'STOP'} onclick={stop}><Icon name="stop" size={19} />Остановить нагрев</button><p>СТОП передаётся автомату; дальнейшая продувка показывается только по снимку.</p>
            {:else if snapshot.state?.code === 'IDLE'}
              <button class="btn primary-button" disabled={!automaticReady || settingsDirty.length || settingsConflict} onclick={start}><Icon name="power" size={20} />{ui.requestInFlight === 'START' ? 'Отправляем…' : 'Запустить отопление'}</button><p>{settingsConflict ? 'Сначала перечитайте настройки' : settingsDirty.length ? 'Сначала примените настройки' : 'Готовность подтверждает автомат'}</p>
              <button class="btn stop-button stop-secondary" disabled={ui.requestInFlight === 'STOP'} onclick={stop}><Icon name="stop" size={17} />Передать СТОП</button>
            {:else}
              <p class="mode-explanation">Пуск и настройка недоступны в текущей фактической фазе.</p>
            {/if}
          </div>
          <button class="btn diagnostics-link" onclick={() => openDetail('diagnostics')}>Показания и состояние<Icon name="right" size={17} /></button>
        </aside>
      </div>
    {:else if tab === 'timers'}
      <div class="section-heading"><h1>Таймер</h1></div>
      {#if timerMode === 'active'}
        <p class="page-intro">Таймер останова устанавливается от текущего момента. Ноль отменяет действующий таймер.</p>
      {:else if timerMode === 'next'}
        <p class="page-intro">Таймер начнёт отсчёт после ручного пуска. Ноль отключает таймер.</p>
      {:else}
        <p class="page-intro">Таймер доступен только в IDLE или на штатном запуске/работе.</p>
      {/if}
      <section class="timer-section" aria-labelledby="timer-title">
        <h2 id="timer-title">Отключить через</h2>
        {#if timerMode === 'active' && snapshot.timer?.active}<p class="timer-active">Осталось <strong>{formatDuration(snapshot.timer.remaining_ms)}</strong></p>{/if}
        <div class="duration-inputs">
          <label>Минуты<input class="input duration-input" type="number" inputmode="numeric" min="0" max="10080" step="1" value={timerMinutes} disabled={!automaticReady || timerMode === 'unavailable'} oninput={(event) => updateTimer(event.currentTarget.value)} aria-label="Минуты таймера" /></label>
        </div>
        {#if timerConflict}<p class="field-description after-button">Таймер изменён внешне. Перечитайте снимок перед отправкой.</p><button class="btn text-button" onclick={() => reconcileTimer(snapshot, 'TIMER')}>Перечитать таймер</button>{/if}
        <button class="btn primary-button" disabled={!automaticReady || timerMode === 'unavailable' || timerConflict} onclick={saveTimer}>{timerMode === 'active' ? (Number(timerMinutes) === 0 ? 'Отменить таймер' : 'Установить таймер') : 'Сохранить таймер'}</button>
      </section>
    {:else}
      {#if detail}<button class="btn back-button" onclick={() => detail = ''}><Icon name="back" size={20} />Настройки</button>{/if}
      {#if !detail}
        <div class="section-heading"><h1>Настройки</h1></div>
        <div class="settings-list">
          {#each [['wifi','Wi-Fi и пароль','Открытая сеть или защита паролем'],['diagnostics','Показания и журнал','Температуры, состояние, события'],['calibration','Ввод в эксплуатацию','Фактическая фаза и запуск автокалибровки'],['advanced','Плавность регулирования','Нарастание и снижение команды'],['update','Обновление','Подписанный пакет .sota']] as [id, title, subtitle]}
            <button class="settings-row" onclick={() => openDetail(id)}><span><strong>{title}</strong><span>{subtitle}</span></span><Icon name="right" size={18} /></button>
          {/each}
          <button class="settings-row theme-row" onclick={toggleTheme}><span><strong>Оформление</strong><span>{dark ? 'Тёмное' : 'Светлое'}</span></span><Icon name="sun" size={22} /></button>
          <button class="settings-row" onclick={() => openDetail('about')}><span><strong>О контроллере</strong><span>Версия, идентификаторы и соединение</span></span><Icon name="right" size={18} /></button>
        </div>
      {:else if detail === 'wifi'}
        <h1>Wi-Fi и пароль</h1>
        <p class="page-intro">{snapshot.wifi?.password_required ? 'Сеть и веб-интерфейс защищены паролем.' : 'Сеть открыта, вход в веб-интерфейс без пароля. Любой рядом может подключиться.'}</p>
        {#if snapshot.wifi?.restart_required}<p class="field-description">После следующего включения: {snapshot.wifi.next_boot_password_required ? 'защита новым паролем' : 'без пароля'}.</p>{/if}
        <label class="full-label">Новый пароль<input class="input" type="password" autocomplete="new-password" minlength="8" maxlength="63" bind:value={wifiPassword} disabled={wifiSaving} /></label>
        <label class="full-label">Повторите пароль<input class="input" type="password" autocomplete="new-password" minlength="8" maxlength="63" bind:value={wifiConfirmation} disabled={wifiSaving} /></label>
        <p class="field-description">8–63 латинских букв, цифр или символов без пробелов. Этот пароль используется для Wi-Fi и входа в браузере.</p>
        <button class="btn primary-button" disabled={!automaticReady || wifiSaving || !wifiPassword} onclick={() => saveWifi(false)}>{wifiSaving ? 'Сохраняем…' : 'Задать пароль'}</button>
        <button class="btn text-button" disabled={!automaticReady || wifiSaving || !snapshot.wifi?.next_boot_password_required} onclick={() => saveWifi(true)}>Убрать пароль</button>
        <p class="field-description">Изменение вступит в силу после следующего включения питания. Если отопитель работает, сначала остановите его и дождитесь окончания продувки.</p>
      {:else if detail === 'diagnostics'}
        <h1>Показания и журнал</h1>
        <dl class="readings">
          <div><dt>Запрет пуска</dt><dd>{blockLabel(snapshot)}</dd></div>
          <div><dt>Хранилище</dt><dd>{snapshot.storage_ok ? 'Доступно' : 'Ошибка хранения'}</dd></div>
          <div><dt>Выходные команды</dt><dd>FAN {String(snapshot.outputs?.fan_request)} · GLOW {String(snapshot.outputs?.glow)} · FUEL {String(snapshot.outputs?.fuel)}</dd></div>
          <div><dt>Состояние</dt><dd>{stateLabel(snapshot)}</dd></div>
          <div><dt>Помещение</dt><dd>{sensorText(snapshot.sensors?.room)}</dd></div>
          <div><dt>Воздух на выходе</dt><dd>{sensorText(snapshot.sensors?.air)}</dd></div>
          <div><dt>Выхлоп</dt><dd>{sensorText(snapshot.sensors?.exhaust)}</dd></div>
          <div><dt>Команда вентилятора</dt><dd>{Number(snapshot.outputs?.command_percent ?? 0).toFixed(1)} % · PWM {snapshot.outputs?.speed_code ?? '—'}</dd></div>
          <div><dt>Режим</dt><dd>{snapshot.settings?.mode === 'MANUAL' ? 'Ручной' : 'По температуре'}</dd></div>
          <div><dt>Сохранённая модель</dt><dd>{snapshot.learned_model?.valid ? `PWM ${snapshot.learned_model.minimum_speed_code}…${snapshot.learned_model.maximum_speed_code}; rev. ${snapshot.learned_model.provenance?.revision ?? '—'}` : 'нет подтверждённой модели'}</dd></div>
          <div><dt>Продувка</dt><dd>{snapshot.purge?.active ? `активна; минимум ${formatDuration(snapshot.purge.minimum_remaining_ms)}` : 'не активна'}</dd></div>
        </dl>
        <h2 class="subheading">Последние события автомата</h2>
        <ol class="event-list">{#each snapshot.events || [] as event}<li><time>{event.at_ms} ms</time><span>{event.kind_ru || event.kind || '—'} · {event.state_ru || event.state || '—'} · PWM {event.speed_code} · EXH {Number(event.exhaust_milli_c / 1000).toFixed(1)} °C / AIR {Number(event.air_milli_c / 1000).toFixed(1)} °C</span></li>{/each}</ol>
      {:else if detail === 'calibration'}
        <h1>Ввод в эксплуатацию</h1><p class="page-intro">Интерфейс показывает только фактическую фазу автомата; процент или завершение не моделируются.</p>
        <div class="process-state"><strong>{commissioningLabel(snapshot)}</strong><span>Точек: {snapshot.commissioning?.accepted_point_count ?? '—'}</span></div>
        <dl class="readings"><div><dt>Фаза</dt><dd>{snapshot.commissioning?.phase?.ru || snapshot.commissioning?.phase?.code || '—'}</dd></div><div><dt>Причина/отмена</dt><dd>{snapshot.commissioning?.reason?.ru || snapshot.commissioning?.reason?.code || '—'}</dd></div><div><dt>Время</dt><dd>{formatDuration(snapshot.commissioning?.elapsed_ms)}</dd></div></dl>
        <button class="btn primary-button" disabled={!automaticReady || snapshot.commissioning?.active || !commissioningAllowed(snapshot)} onclick={startCalibration}>{snapshot.commissioning?.active ? 'Ввод в эксплуатацию активен' : commissioningAllowed(snapshot) ? 'Начать ввод в эксплуатацию' : 'Ввод в эксплуатацию недоступен'}</button>
        <p class="field-description after-button" data-testid="commissioning-availability">{snapshot.commissioning?.active || commissioningAllowed(snapshot) ? 'Для отмены используется СТОП; итог и причина появятся только в следующем снимке автомата.' : `Ввод в эксплуатацию недоступен: ${commissioningBlockLabel(snapshot)}.`}</p>
        <section class="timer-section" aria-labelledby="model-revision-title">
          <h2 id="model-revision-title">Ручная коррекция сохранённой модели</h2>
          {#if snapshot.learned_model?.valid && modelDraft}
            <p class="field-description">Ревизия {snapshot.learned_model.provenance?.revision ?? '—'}. Можно только сузить диапазон PWM и замедлить скорости; исходную модель эта форма не создаёт.</p>
            <div class="duration-inputs">
              <label>Минимум PWM<input class="input" type="number" min={snapshot.learned_model.minimum_speed_code} max={snapshot.learned_model.maximum_speed_code} step="1" inputmode="numeric" value={modelDraft.minimum_speed_code} disabled={!automaticReady || modelConflict} oninput={(event) => editModel('minimum_speed_code', Number(event.currentTarget.value))} /></label>
              <label>Максимум PWM<input class="input" type="number" min={snapshot.learned_model.minimum_speed_code} max={snapshot.learned_model.maximum_speed_code} step="1" inputmode="numeric" value={modelDraft.maximum_speed_code} disabled={!automaticReady || modelConflict} oninput={(event) => editModel('maximum_speed_code', Number(event.currentTarget.value))} /></label>
              <label>Нарастание, код/с<input class="input" type="number" min="0" max={snapshot.learned_model.slew_up_per_s} step="any" inputmode="decimal" value={modelDraft.slew_up_per_s} disabled={!automaticReady || modelConflict} oninput={(event) => editModel('slew_up_per_s', Number(event.currentTarget.value))} /></label>
              <label>Снижение, код/с<input class="input" type="number" min="0" max={snapshot.learned_model.slew_down_per_s} step="any" inputmode="decimal" value={modelDraft.slew_down_per_s} disabled={!automaticReady || modelConflict} oninput={(event) => editModel('slew_down_per_s', Number(event.currentTarget.value))} /></label>
            </div>
            {#if modelConflict}<p class="field-description" data-testid="model-conflict">Сохранённая модель изменена внешне. Перечитайте снимок.</p>{/if}
            <button class="btn primary-button" disabled={!automaticReady || modelConflict || !modelDirty} onclick={saveModel}>Сохранить коррекцию модели</button>
            <button class="btn text-button full-width" onclick={restoreModel}>Вернуть снимок модели</button>
          {:else}
            <p class="field-description" data-testid="model-unavailable">Ручная коррекция недоступна: сохранённая LearnedOperatingModel ещё не подтверждена.</p>
          {/if}
        </section>
      {:else if detail === 'advanced'}
        <h1>Плавность регулирования</h1><p class="page-intro">Изменение команды за секунду, в процентах рабочего диапазона.</p>
        <label class="full-label">Нарастание, %/с<input class="input" type="number" min="0.01" max="0.5" step="0.01" inputmode="decimal" value={Number(settingsValue('slew_up_per_s')) * 100} disabled={!automaticReady || settingsConflict} oninput={(event) => draftValue('slew_up_per_s', Number(event.currentTarget.value) / 100)} /></label>
        <label class="full-label">Снижение, %/с<input class="input" type="number" min="0.01" max="0.5" step="0.01" inputmode="decimal" value={Number(settingsValue('slew_down_per_s')) * 100} disabled={!automaticReady || settingsConflict} oninput={(event) => draftValue('slew_down_per_s', Number(event.currentTarget.value) / 100)} /></label>
        {#if settingsConflict}<p class="field-description">Настройки изменены внешне. Перечитайте снимок.</p>{/if}
        <button class="btn primary-button" disabled={!automaticReady || settingsConflict || !settingsDirty.length} onclick={saveSettings}>Сохранить плавность</button>
        <button class="btn text-button full-width" onclick={restoreSettings}>Вернуть снимок автомата</button>

      {:else if detail === 'update'}
        <h1>Обновление</h1><p class="page-intro">Принимается один подписанный .sota. Файл полностью хешируется в браузере, затем проверяется автоматом перед постановкой активации в очередь.</p>
        <label class="file-label">Пакет прошивки<input class="file-input" type="file" accept=".sota,application/octet-stream" onchange={chooseOta} disabled={!automaticReady || ui.requestInFlight === 'OTA'} /></label>
        {#if otaFile}<p class="field-description">Выбран: {otaFile.name} · {otaFile.size} байт</p>{/if}
        <button class="btn primary-button" disabled={!automaticReady || !otaFile || ui.requestInFlight === 'OTA'} onclick={uploadOta}>{ui.requestInFlight === 'OTA' ? 'Проверяем и загружаем…' : 'Проверить и загрузить OTA'}</button>
        <p class="field-description after-button" data-testid="ota-status">{otaLabel(ui.ota)}</p>
      {:else if detail === 'about'}
        <h1>О контроллере</h1>
        <dl class="readings"><div><dt>Версия</dt><dd>{snapshot.version || '—'}</dd></div><div><dt>boot_id</dt><dd>{snapshot.boot_id || '—'}</dd></div><div><dt>run_id</dt><dd>{snapshot.run_id || '—'}</dd></div><div><dt>Состояние связи</dt><dd>{online ? 'снимок подтверждён' : 'нет свежего снимка'}</dd></div></dl>
      {/if}
    {/if}
  </main>

  {#if authenticated && snapshot && (tab !== 'home' || !['IDLE', 'PREPURGE', 'PREHEAT', 'IGNITION', 'VERIFY', 'RUN', 'STOP_PURGE', 'FAULT_PURGE'].includes(snapshot.state?.code))}
    <div class="persistent-stop" bind:this={stopPanel}><span>{stateLabel(snapshot)}</span><button class="btn stop-button" disabled={ui.requestInFlight === 'STOP'} onclick={stop}><Icon name="stop" size={17} />Остановить</button></div>
  {/if}

  {#if feedback}<div bind:this={feedbackPanel} class="feedback visible" role="status" aria-live="polite" class:error={feedback.kind === 'error'} class:warning={feedback.kind === 'warning'}><Icon name={feedback.kind === 'error' || feedback.kind === 'warning' ? 'warning' : feedback.kind === 'pending' ? 'clock' : 'check'} size={19} /><span>{feedback.text}</span></div>{/if}

  <nav class="bottom-nav" bind:this={bottomNav} aria-label="Основная навигация">
    {#each [['home','Главная','home'],['timers','Таймер','clock'],['settings','Настройки','settings']] as [id, label, icon]}
      <button class:current={tab === id} aria-current={tab === id ? 'page' : undefined} disabled={!authenticated} onclick={() => navigate(id)}><Icon name={icon} size={23} /><span>{label}</span></button>
    {/each}
  </nav>
</div>
