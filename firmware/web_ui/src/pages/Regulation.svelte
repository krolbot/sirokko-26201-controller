<script>
  import Icon from "../Icon.svelte";
  import DraftActions from "../components/DraftActions.svelte";
  import {
    defaultSlew,
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
  const defaultRate = $derived(defaultSlew(draft.value));
  const enabled = $derived(
    connectionReady(ui) &&
      commandAllowed(snapshot, "SETTINGS") &&
      !draft.conflict,
  );
  const fields = [
    ["slew_up_per_s", "Увеличение ШИМ"],
    ["slew_down_per_s", "Уменьшение ШИМ"],
  ];
  function useDefault() {
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
      {defaultRate ? "Исходный темп" : "Замедлено вручную"}
    </h2>
  </div>
  <p class="caption">Скорость изменения ШИМ в пределах рабочего диапазона.</p>
  {#each fields as [field, label]}
    {@const limit = MAXIMUM_SLEW_PER_S}
    {@const lower = Math.ceil((MINIMUM_SLEW_PER_S / limit) * 100)}
    {@const percent = Math.max(
      lower,
      Math.min(100, Math.round((draft.value[field] / limit) * 100)),
    )}
    <label class="slew-control">
      <span class="slew-heading"
        ><span>{label}</span><output
          >{percent === 100
            ? "Исходный темп"
            : `${percent}% от исходного темпа`}</output
        ></span
      >
      <input
        type="range"
        aria-label={label}
        aria-valuetext={`${percent}% от исходного темпа`}
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
                ((limit * Number(event.currentTarget.value)) / 100).toPrecision(
                  8,
                ),
              ),
            ),
          )}
      />
      <span class="slew-scale"><span>Медленнее</span><span>Быстрее</span></span>
    </label>
  {/each}
  {#if !defaultRate}<button
      class="text-button full-width"
      disabled={!enabled}
      onclick={useDefault}>Вернуть исходный темп</button
    >{/if}
  <DraftActions
    busy={Boolean(ui.pending || ui.requestInFlight)}
    {draft}
    {enabled}
    {save}
    reset={() => draft.reset(snapshot, snapshot.settings)}
    label="Применить"
  />
</section>
