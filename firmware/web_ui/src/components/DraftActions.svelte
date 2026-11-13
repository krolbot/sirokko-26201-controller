<script>
  let {
    draft,
    enabled,
    save,
    reset,
    busy = false,
    label = "Сохранить",
  } = $props();
</script>

{#if draft.conflict}
  <div class="notice" role="status">
    Настройки изменились на контроллере<button
      class="text-button"
      onclick={reset}>Загрузить текущие</button
    >
  </div>
{:else if draft.dirty}
  <div class="form-actions">
    <button class="primary" disabled={!enabled || busy} onclick={save}
      >{label}</button
    ><button class="secondary" disabled={busy} onclick={reset}
      >Отменить изменения</button
    >
  </div>
{/if}
