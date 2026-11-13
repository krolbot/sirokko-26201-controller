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
  import { Draft } from "../lib/draft.svelte.js";
  let {
    ui,
    api,
    session,
    notify,
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
  const measuring = $derived(active && phase === "RUN");
  const pwm = new Draft(["speed_code"]);
  const pwmMaximum = $derived(snapshot.envelope.limits.pwm_max);
  const pwmPercent = $derived(
    Math.round(
      (100 * (pwm.value?.speed_code ?? snapshot.outputs.speed_code)) /
        pwmMaximum,
    ),
  );
  let externalPwmChange = $state(false);
  $effect(() => {
    const actual = { speed_code: snapshot.outputs.speed_code };
    pwm.sync(snapshot, actual);
    if (pwm.conflict || !measuring) {
      externalPwmChange = pwm.conflict;
      pwm.reset(snapshot, actual);
    }
  });
  function changePwm(event) {
    externalPwmChange = false;
    pwm.set(
      "speed_code",
      Math.max(
        1,
        Math.round((Number(event.currentTarget.value) * pwmMaximum) / 100),
      ),
    );
  }
  async function submit(action, payload = {}) {
    try {
      await session.submit(action, payload);
    } catch (error) {
      notify("error", error.message);
    }
  }
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
          : phase === "IGNITION"
            ? "На подтверждение горения"
            : "До окончания измерений",
  );
</script>

<div class="page-heading"><h1>Первый запуск</h1></div>
<p class="intro">
  Запуск и запись температур без заполненного профиля отопителя.
</p>
{#if active}
  <section class="surface info-panel section trial-process" aria-live="polite">
    <h2>
      {phase === "TRIAL_READY"
        ? "Готов к началу"
        : measuring
          ? "Прогрев и измерения"
          : stateText(snapshot)}
    </h2>
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
        <button
          class="primary confirm-ignition"
          disabled={!ready || !commandAllowed(snapshot, "TRIAL_CONFIRM")}
          onclick={() => submit("TRIAL_CONFIRM")}>Горение устойчивое</button
        >
        {#if snapshot.outputs.glow}<p class="caption">
            Подтверждение доступно после выключения свечи.
          </p>{/if}
      {:else if measuring}
        <p class="caption">
          Сначала прогрейте отопитель на полном ШИМ около 10 минут. Затем
          меняйте ШИМ поэтапно и дождитесь установления температуры на каждой
          ступени.
        </p>
        <p class="caption">
          После подтверждения: {countdownText(snapshot.trial.elapsed_ms)}
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
      свеча выключится. Подтвердите устойчивое горение, чтобы перейти к
      измерениям и регулировке ШИМ. Без подтверждения топливо отключится через
      90 с после его подачи. На измерения — до 30 минут с удержанием KEY0.
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

{#if measuring}
  <section class="surface info-panel section">
    <h2>ШИМ двигателя</h2>
    <p class="pwm-value">
      {pwmPercent} % <span>· {pwm.value?.speed_code} / {pwmMaximum}</span>
    </p>
    <input
      class="pwm-slider"
      type="range"
      aria-label="ШИМ двигателя"
      min="1"
      max="100"
      step="1"
      value={pwmPercent}
      disabled={!ready || !commandAllowed(snapshot, "TRIAL_PWM")}
      oninput={changePwm}
    />
    <p class="caption">
      Применено: {snapshot.outputs.speed_code} / {pwmMaximum}. Изменения и
      температуры попадают в CSV.
    </p>
    {#if externalPwmChange}<p class="caption" role="status">
        ШИМ изменён на другом пульте. Показано текущее значение.
      </p>{/if}
    <button
      class="primary"
      disabled={!ready ||
        !commandAllowed(snapshot, "TRIAL_PWM") ||
        !pwm.dirty ||
        pwm.value?.speed_code === snapshot.outputs.speed_code}
      onclick={() => submit("TRIAL_PWM", { speed_code: pwm.value.speed_code })}
      >Применить ШИМ</button
    >
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
  .confirm-ignition {
    margin-top: 16px;
  }
  .pwm-value {
    font-size: 28px;
    margin: 16px 0;
    font-variant-numeric: tabular-nums;
  }
  .pwm-value span {
    font-size: 14px;
    color: var(--secondary);
  }
  .pwm-slider {
    accent-color: var(--accent);
    width: 100%;
    margin: 8px 0 16px;
  }
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
