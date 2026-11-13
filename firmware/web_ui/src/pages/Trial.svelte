<script>
  import History from "../components/History.svelte";
  import Measurements from "../components/Measurements.svelte";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
    stateText,
    temperature,
    countdownText,
  } from "../lib/presentation.js";
  import { trialActive } from "../lib/protocol.js";
  let {
    ui,
    api,
    recording,
    startTrial,
    downloadMeasurements,
    addMeasurementNote,
    stopRecording,
  } = $props();
  const snapshot = $derived(ui.snapshot);
  const active = $derived(trialActive(snapshot));
  const pending = $derived(
    ui.pending?.action === "TRIAL_START" ||
      ui.requestInFlight === "TRIAL_START",
  );
  const ready = $derived(connectionReady(ui));
  const captureAvailable = $derived(!recording.active && !recording.unsaved);
  const phase = $derived(snapshot.state.code);
  const cooling = $derived(snapshot.purge?.active);
  const time = $derived(
    cooling
      ? snapshot.purge.minimum_remaining_ms
      : snapshot.trial?.remaining_ms,
  );
  const timerLabel = $derived(
    cooling
      ? "Минимальное охлаждение"
      : phase === "TRIAL_READY"
        ? "На начало запуска"
        : phase === "PREHEAT"
          ? "До подачи топлива"
          : "До отключения топлива",
  );
</script>

<div class="page-heading"><h1>Первый запуск</h1></div>
<p class="intro">
  Запуск и запись температур без заполненного профиля отопителя.
</p>
{#if active}
  <section class="surface info-panel section trial-process" aria-live="polite">
    <h2>{phase === "TRIAL_READY" ? "Готов к началу" : stateText(snapshot)}</h2>
    {#if phase === "TRIAL_READY"}
      <p>
        Нажмите и удерживайте KEY0 на панели. Отпускание кнопки остановит
        запуск.
      </p>
    {:else if cooling}
      <p>Свеча и топливо выключены. Двигатель охлаждает отопитель.</p>
    {:else}
      <p>
        Удерживайте KEY0. Отпустите кнопку, чтобы отключить свечу и топливо.
      </p>
      {#if phase === "IGNITION"}
        <p class="caption">
          Если розжига нет, отпустите KEY0; не ждите окончания отсчёта.
        </p>
      {/if}
    {/if}
    {#if time > 0}<div class="trial-countdown">
        <span>{timerLabel}</span><strong>{countdownText(time)}</strong>
      </div>
    {:else if cooling}<p class="caption">
        Ждём охлаждения выхлопа ниже {snapshot.envelope.parameters
          .cooling_exhaust_milli_c / 1000} °C.
      </p>{/if}
  </section>
{:else}
  <section class="surface info-panel section">
    <h2>Запуск с записью температур</h2>
    <p class="caption">
      После 30 с накала включатся двигатель и подача топлива. Ещё через 10 с
      свеча выключится. Через 90 с после подачи топлива начнётся охлаждение.
      Двигатель работает на максимальном ШИМ.
    </p>
    <p class="notice">
      Перед запуском восстановите штатное отключение муфты при перегреве.
    </p>
    <div class="trial-actions">
      <button
        class="primary"
        disabled={!ready ||
          !captureAvailable ||
          !commandAllowed(snapshot, "TRIAL_START")}
        onclick={startTrial}>Подготовить запуск</button
      >
      {#if !commandAllowed(snapshot, "TRIAL_START")}<p class="caption">
          {commandReason(snapshot, "TRIAL_START")}
        </p>{/if}
    </div>
    {#if recording.active}<p class="caption">
        Перед новым запуском завершите текущую запись и скачайте CSV.
      </p>
    {:else if recording.unsaved}<p class="caption">
        Скачайте CSV предыдущей записи перед новым запуском.
      </p>{/if}
    {#if pending}<p class="caption" role="status">
        Ждём подтверждения контроллера…
      </p>{/if}
  </section>
{/if}

<div class="readings">
  {#each [["exhaust", "Выхлоп"], ["air", "Нагретый воздух"]] as [channel, label]}
    <section class="reading surface">
      <h2>{label}</h2>
      <strong>{temperature(snapshot.sensors[channel])}</strong>
    </section>
  {/each}
</div>
<section class="surface info-panel section">
  <dl class="reading-list">
    <div>
      <dt>Свеча</dt>
      <dd>{snapshot.outputs.glow ? "Включена" : "Выключена"}</dd>
    </div>
    <div>
      <dt>Подача топлива</dt>
      <dd>{snapshot.outputs.fuel ? "Включена" : "Выключена"}</dd>
    </div>
    <div>
      <dt>Двигатель</dt>
      <dd>
        {snapshot.outputs.fan_request
          ? `Включён · ШИМ ${snapshot.outputs.speed_code}`
          : "Выключен"}
      </dd>
    </div>
  </dl>
</section>
<History {snapshot} {api} online={ui.link === "online"} />
{#if recording.samples}
  <Measurements
    {recording}
    online={ui.link === "online"}
    stop={stopRecording}
    download={downloadMeasurements}
    addNote={addMeasurementNote}
    allowStart={false}
    stopDisabled={active || pending}
  />
{:else}<p class="caption">
    Запись начнётся при подготовке запуска и завершится после охлаждения.
    Держите страницу открытой; CSV останется доступен до её закрытия.
  </p>{/if}
<a class="secondary" href="#/heater">Параметры отопителя</a>

<style>
  .readings {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
  .trial-actions {
    margin-top: 20px;
  }
  .trial-actions button {
    width: 100%;
  }
  .trial-actions .caption {
    margin-top: 12px;
  }
  .trial-countdown {
    display: grid;
    gap: 6px;
    margin-top: 20px;
  }
  .trial-countdown span {
    color: var(--secondary);
    font-size: 13px;
  }
  .trial-countdown strong {
    font-size: 34px;
    font-variant-numeric: tabular-nums;
  }
  .trial-process h2 {
    margin: 8px 0 16px;
  }
</style>
