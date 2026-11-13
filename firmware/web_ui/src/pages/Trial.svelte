<script>
  import History from "../components/History.svelte";
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
    startTrial,
    suggestion,
    selectTrialPwm,
    saveTrialProfile,
  } = $props();
  const snapshot = $derived(ui.snapshot);
  const active = $derived(trialActive(snapshot));
  const pending = $derived(
    ui.pending?.action === "TRIAL_START" ||
      ui.requestInFlight === "TRIAL_START",
  );
  const ready = $derived(connectionReady(ui));
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
      ? "Охлаждение ещё не менее"
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
  Выберите нижний ШИМ при устойчивом горении. По измерениям предложим
  температуру отключения топлива.
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
        Нажмите KEY0 на панели для запуска. Повторное нажатие остановит нагрев.
      </p>
    {:else if cooling}
      <p>Свеча и топливо выключены. Двигатель охлаждает отопитель.</p>
    {:else}
      <p>Для остановки нажмите KEY0 или «Остановить запуск» в веб-пульте.</p>
      {#if phase === "IGNITION"}
        <p class="caption">
          Если розжига нет, нажмите KEY0; не ждите окончания отсчёта.
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
          Сначала прогрейте отопитель при ШИМ 100% около 10 минут. Затем меняйте
          ШИМ постепенно. После каждого изменения дождитесь, пока температура
          перестанет заметно меняться.
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
{:else if !suggestion?.samples}
  <section class="surface info-panel section">
    <h2>Порядок запуска</h2>
    <p class="caption">
      После 30 с накала включатся двигатель и подача топлива. Ещё через 10 с
      свеча выключится. Подтвердите устойчивое горение, чтобы перейти к
      измерениям и регулировке ШИМ. Без подтверждения топливо отключится через
      90 с после его подачи. На измерения — до 30 минут.
    </p>
    <p class="notice">
      Перед запуском восстановите штатное отключение муфты при перегреве.
    </p>
    <div class="trial-actions">
      <button
        class="primary"
        disabled={!ready || !commandAllowed(snapshot, "TRIAL_START")}
        onclick={startTrial}>Подготовить запуск</button
      >
      {#if !commandAllowed(snapshot, "TRIAL_START")}<p class="caption">
          {commandReason(snapshot, "TRIAL_START")}
        </p>{/if}
    </div>
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
      Текущий ШИМ: {snapshot.outputs.speed_code} / {pwmMaximum}. Изменения и
      температуры записываются автоматически.
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
    <button
      class="secondary select-pwm"
      disabled={!ready ||
        Boolean(ui.pending || ui.requestInFlight) ||
        !commandAllowed(snapshot, "TRIAL_SELECT") ||
        pwm.dirty}
      onclick={selectTrialPwm}>Выбрать этот ШИМ как нижний</button
    >
    <p class="caption">
      Выбирайте после проверки устойчивого горения. Настройки сохраняются после
      остановки и охлаждения.
    </p>
  </section>
{/if}
{#if suggestion?.pwm}
  <section class="surface info-panel section" aria-live="polite">
    <h2>
      {suggestion.saved ? "Настройки сохранены" : "Настройки по измерениям"}
    </h2>
    <p>Не снижать ШИМ ниже: <strong>{suggestion.pwm}</strong></p>
    {#if suggestion.saved}
      <p>
        Отключать топливо при выхлопе ниже: <strong
          >{snapshot.envelope.parameters.minimum_run_exhaust_milli_c / 1000} °C</strong
        >
      </p>
      <a class="secondary" href="#/control">Перейти к отоплению</a>
    {:else if suggestion.ready}
      <p>
        Отключать топливо при выхлопе ниже: <strong
          >{suggestion.temperature_milli_c / 1000} °C</strong
        >
      </p>
      <p class="caption">
        Начальное предложение — между {(
          suggestion.reference_milli_c / 1000
        ).toFixed(1)} °C (до розжига или завершение охлаждения) и {(
          suggestion.minimum_milli_c / 1000
        ).toFixed(1)} °C (минимум при подтверждённом горении в выбранном диапазоне
        ШИМ).
      </p>
      <p class="caption">
        Проверьте при следующем холодном запуске. Температура выхлопа реагирует
        на погасание с задержкой.
      </p>
      <button
        class="primary"
        disabled={!ready ||
          !suggestion.complete ||
          !commandAllowed(snapshot, "PROFILE") ||
          Boolean(ui.pending || ui.requestInFlight)}
        onclick={saveTrialProfile}>Сохранить ШИМ и температуру</button
      >
      {#if !suggestion.complete}<p class="caption">
          Сохранение будет доступно после остановки и охлаждения.
        </p>{/if}
    {:else}<p class="caption">{suggestion.reason}</p>{/if}
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
<section class="surface info-panel section">
  <h2>Измерения первого запуска</h2>
  <p>{suggestion?.samples ?? 0} измерений · запись и расчёт на ESP</p>
  <p class="caption">
    Страницу можно закрыть. Измерения и выбранный ШИМ останутся до перезапуска
    ESP или нового первого запуска. Сохранённые настройки сохраняются и после
    отключения питания.
  </p>
  {#if !active && suggestion?.samples}
    <button
      class="secondary"
      disabled={!ready || pending || !commandAllowed(snapshot, "TRIAL_START")}
      onclick={startTrial}>Новый первый запуск</button
    >
    <p class="caption">
      Заменит текущую запись. Сохранённые настройки не изменятся.
    </p>
  {/if}
</section>
<a class="secondary" href="#/heater">Параметры отопителя</a>

<style>
  .confirm-ignition {
    margin-top: 16px;
  }
  .select-pwm {
    margin-top: 12px;
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
