<script>
  import { onMount } from "svelte";
  import History from "../components/History.svelte";
  import Measurements from "../components/Measurements.svelte";
  import { heaterFields, heaterValidation } from "../lib/heater.js";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
    temperature,
  } from "../lib/presentation.js";
  let {
    ui,
    session,
    notify,
    heater,
    recording,
    startRecording,
    stopRecording,
    downloadMeasurements,
    addMeasurementNote,
  } = $props();
  const snapshot = $derived(ui.snapshot);
  const active = $derived(snapshot.state.code === "DEBUG");
  const ready = $derived(connectionReady(ui));
  const outputs = $derived(snapshot.outputs);
  const maximum = $derived(snapshot.envelope?.limits?.pwm_max || 4014);
  const validation = $derived(
    heaterValidation(heater.value, snapshot.envelope?.limits),
  );
  const measurementFields = heaterFields.filter(([key]) =>
    [
      "maximum_exhaust_milli_c",
      "maximum_air_milli_c",
      "cooling_exhaust_milli_c",
      "minimum_run_exhaust_milli_c",
      "minimum_ignition_rise_milli_c",
      "minimum_ignition_air_rise_milli_c",
      "exploration_floor_code",
    ].includes(key),
  );
  let speed = $state(0);
  let editing = $state(false);
  let lastSent = 0;
  let observedBoot = null;
  let attended = false;
  $effect(() => {
    if (!editing) speed = outputs.speed_code;
  });
  async function enter() {
    const result = await session.submit("DEBUG_ENTER");
    attended = result?.status === "QUEUED";
    observedBoot = snapshot.boot_id;
    lastSent = performance.now();
  }
  async function apply(changes = {}) {
    if (!active || !ready) return;
    lastSent = performance.now();
    const result = await session.submit("DEBUG_SET", {
      speed_code: outputs.speed_code,
      glow: outputs.glow,
      fuel: outputs.fuel,
      ...changes,
    });
    if (result?.status === "QUEUED") {
      attended = true;
      observedBoot = snapshot.boot_id;
    }
  }
  async function applySpeed() {
    const next = Math.max(0, Math.min(maximum, Math.round(speed)));
    await apply({ speed_code: next, ...(next === 0 ? { fuel: false } : {}) });
    editing = false;
  }
  function rememberMinimum() {
    heater.set("exploration_floor_code", outputs.speed_code);
    notify(
      "ok",
      "ШИМ перенесён в параметры. Сохраните после завершения отладки",
    );
  }
  function leave() {
    attended = false;
    void session.stop();
  }
  async function saveParameters() {
    if (validation.valid)
      await session.submit("PROFILE", { envelope: validation.parameters });
  }
  onMount(() => {
    const visibility = () => {
      if (document.hidden && attended) leave();
    };
    const timer = setInterval(() => {
      if (
        observedBoot !== snapshot.boot_id ||
        (!active && ui.pending?.action !== "DEBUG_ENTER")
      )
        attended = false;
      if (
        attended &&
        active &&
        ready &&
        !document.hidden &&
        performance.now() - lastSent >= 1500
      )
        void apply();
    }, 250);
    document.addEventListener("visibilitychange", visibility);
    return () => {
      clearInterval(timer);
      document.removeEventListener("visibilitychange", visibility);
      if (attended && active) leave();
    };
  });
</script>

<div class="page-heading"><h1>Режим отладки</h1></div>
<section
  class="surface info-panel section"
  aria-label="Ручное управление выходами"
