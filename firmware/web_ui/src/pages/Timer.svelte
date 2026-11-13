<script>
  import Icon from "../Icon.svelte";
  import DraftActions from "../components/DraftActions.svelte";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
    countdownText,
  } from "../lib/presentation.js";
  import {
    fullSettingsPayload,
    isActiveSession,
    millisecondsFromMinutes,
    MAX_TIMER_MINUTES,
  } from "../lib/protocol.js";
  let { ui, session, timer, notify } = $props();
  let editing = $state(false);
  const snapshot = $derived(ui.snapshot);
  const active = $derived(isActiveSession(snapshot));
  const action = $derived(active ? "TIMER" : "SETTINGS");
  const duration = $derived(
    active ? snapshot.timer.remaining_ms : snapshot.settings.duration_ms,
  );
  const allowed = $derived(
    connectionReady(ui) && commandAllowed(snapshot, action) && !timer.conflict,
  );
  const editor = $derived(
    editing || timer.dirty || timer.conflict || !duration,
  );
  const minutes = $derived(timer.value?.minutes ?? 0);
  const days = $derived(Math.floor(minutes / 1440));
  const hours = $derived(Math.floor(minutes / 60) % 24);
  const remainder = $derived(minutes % 60);
  function choose(day, hour, minute) {
    timer.set(
      "minutes",
      Math.min(MAX_TIMER_MINUTES, day * 1440 + hour * 60 + minute),
    );
  }
  function reset() {
    timer.reset(snapshot, {
      deadline_ms: active ? snapshot.timer.deadline_ms : 0,
      minutes: Math.ceil(duration / 60000),
    });
    editing = false;
  }
  async function save(value = minutes) {
    if (!allowed) return;
    try {
      const delay = millisecondsFromMinutes(value);
      const result = active
        ? await session.submit("TIMER", { delay_ms: delay })
        : await session.submit("SETTINGS", {
            settings: fullSettingsPayload(snapshot.settings, {
              duration_ms: delay,
            }),
          });
      if (result.status === "QUEUED") editing = false;
    } catch (error) {
      notify("error", error.message);
    }
  }
  function cancelTimer() {
    timer.set("minutes", 0);
    void save(0);
  }
</script>

<div class="page-heading"><h1>Таймер</h1></div>
<div class="timer-layout">
  <section
    class="surface timer-display"
    class:compact={editor && active}
    aria-label="Состояние таймера"
  >
    <div class="timer-symbol" class:running={active && duration > 0}>
      <Icon name="clock" size={28} />
    </div>
    {#if duration > 0}
      <p class="eyebrow">
        {active ? "До остановки нагрева" : "Длительность после запуска"}
      </p>
      <p
        class="countdown"
        aria-label={active ? "Осталось" : "Заданная длительность"}
      >
        {countdownText(duration)}
      </p>
      {#if !editor}<div class="timer-actions">
          <button
            class="primary"
            disabled={!allowed}
            onclick={() => (editing = true)}>Изменить</button
          >
          <button class="text-button" disabled={!allowed} onclick={cancelTimer}
            >Выключить таймер</button
          >
        </div>{/if}
    {:else}
      <h2>Без таймера</h2>
      <p class="caption">Нагрев без ограничения времени</p>
    {/if}
  </section>
  {#if editor}<section
      class="surface timer-editor"
      aria-label="Длительность таймера"
    >
      <h2>{active ? "Остановить через" : "Длительность после запуска"}</h2>
      <div class="duration-picker">
        {#each [["Дни", days, 7], ["Часы", hours, 23], ["Минуты", remainder, 59]] as [label, value, maximum], index}
          <label class="duration-column">
            <span class="duration-label">{label}</span>
            <span class="duration-value">{String(value).padStart(2, "0")}</span>
            <Icon name="chevron" size={14} />
            <select
              aria-label={label}
              {value}
              disabled={!allowed || (days === 7 && index > 0)}
              onchange={(event) => {
                const next = Number(event.currentTarget.value);
                choose(
                  index === 0 ? next : days,
                  index === 1 ? next : hours,
                  index === 2 ? next : remainder,
                );
              }}
            >
              {#each Array.from({ length: maximum + 1 }, (_, n) => n) as n}<option
                  value={n}>{n}</option
                >{/each}
            </select>
          </label>
        {/each}
      </div>
      <div
        class="presets timer-presets"
        role="group"
        aria-label="Быстрый выбор"
      >
        {#each [[30, "30 мин"], [60, "1 час"], [120, "2 часа"], [240, "4 часа"]] as [value, label]}
          <button
            aria-pressed={minutes === value}
            disabled={!allowed}
            onclick={() => timer.set("minutes", value)}>{label}</button
          >
        {/each}
      </div>
      <DraftActions
        busy={Boolean(ui.pending || ui.requestInFlight)}
        draft={timer}
        enabled={allowed}
        save={() => save()}
        {reset}
        label={minutes === 0
          ? "Выключить таймер"
          : active
            ? "Начать отсчёт"
            : "Сохранить таймер"}
      />
      {#if editing && !timer.dirty && !timer.conflict}<button
          class="text-button full-width"
          onclick={reset}>Закрыть</button
        >{/if}
    </section>{/if}
</div>
{#if !commandAllowed(snapshot, action)}<p class="caption">
    {commandReason(snapshot, action)}
  </p>{/if}
