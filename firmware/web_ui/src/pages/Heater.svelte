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
<p class="caption">Можно сохранять по мере заполнения.</p>
<a class="secondary" href="#/trial">Первый запуск и измерения</a>
{#if !commandAllowed(snapshot, "PROFILE")}<p class="notice" role="status">
    {commandReason(snapshot, "PROFILE")}
  </p>{/if}
{#if draft.value && limits}
  <div class="form-column heater-profile">
    {#each heaterGroups as group}
      <section class="surface form-panel">
        <h2>{group.title}</h2>
        {#if group.title === "Двигатель"}<p class="caption">
            ШИМ задаёт управление двигателем, который приводит вентилятор и
            топливный насос. Значения от 1 до {limits.pwm_max}; это не обороты в
            минуту.
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
                placeholder={key === "maximum_exhaust_milli_c"
                  ? "Необязательно"
                  : "Укажите значение"}
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
                  >Двигатель и подача топлива включатся после накала. Свеча
                  продолжит работать ещё {limits.ignition_glow_ms / 1000} с.</small
                >
              {:else if key === "postpurge_ms"}<small class="caption"
                  >После отключения топлива двигатель работает не меньше этого
                  времени. Если выхлоп ещё горячий, охлаждение продолжается.</small
                >
              {:else if key === "ignition_timeout_ms"}<small class="caption"
                  >От подачи топлива до подтверждения розжига по температуре
                  выхлопа. Если время истечёт, топливо отключится и начнётся
                  охлаждение.</small
                >
              {:else if key === "minimum_run_exhaust_milli_c"}<small
                  class="caption"
                  >При розжиге, после выключения свечи, выхлоп должен достичь
                  этого значения и держаться не ниже него {limits.temperature_confirmation_ms /
                    1000} с. При отоплении падение ниже значения на {limits.temperature_confirmation_ms /
                    1000} с отключает топливо и запускает охлаждение.</small
                >
              {:else if key === "maximum_exhaust_milli_c"}<small class="caption"
                  >При достижении этой температуры топливо отключится и начнётся
                  охлаждение. Можно оставить пустым. Штатная защита муфты от
                  перегрева должна оставаться подключённой.</small
                >
              {:else if key === "cooling_exhaust_milli_c"}<small class="caption"
                  >Двигатель выключится, когда истечёт минимальное время
                  охлаждения и выхлоп остынет ниже этого значения.</small
                >
              {:else if key === "maximum_speed_code"}<small class="caption"
                  >Используется при розжиге. При отоплении ШИМ также не
                  поднимается выше этого значения.</small
                >
              {:else if key === "minimum_speed_code"}<small class="caption"
                  >При отоплении ШИМ не опускается ниже этого значения. Укажите
                  код ШИМ, проверенный при устойчивом горении в первом запуске.</small
                >
              {:else if key === "purge_speed_code"}<small class="caption"
                  >ШИМ двигателя после отключения топлива, пока отопитель
                  остывает.</small
                >
              {/if}
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
        <a class="secondary" href="#/control">Перейти к отоплению</a>{/if}
    </div>
    <details class="surface advanced">
      <summary>Паспортные значения</summary>
      <div class="disclosure-body">
        <dl class="reading-list">
          <div>
            <dt>Мощность штатных ступеней</dt>
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
