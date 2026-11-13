<script>
  import DraftActions from "../components/DraftActions.svelte";
  import {
    heaterGroups,
    heaterDraft,
    heaterValidation,
  } from "../lib/heater.js";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
  } from "../lib/presentation.js";
  let { ui, session, draft, notify } = $props();
  let touched = $state({});
  const snapshot = $derived(ui.snapshot);
  const limits = $derived(snapshot.envelope?.limits);
  const validation = $derived(heaterValidation(draft.value, limits));
  const editing = $derived(
    connectionReady(ui) &&
      commandAllowed(snapshot, "PROFILE") &&
      !draft.conflict,
  );
  function reset() {
    draft.reset(snapshot, heaterDraft(snapshot.envelope));
    touched = {};
  }
  async function save() {
    if (!validation.valid) return;
    try {
      await session.submit("PROFILE", { envelope: validation.parameters });
    } catch (error) {
      notify("error", error.message);
    }
  }
</script>

<div class="page-heading"><h1>Параметры отопителя</h1></div>
<p class="intro">Sirokko 262.01 · Параметры установки</p>
<p class="caption">
  Можно сохранять по мере заполнения. Для проверки двигателя используйте <a
    href="#/debug">режим отладки</a
  >.
</p>
{#if !commandAllowed(snapshot, "PROFILE")}<p class="notice" role="status">
    {commandReason(snapshot, "PROFILE")}
  </p>{/if}
{#if snapshot.learned_model?.valid}<p class="caption">
    После изменения параметров понадобится новая калибровка
  </p>{/if}
{#if draft.value && limits}
  <div class="form-column heater-profile">
    {#each heaterGroups as group}
      <section class="surface form-panel">
        <h2>{group.title}</h2>
        {#if group.title === "Время этапов"}<p class="caption">
            Розжиг и проверка вместе — не более {limits.firing_max_ms / 1000} с
          </p>
        {:else if group.title === "Температурные пределы"}<p class="caption">
            Для мест установки термопар выхлопа и нагретого воздуха
          </p>
        {:else}<p class="caption">
            Команды ШИМ от 1 до {limits.pwm_max}; это не измеренные обороты
          </p>{/if}
        <div class="parameter-fields">
          {#each group.fields as [key, label, unit, scale]}
            <div class="parameter-field">
              <label for={key}>{label}<span>{unit}</span></label>
              <input
                id={key}
                type="number"
                inputmode={scale === 1 ? "numeric" : "decimal"}
                min={1 / scale}
                step={scale === 1 ? 1 : 0.001}
                value={draft.value[key]}
                disabled={!editing}
                placeholder="Не задано"
                aria-invalid={Boolean(touched[key] && validation.errors[key])}
                aria-describedby={touched[key] && validation.errors[key]
                  ? `${key}-error`
                  : undefined}
                oninput={(event) =>
                  draft.set(
                    key,
                    event.currentTarget.value === ""
                      ? ""
                      : Number(event.currentTarget.value),
                  )}
                onblur={() => (touched = { ...touched, [key]: true })}
              />
              {#if touched[key] && validation.errors[key]}<small
                  class="field-error"
                  id={`${key}-error`}>{validation.errors[key]}</small
                >
              {:else if key === "preheat_ms"}<small class="caption"
                  >Из инструкции: {limits.preheat_min_ms /
                    1000}–{limits.preheat_max_ms / 1000} с</small
                >
              {:else if key === "postpurge_ms"}<small class="caption"
                  >Из инструкции: не менее {limits.postpurge_min_ms / 1000} с</small
                >{/if}
            </div>
          {/each}
        </div>
      </section>
    {/each}
    <div>
      {#if draft.dirty && !validation.valid}<p class="caption">
          Проверьте отмеченные значения
        </p>{/if}
      <DraftActions
        {draft}
        {save}
        {reset}
        label="Сохранить параметры"
        enabled={editing && validation.valid}
        busy={Boolean(ui.pending || ui.requestInFlight)}
      />
      {#if !draft.dirty && snapshot.envelope.provisioned}<p class="caption">
          Параметры сохранены
        </p>
        <a class="secondary" href="#/calibration">Перейти к калибровке</a>{/if}
    </div>
    <details class="surface advanced">
      <summary>Паспортные значения</summary>
      <div class="disclosure-body">
        <dl class="reading-list">
          <div>
            <dt>Тепловая мощность</dt>
            <dd>17,4 / 14,0 кВт</dd>
          </div>
          <div>
            <dt>Выхлоп в номинальном режиме</dt>
            <dd>Около 450 °C</dd>
          </div>
          <div>
            <dt>Нагретый воздух при входных 0 °C</dt>
            <dd>Около 120 °C</dd>
          </div>
        </dl>
        <p class="caption">
          Номинальные температуры не задают пороги аварийного отключения
        </p>
      </div>
    </details>
  </div>
{/if}

<style>
  .heater-profile {
    margin-top: 24px;
    max-width: 760px;
  }
  .heater-profile .form-panel {
    max-width: none;
  }
  .parameter-fields {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 24px;
    margin-top: 24px;
  }
  .parameter-field {
    min-width: 0;
  }
  .parameter-field label {
    display: flex;
    justify-content: space-between;
    align-items: flex-end;
    min-height: 44px;
    gap: 8px;
    margin-bottom: 8px;
  }
  .parameter-field label span {
    color: var(--secondary);
    white-space: nowrap;
  }
  .parameter-field small {
    display: block;
    margin-top: 8px;
  }
  .field-error {
    color: var(--danger);
    font-size: 13px;
  }
  @media (max-width: 700px) {
    .parameter-fields {
      grid-template-columns: 1fr;
    }
    .parameter-field label {
      min-height: 0;
    }
  }
</style>
