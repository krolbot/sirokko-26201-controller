<script>
  import Icon from "../Icon.svelte";
  import DraftActions from "../components/DraftActions.svelte";
  import History from "../components/History.svelte";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
    blockedActionLink,
  } from "../lib/presentation.js";
  import { modelRevisionPayload, formatDuration } from "../lib/protocol.js";
  let { ui, session, draft, notify } = $props();
  const snapshot = $derived(ui.snapshot);
  const calibration = $derived(snapshot.commissioning);
  const model = $derived(snapshot.learned_model);
  const ready = $derived(connectionReady(ui));
  const help = $derived(blockedActionLink(snapshot, "CALIBRATE"));
  function values() {
    return {
      minimum_speed_code: model.minimum_speed_code,
      maximum_speed_code: model.maximum_speed_code,
      slew_up_per_s: model.slew_up_per_s,
      slew_down_per_s: model.slew_down_per_s,
    };
  }
  async function save() {
    try {
      await session.submit("MODEL_REVISION", {
        revision: modelRevisionPayload(model, draft.value),
      });
    } catch (error) {
      notify("error", error.message);
    }
  }
</script>

<div class="page-heading"><h1>Калибровка</h1></div>
<section class="process-card surface">
  <div class="process-icon">
    <Icon
      name={model.valid && !calibration.active ? "check" : "heat"}
      size={32}
    />
  </div>
  <h2>
    {calibration.active
      ? calibration.phase.ru
      : model.valid
        ? "Калибровка выполнена"
        : "Автоматическая калибровка"}
  </h2>
  {#if calibration.active}
    <dl class="reading-list">
      <div>
        <dt>Измерено точек</dt>
        <dd>{calibration.accepted_point_count}</dd>
      </div>
      <div>
        <dt>Прошло</dt>
        <dd>{formatDuration(calibration.elapsed_ms)}</dd>
      </div>
    </dl>
  {:else if commandAllowed(snapshot, "CALIBRATE")}
    <p class="caption">Во время калибровки отопитель будет работать</p>
    <button
      class="primary full-width"
      disabled={!ready}
      onclick={() => session.submit("CALIBRATE")}
      >{model.valid ? "Повторить калибровку" : "Начать калибровку"}</button
    >
  {:else}<p class="caption">{commandReason(snapshot, "CALIBRATE")}</p>
    {#if help}<a class="secondary" href={help.href}>{help.label}</a>{/if}{/if}
  {#if calibration.reason?.code && !["NONE", "COMPLETE"].includes(calibration.reason.code)}<p
      class="caption"
    >
      {calibration.reason.ru}
    </p>{/if}
</section>
<History {snapshot} api={session.api} online={ui.link === "online"} />
{#if model.valid && draft.value}<details class="surface advanced section">
    <summary>Рабочий диапазон<Icon name="chevron" size={16} /></summary>
    <div class="disclosure-body">
      <p class="caption">Диапазон можно сузить после калибровки</p>
      {#each [["minimum_speed_code", "Минимум ШИМ"], ["maximum_speed_code", "Максимум ШИМ"]] as [field, label]}
        <label
          >{label}<input
            type="number"
            step="1"
            min={model.minimum_speed_code}
            max={model.maximum_speed_code}
            value={draft.value[field]}
            disabled={!ready ||
              !commandAllowed(snapshot, "MODEL_REVISION") ||
              draft.conflict}
            oninput={(event) =>
              draft.set(field, Number(event.currentTarget.value))}
          /></label
        >
      {/each}
      <DraftActions
        busy={Boolean(ui.pending || ui.requestInFlight)}
        {draft}
        enabled={ready && commandAllowed(snapshot, "MODEL_REVISION")}
        {save}
        reset={() => draft.reset(snapshot, values())}
      />
    </div>
  </details>{/if}
