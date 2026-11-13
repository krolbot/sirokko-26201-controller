<script>
  import { onMount } from "svelte";
  import Icon from "./Icon.svelte";
  import { ApiClient } from "./lib/api.js";
  import { ControllerSession } from "./lib/controller.js";
  import { Draft } from "./lib/draft.svelte.js";
  import {
    routes,
    routeFromHash,
    needsStop,
    connectionReady,
    commandAllowed,
    stateText,
  } from "./lib/presentation.js";
  import { isActiveSession } from "./lib/protocol.js";
  import Timer from "./pages/Timer.svelte";
  import Control from "./pages/Control.svelte";
  import Diagnostics from "./pages/Diagnostics.svelte";
  import Settings from "./pages/Settings.svelte";
  import Wifi from "./pages/Wifi.svelte";
  import Update from "./pages/Update.svelte";
  import Regulation from "./pages/Regulation.svelte";
  import Calibration from "./pages/Calibration.svelte";
  import Heater from "./pages/Heater.svelte";
  import Debug from "./pages/Debug.svelte";
  import { heaterDraft } from "./lib/heater.js";
  import { Measurements } from "./lib/measurements.js";

  let route = $state(routeFromHash(window.location.hash));
  const colorScheme = window.matchMedia("(prefers-color-scheme: dark)");
  let systemDark = $state(colorScheme.matches);
  let theme = $state(savedTheme());
  const dark = $derived(theme === "system" ? systemDark : theme === "dark");
  let password = $state("");
  let ui = $state({
    auth: "checking",
    link: "connecting",
    snapshot: null,
    pending: null,
    requestInFlight: null,
    message: null,
    ota: { phase: "idle" },
  });
  let feedback = $state(null);
  let feedbackTimer;
  let lastMessage;
  const settings = new Draft([
    "room_setpoint_milli_c",
    "mode",
    "manual_percent",
  ]);
  const regulation = new Draft(["slew_up_per_s", "slew_down_per_s"]);
  const timer = new Draft();
  const model = new Draft();
  const heater = new Draft();
  let timerRun = null;
  let session = $state.raw(null);
  const snapshot = $derived(ui.snapshot);
  const measurements = new Measurements();
  let recording = $state(measurements.status());

  function startRecording() {
    measurements.start(snapshot, ui.link === "online");
    recording = measurements.status();
  }
  function stopRecording() {
    measurements.stop();
    recording = measurements.status();
  }
  function addMeasurementNote(text) {
    const added = measurements.note(text);
    recording = measurements.status();
    return added;
  }
  function downloadMeasurements() {
    const url = URL.createObjectURL(
      new Blob([measurements.csv()], { type: "text/csv;charset=utf-8" }),
    );
    const link = document.createElement("a");
    link.href = url;
    link.download = `sirokko-${new Date().toISOString().replaceAll(":", "-")}.csv`;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    measurements.markExported();
    recording = measurements.status();
  }

  const stopVisible = $derived(
    needsStop(snapshot) ||
      ["START", "CALIBRATE", "DEBUG_ENTER"].includes(ui.pending?.action),
  );
  const authenticated = $derived(ui.auth === "authenticated");
  function notify(kind, text) {
    clearTimeout(feedbackTimer);
    feedback = kind === "pending" ? null : { kind, text };
    if (kind === "ok")
      feedbackTimer = setTimeout(() => {
        feedback = null;
      }, 3000);
  }
  function receive(next) {
    const current = next.snapshot;
    measurements.observe(current, next.link === "online");
    recording = measurements.status();
    if (next.auth === "anonymous") {
      settings.clear();
      regulation.clear();
      timer.clear();
      model.clear();
    }
    if (current) {
      settings.sync(current, current.settings);
      regulation.sync(current, current.settings);
      const active = isActiveSession(current);
      const run = `${current.boot_id}:${active ? current.run_id : "next"}`;
      const timerValue = {
        deadline_ms: active ? current.timer.deadline_ms : 0,
        minutes: Math.ceil(
          (active ? current.timer.remaining_ms : current.settings.duration_ms) /
            60000,
        ),
      };
      const confirmed =
        ui.pending && !next.pending && next.message?.kind === "ok"
          ? ui.pending.action
          : null;
      if (
        confirmed === "SETTINGS" &&
        settings.fields.some(
          (key) =>
            ui.pending.payload.settings[key] !==
            ui.pending.baseline.settings[key],
        )
      )
        settings.reset(current, current.settings);
      if (
        timerRun !== run ||
        confirmed === "TIMER" ||
        (!active &&
          confirmed === "SETTINGS" &&
          ui.pending?.payload.settings?.duration_ms ===
            timer.value?.minutes * 60000)
      )
        timer.reset(current, timerValue);
      else if (active && timer.dirty)
        timer.conflict =
          current.timer.deadline_ms !== timer.baseline.deadline_ms;
      else timer.sync(current, timerValue);
      timerRun = run;
      heater.sync(current, heaterDraft(current.envelope));
      if (current.learned_model?.valid) {
        const saved = current.learned_model;
        model.sync(current, {
          minimum_speed_code: saved.minimum_speed_code,
          maximum_speed_code: saved.maximum_speed_code,
          slew_up_per_s: saved.slew_up_per_s,
          slew_down_per_s: saved.slew_down_per_s,
        });
      } else model.clear();
    }
    if (next.message !== lastMessage) {
      lastMessage = next.message;
      if (next.message) notify(next.message.kind, next.message.text);
      else {
        clearTimeout(feedbackTimer);
        feedback = null;
      }
    }
    ui = next;
  }
  function savedTheme() {
    try {
      const saved = localStorage.getItem("sirokko-ui-theme");
      if (["light", "dark"].includes(saved)) return saved;
    } catch {}
    return "system";
  }
  function selectTheme(value) {
    theme = value;
    try {
      localStorage.setItem("sirokko-ui-theme", value);
    } catch {}
  }
  $effect(() => {
    document.documentElement.dataset.theme = dark ? "dark" : "light";
    document.documentElement.style.colorScheme = dark ? "dark" : "light";
  });
  async function signIn(event) {
    event.preventDefault();
    const entered = password;
    password = "";
    await session.login(entered);
  }
  onMount(() => {
    const systemThemeChanged = (event) => {
      systemDark = event.matches;
    };
    colorScheme.addEventListener("change", systemThemeChanged);
    session = new ControllerSession({
      api: new ApiClient(),
      onChange: receive,
    });
    void session.restore();
    const poll = setInterval(() => {
      session.tick();
      if (session.view.auth === "authenticated") void session.poll();
    }, 1000);
    const locationChanged = () => {
      route = routeFromHash(location.hash);
      feedback = null;
      window.scrollTo({ top: 0, behavior: "instant" });
    };
    window.addEventListener("hashchange", locationChanged);
    const beforeUnload = (event) => {
      if (measurements.active || measurements.unsaved) {
        event.preventDefault();
        event.returnValue = "";
      }
    };
    window.addEventListener("beforeunload", beforeUnload);
    return () => {
      colorScheme.removeEventListener("change", systemThemeChanged);
      clearInterval(poll);
      clearTimeout(feedbackTimer);
      window.removeEventListener("hashchange", locationChanged);
      window.removeEventListener("beforeunload", beforeUnload);
    };
  });
