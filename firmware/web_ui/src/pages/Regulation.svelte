<script>
  import Icon from "../Icon.svelte";
  import DraftActions from "../components/DraftActions.svelte";
  import {
    automaticSlew,
    commandAllowed,
    connectionReady,
  } from "../lib/presentation.js";
  import {
    fullSettingsPayload,
    MINIMUM_SLEW_PER_S,
    MAXIMUM_SLEW_PER_S,
  } from "../lib/protocol.js";
  let { ui, session, draft, notify } = $props();
  const snapshot = $derived(ui.snapshot);
  const automatic = $derived(automaticSlew(draft.value));
  const model = $derived(snapshot.learned_model);
  const enabled = $derived(
    connectionReady(ui) &&
      commandAllowed(snapshot, "SETTINGS") &&
      !draft.conflict,
  );
  const fields = [
    ["slew_up_per_s", "Увеличение ШИМ"],
    ["slew_down_per_s", "Уменьшение ШИМ"],
  ];
  function useAutomatic() {
    for (const [field] of fields) draft.set(field, MAXIMUM_SLEW_PER_S);
  }
  async function save() {
    if (!enabled) return;
    try {
      await session.submit("SETTINGS", {
        settings: fullSettingsPayload(snapshot.settings, draft.value),
      });
    } catch (error) {
      notify("error", error.message);
    }
  }
</script>

<div class="page-heading"><h1>Темп изменения ШИМ</h1></div>
<section class="surface smoothing-panel">
  <div class="smoothing-status">
    <Icon name="tune" size={24} />
    <h2>
      {automatic
        ? model.valid
          ? "По калибровке"
          : "Исходный темп"
        : "Замедлено вручную"}
    </h2>
  </div>
  <p class="caption">
    {model.valid
      ? "Скорость изменения ШИМ двигателя ограничена калибровкой. Здесь её можно уменьшить."
      : "Калибровка использует исходный темп изменения ШИМ."}
  </p>
  {#if model.valid}
    {#each fields as [field, label]}
      {@const limit = Math.min(model[field], MAXIMUM_SLEW_PER_S)}
      {@const lower = Math.ceil((MINIMUM_SLEW_PER_S / limit) * 100)}
      {@const percent = Math.max(
        lower,
        Math.min(100, Math.round((draft.value[field] / limit) * 100)),
      )}
      <label class="slew-control">
        <span class="slew-heading"
          ><span>{label}</span><output
            >{percent === 100
              ? "По калибровке"
              : `${percent}% от темпа калибровки`}</output
          ></span
        >
        <input
          type="range"
          aria-label={label}
          aria-valuetext={`${percent}% от темпа калибровки`}
          min={lower}
          max="100"
          step="1"
          value={percent}
          disabled={!enabled || lower >= 100}
          style={`--position:${lower === 100 ? 100 : ((percent - lower) / (100 - lower)) * 100}%`}
          oninput={(event) =>
            draft.set(
              field,
              Math.max(
                MINIMUM_SLEW_PER_S,
                Number(
                  (
                    (limit * Number(event.currentTarget.value)) /
                    100
                  ).toPrecision(8),
                ),
              ),
            )}
        />
        <span class="slew-scale"
          ><span>Медленнее</span><span>Быстрее</span></span
        >
      </label>
    {/each}
  {/if}
  {#if !automatic}<button
      class="text-button full-width"
      disabled={!enabled}
      onclick={useAutomatic}
      >{model.valid
        ? "Вернуть темп калибровки"
        : "Вернуть исходный темп"}</button
    >{/if}
  <DraftActions
    busy={Boolean(ui.pending || ui.requestInFlight)}
    {draft}
    {enabled}
    {save}
    reset={() => draft.reset(snapshot, snapshot.settings)}
    label="Применить"
  />
  {#if !model.valid && !draft.dirty && snapshot.envelope?.provisioned}
    <a class="secondary full-width" href="#/calibration">Перейти к калибровке</a
    >
  {/if}
</section>
