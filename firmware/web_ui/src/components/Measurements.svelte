<script>
  import { formatDuration } from "../lib/protocol.js";
  let {
    recording,
    online,
    start,
    stop,
    download,
    addNote,
    allowStart = true,
    stopDisabled = false,
  } = $props();
  let note = $state("");
  let added = $state(false);
  const status = $derived(
    recording.active
      ? recording.online
        ? "Идёт запись"
        : "Ожидаем связь"
      : {
          restart: "Запись завершена: контроллер перезапущен",
          limit: "Лимит записи достигнут",
          stopped: "Запись завершена",
        }[recording.reason] ||
          "Температуры и управление отопителем в одном файле",
  );
  function submit(event) {
    event.preventDefault();
    if (addNote(note)) {
      note = "";
      added = true;
    }
  }
  function begin() {
    note = "";
    added = false;
    start();
  }
</script>

<section class="surface info-panel section" aria-label="Запись измерений">
  <h2>Запись измерений</h2>
  <p class="caption" role="status">{status}</p>
  {#if recording.samples}
    <p class="recording-time">
      {formatDuration(recording.elapsedMs)}
      <span>· Измерений: {recording.samples.toLocaleString("ru-RU")}</span>
    </p>
  {/if}
  <div class="form-actions">
    {#if recording.active}
      {#if !stopDisabled}<button class="primary" onclick={stop}
          >Завершить запись</button
        >{/if}
    {:else if allowStart}
      <button
        class="primary"
        onclick={begin}
        disabled={!online || recording.unsaved}
      >
        {recording.samples ? "Новая запись" : "Начать запись"}
      </button>
    {/if}
    {#if recording.samples}
      <button class="secondary" onclick={download}>Скачать CSV</button>
    {/if}
  </div>
  {#if recording.active}
    <form onsubmit={submit}>
      <label for="measurement-note">Отметка в записи</label>
      <div class="note-controls">
        <input
          id="measurement-note"
          bind:value={note}
          maxlength="160"
          oninput={() => (added = false)}
          placeholder="Например, напряжение 24 В"
        />
        <button class="secondary" disabled={!note.trim()}>Добавить</button>
      </div>
      {#if added}<p class="caption" role="status">Отметка добавлена</p>{/if}
    </form>
  {/if}
  <p class="caption">
    {recording.active
      ? "Держите страницу открытой и экран включённым до завершения записи"
      : !allowStart
        ? "CSV можно скачать для себя. При закрытии страницы запись будет удалена."
        : recording.unsaved
          ? "Скачайте файл перед новой записью или закрытием страницы"
          : "Данные сохраняются только в браузере"}
  </p>
</section>

<style>
  .recording-time {
    font-variant-numeric: tabular-nums;
    margin-top: 12px;
  }
  .recording-time span {
    font-size: 13px;
    color: var(--secondary);
  }
  form {
    margin-top: 24px;
  }
  .note-controls {
    display: flex;
    gap: 8px;
    margin-top: 8px;
  }
  .note-controls button {
    flex-shrink: 0;
  }
  .form-actions {
    flex-wrap: wrap;
  }
  .form-actions button {
    flex-basis: 140px;
  }
</style>