</script>

<svelte:head
  ><title>{routes[route].title} · Sirokko</title><meta
    name="theme-color"
    content={dark ? "#171719" : "#f5f5f7"}
  /></svelte:head
>
<div class="app-shell" class:has-stop={stopVisible}>
  <header class="app-header">
    <a class="brand" href="#/control">Sirokko <span>262.01</span></a><span
      class="connection"
      class:offline={authenticated && ui.link === "offline"}
      aria-live="polite"
      >{ui.auth === "anonymous"
        ? "Вход"
        : ui.link === "online"
          ? "На связи"
          : ui.link === "connecting"
            ? "Подключение…"
            : "Нет связи"}</span
    >
  </header>
  <main id="main">
    {#if feedback}<div
        class="feedback toast"
        class:error={feedback.kind === "error"}
        role="status"
      >
        <span>{feedback.text}</span><button
          class="dismiss"
          aria-label="Закрыть сообщение"
          onclick={() => {
            feedback = null;
            session.dismissMessage();
          }}>×</button
        >
      </div>{/if}
    {#if ui.auth === "anonymous"}
      <form class="login form-column" onsubmit={signIn}>
        <h1>Вход в пульт</h1>
        <label
          >Пароль Wi-Fi<input
            type="password"
            autocomplete="current-password"
            bind:value={password}
            required
          /></label
        ><button
          class="primary"
          type="submit"
          disabled={ui.requestInFlight === "LOGIN"}
          >{ui.requestInFlight === "LOGIN" ? "Проверка…" : "Войти"}</button
        >
      </form>
    {:else if !snapshot}
      <section class="connection-panel">
        <h1>
          {ui.link === "offline"
            ? "Нет связи с контроллером"
            : "Подключение к контроллеру"}
        </h1>
        <p class="intro">
          {ui.link === "offline"
            ? "Проверьте подключение к сети Sirokko"
            : "Получаем состояние устройства"}
        </p>
        {#if ui.link === "offline"}<button
            class="secondary"
            onclick={() => session.restore()}>Повторить</button
          >{/if}
      </section>
    {:else}
      {#if ui.link === "offline"}<div class="notice" role="status">
          Нет связи. Показаны последние полученные данные
        </div>{/if}
      {#if snapshot.faults}<div class="fault-notice" role="alert">
          <strong>{snapshot.fault_messages?.[0] || "Ошибка отопителя"}</strong
          ><a href="#/diagnostics">Подробнее</a
          >{#if commandAllowed(snapshot, "RESET_FAULT")}<button
              class="secondary"
              disabled={!connectionReady(ui)}
              onclick={() => session.submit("RESET_FAULT")}
              >Сбросить ошибку</button
            >{/if}
        </div>{/if}
      {#if routes[route].section === "settings" && route !== "settings"}<a
          class="back-link"
          href="#/settings">← Настройки</a
        >{/if}
      {#if recording.active && route !== "diagnostics"}<p class="caption">
          <a href="#/diagnostics"
            >Запись измерений · {recording.online
              ? "Идёт запись"
              : "Ожидаем связь"}</a
          >
        </p>{/if}
      {#if route === "control"}<Control {ui} {session} {settings} {notify} />
      {:else if route === "timer"}<Timer {ui} {session} {timer} {notify} />
      {:else if route === "diagnostics"}<Diagnostics
          {snapshot}
          api={session.api}
          online={ui.link === "online"}
          {recording}
          {startRecording}
          {stopRecording}
          {downloadMeasurements}
          {addMeasurementNote}
        />
      {:else if route === "settings"}<Settings
          {snapshot}
          {theme}
          {selectTheme}
        />
      {:else if route === "wifi"}<Wifi {ui} {session} {notify} />
      {:else if route === "debug"}<Debug
          {ui}
          {session}
          {notify}
          {heater}
          {recording}
          {startRecording}
          {stopRecording}
          {downloadMeasurements}
          {addMeasurementNote}
        />
      {:else if route === "heater"}<Heater
          {ui}
          {session}
          draft={heater}
          {notify}
        />
      {:else if route === "update"}<Update {ui} {session} {notify} />
      {:else if route === "regulation"}<Regulation
          {ui}
          {session}
          draft={regulation}
          {notify}
        />
      {:else if route === "calibration"}<Calibration
          {ui}
          {session}
          draft={model}
          {notify}
        />{/if}
    {/if}
  </main>
  {#if snapshot && authenticated}<footer class="bottom-dock">
      {#if stopVisible}<div class="stop-strip">
          <span>{stateText(snapshot)}</span><button
            class="stop-button"
            disabled={ui.requestInFlight === "STOP"}
            onclick={() => session.stop()}
            >{snapshot.state.code === "DEBUG" ||
            ui.pending?.action === "DEBUG_ENTER"
              ? "Завершить отладку"
              : "Остановить нагрев"}</button
          >
        </div>{/if}
      <nav aria-label="Основная навигация">
        {#each [["control", "Отопление", "heat"], ["timer", "Таймер", "clock"], ["diagnostics", "Состояние", "thermometer"], ["settings", "Настройки", "settings"]] as [id, label, icon]}<a
            href={"#/" + id}
            aria-current={routes[route].section === id ? "page" : undefined}
            ><Icon name={icon} size={20} /><span>{label}</span></a
          >{/each}
      </nav>
    </footer>{/if}
</div>