>
  {#if active}
    <p class="caption">
      Ручное управление · при потере связи остановка через 5 секунд
    </p>
    <label class="speed-label" for="debug-speed"
      >ШИМ двигателя <strong
        >{((outputs.speed_code / 4096) * 100).toLocaleString("ru-RU", {
          minimumFractionDigits: 1,
          maximumFractionDigits: 1,
        })} %</strong
      ></label
    >
    <input
      id="debug-speed"
      type="range"
      min="0"
      max={maximum}
      step="1"
      value={speed}
      disabled={!ready}
      oninput={(event) => {
        editing = true;
        speed = Number(event.currentTarget.value);
      }}
      onchange={applySpeed}
    />
    <div class="form-actions">
      <button
        class="secondary"
        disabled={!ready}
        onclick={() =>
          apply({ speed_code: outputs.speed_code ? 0 : maximum, fuel: false })}
        >{outputs.speed_code
          ? "Выключить двигатель"
          : "Включить двигатель"}</button
      >
      <button
        class="secondary"
        disabled={!ready || !outputs.speed_code}
        onclick={rememberMinimum}>Запомнить минимум</button
      >
    </div>
    <div class="actuators">
      <button
        class="secondary"
        aria-pressed={outputs.glow}
        disabled={!ready || (!outputs.glow && !snapshot.debug?.heat_allowed)}
        onclick={() => apply({ glow: !outputs.glow })}
        >Свеча · {outputs.glow ? "включена" : "выключена"}</button
      >
      <button
        class="secondary"
        aria-pressed={outputs.fuel}
        disabled={!ready ||
          (!outputs.fuel &&
            (!snapshot.debug?.heat_allowed || !outputs.speed_code))}
        onclick={() => apply({ fuel: !outputs.fuel })}
        >Муфта · {outputs.fuel ? "включена" : "выключена"}</button
      >
    </div>
    {#if !snapshot.debug?.heat_allowed}<p class="caption">
        {snapshot.debug?.heat_reason}
      </p>{/if}
    <p class="caption">
      Свеча отключится через 60 секунд непрерывного накала. Без заданного и
      достигнутого порога горения подача топлива ограничена 90 секундами.
    </p>
  {:else}
    <p class="caption">
      Двигатель, свеча и топливная муфта управляются отдельно. После нагрева
      выход из режима запускает охлаждение.
    </p>
    <button
      class="primary"
      disabled={!ready || !commandAllowed(snapshot, "DEBUG_ENTER")}
      onclick={enter}>Включить режим отладки</button
    >
    {#if !commandAllowed(snapshot, "DEBUG_ENTER")}<p class="caption">
        {snapshot.purge?.active
          ? "Идёт охлаждение — дождитесь завершения"
          : commandReason(snapshot, "DEBUG_ENTER")}
      </p>{/if}
  {/if}
  <p class="caption"><a href="#/heater">Параметры отопителя</a></p>
</section>
<section class="surface info-panel section" aria-label="Текущие температуры">
  <dl class="reading-list">
    <div>
      <dt>Выхлоп</dt>
      <dd>{temperature(snapshot.sensors.exhaust)}</dd>
    </div>
    <div>
      <dt>Нагретый воздух</dt>
      <dd>{temperature(snapshot.sensors.air)}</dd>
    </div>
  </dl>
</section>
<History {snapshot} api={session.api} online={ui.link === "online"} />
<details class="surface info-panel section">
  <summary
    >Параметры по измерениям{heater.dirty ? " · есть изменения" : ""}</summary
  >
  <p class="caption">
    Заполняйте во время наблюдения. Сохранение доступно после завершения отладки
    и охлаждения.
  </p>
  <div class="observed-fields">
    {#each measurementFields as [key, label, unit]}
      <label
        >{label}, {unit}<input
          type="number"
          step={unit === "ШИМ" ? "1" : "0.1"}
          min="0"
          value={heater.value?.[key] ?? ""}
          disabled={heater.conflict}
          placeholder="Не задано"
          oninput={(event) =>
            heater.set(
              key,
              event.currentTarget.value === ""
                ? ""
                : Number(event.currentTarget.value),
            )}
        /></label
      >
      {#if validation.errors[key]}<p class="caption">
          {validation.errors[key]}
        </p>{/if}
    {/each}
  </div>
  <button
    class="primary"
    disabled={!ready ||
      !commandAllowed(snapshot, "PROFILE") ||
      !heater.dirty ||
      heater.conflict ||
      !validation.valid}
    onclick={saveParameters}>Сохранить параметры</button
  >
  {#if heater.conflict}<p class="caption">
      Параметры изменены с другого пульта. Откройте параметры отопителя и
      обновите форму.
    </p>{/if}
</details>
<Measurements
  {recording}
  online={ui.link === "online"}
  start={startRecording}
  stop={stopRecording}
  download={downloadMeasurements}
  addNote={addMeasurementNote}
/>

<style>
  .speed-label {
    display: flex;
    justify-content: space-between;
    gap: 16px;
    margin-top: 24px;
  }
  input[type="range"] {
    width: 100%;
    margin: 20px 0;
    padding: 0;
    accent-color: var(--accent);
  }
  .actuators {
    display: flex;
    flex-wrap: wrap;
    gap: 12px;
    margin: 24px 0;
  }
  .actuators button[aria-pressed="true"] {
    outline: 2px solid var(--accent);
  }
  .observed-fields {
    display: grid;
    gap: 20px;
    margin: 24px 0;
  }
  .observed-fields input {
    display: block;
    width: 100%;
    margin-top: 8px;
  }
</style>
