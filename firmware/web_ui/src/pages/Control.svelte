<script>
  import Dial from "../components/Dial.svelte";
  import Icon from "../Icon.svelte";
  import DraftActions from "../components/DraftActions.svelte";
  import { combustionText } from "../lib/history.js";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
    blockedActionLink,
    stateText,
    timerLabel,
  } from "../lib/presentation.js";
  import {
    fullSettingsPayload,
    isActiveSession,
    formatDuration,
    manualMinimum,
  } from "../lib/protocol.js";
  let { ui, session, settings, notify } = $props();
  const snapshot = $derived(ui.snapshot);
  const ready = $derived(connectionReady(ui));
  const help = $derived(blockedActionLink(snapshot, "START"));
  const active = $derived(isActiveSession(snapshot));
  const process = $derived(
    snapshot.purge?.active ||
      ["PREHEAT", "IGNITION", "FAULT", "MAINTENANCE"].includes(
        snapshot.state.code,
      ),
  );
  const processText = $derived(
    snapshot.purge?.active
      ? snapshot.purge.minimum_remaining_ms > 0
        ? `Охлаждение ещё не менее ${formatDuration(snapshot.purge.minimum_remaining_ms)}`
        : snapshot.purge.sensors_fresh
          ? `Ждём выхлоп ниже ${snapshot.envelope.parameters.cooling_exhaust_milli_c / 1000} °C`
          : "Нет свежих показаний термопар"
      : {
          PREHEAT: "Двигатель и муфта выключены",
          IGNITION: "Двигатель, муфта и свеча включены",
          FAULT: "Свеча и муфта выключены",
          MAINTENANCE: "Установка новой версии",
        }[snapshot.state.code] || "",
  );
  const edit = $derived(
    ready && commandAllowed(snapshot, "SETTINGS") && !settings.conflict,
  );
  const manual = $derived(settings.value?.mode === "MANUAL");
  const lowerPower = $derived(manualMinimum(snapshot.envelope?.parameters));
  const handoff = $derived(
    manual && active && snapshot.settings.mode === "TEMPERATURE",
  );
  async function saveSettings() {
    try {
      await session.submit("SETTINGS", {
        settings: fullSettingsPayload(snapshot.settings, settings.value),
      });
    } catch (error) {
      notify("error", error.message);
    }
  }
  function chooseMode(mode) {
    settings.set("mode", mode);
    if (mode === "MANUAL" && active && snapshot.settings.mode === "TEMPERATURE")
      settings.set(
        "manual_percent",
        Math.max(
          lowerPower,
          Math.min(100, Math.round(snapshot.outputs.command_percent / 5) * 5),
        ),
      );
  }
</script>

{#snippet controls()}
  <div class="segmented" role="group" aria-label="Режим отопления">
    <button
      aria-pressed={!manual}
      disabled={!edit}
      onclick={() => chooseMode("TEMPERATURE")}>По температуре</button
    >
    <button
      aria-pressed={manual}
      disabled={!edit}
      onclick={() => chooseMode("MANUAL")}>Ручной</button
    >
  </div>
  <Dial
    value={manual
      ? settings.value.manual_percent
      : settings.value.room_setpoint_milli_c / 1000}
    min={manual ? lowerPower : 15}
    max={manual ? 100 : 30}
    step={manual ? 5 : 1}
    {manual}
    sensor={snapshot.sensors.room}
    heating={snapshot.state.code === "RUN" &&
      snapshot.outputs.fuel &&
      !settings.dirty}
    disabled={!edit || handoff}
    change={(value) =>
      settings.set(
        manual ? "manual_percent" : "room_setpoint_milli_c",
        manual ? value : value * 1000,
      )}
  />
  {#if handoff}<p class="caption centered">
      Ручной режим начнётся с текущего ШИМ двигателя
    </p>{/if}
  {#if snapshot.room_excess}<p class="caption centered">
      Температура достигнута
    </p>{/if}
{/snippet}
<div class="page-heading">
  <h1>Отопление</h1>
  {#if manual}<p class="caption">% от максимального ШИМ двигателя</p>{/if}
  {#if !process}<span class="state-label" class:heating={active}
      ><i></i>{stateText(snapshot)}</span
    >{/if}
</div>
<div class="control-layout">
  <section class="thermostat surface" aria-label="Управление отоплением">
    {#if process}
      <div class="stage-display">
        <div class="process-icon" class:cooling={snapshot.purge?.active}>
          <Icon name={snapshot.purge?.active ? "fan" : "heat"} size={36} />
        </div>
        <h2>
          {stateText(snapshot)}
        </h2>
        <p class="caption">{processText}</p>
      </div>
      <details class="stage-settings">
        <summary>Температура и режим<Icon name="chevron" size={16} /></summary
        >{@render controls()}
      </details>
    {:else}{@render controls()}{/if}
    {#if settings.dirty || settings.conflict || ui.pending?.action === "SETTINGS" || ui.requestInFlight === "SETTINGS" || ["IDLE", "UNCONFIGURED"].includes(snapshot.state.code)}
      <div class="heater-action">
        {#if settings.dirty || settings.conflict}
          <DraftActions
            busy={Boolean(ui.pending || ui.requestInFlight)}
            draft={settings}
            enabled={edit}
            save={saveSettings}
            reset={() => settings.reset(snapshot, snapshot.settings)}
            label="Применить"
          />
        {:else if ui.pending?.action === "SETTINGS" || ui.requestInFlight === "SETTINGS"}<p
            class="activity"
            role="status"
          >
            Применяем настройки…
          </p>
        {:else if ["IDLE", "UNCONFIGURED"].includes(snapshot.state.code)}
          {#if commandAllowed(snapshot, "START")}<button
              class="primary full-width"
              disabled={!ready}
              onclick={() => session.submit("START")}
              ><Icon name="power" />Включить отопление</button
            >
          {:else}<p class="caption">{commandReason(snapshot, "START")}</p>
            {#if help}<a class="primary full-width" href={help.href}
                >{help.label}</a
              >{/if}{/if}
        {/if}
      </div>
    {/if}
  </section>
  <div class="control-secondary">
    {#if snapshot.combustion?.phase === "SUSPECT"}<p
        class="notice"
        role="status"
      >
        {combustionText(snapshot.combustion.phase)}
      </p>{/if}
    {#if (active && snapshot.timer.active) || (!active && snapshot.settings.duration_ms > 0)}
      <a class="list-row surface" href="#/timer">
        <span class="row-icon"><Icon name="clock" /></span>
        <span class="row-title"
          >{active ? "До остановки" : "Таймер после запуска"}</span
        >
        <span class="row-value"
          >{timerLabel(
            active
              ? snapshot.timer.remaining_ms
              : snapshot.settings.duration_ms,
          )}</span
        >
        <Icon name="chevron" size={16} />
      </a>
    {/if}
    <a class="list-row surface" href="#/diagnostics"
      ><span class="row-icon"><Icon name="thermometer" /></span><span
        class="row-title">Показания и журнал</span
      ><Icon name="chevron" size={16} /></a
    >
  </div>
</div>
