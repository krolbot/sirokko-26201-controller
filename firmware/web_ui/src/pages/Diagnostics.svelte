<script>
  import { stateText, compactTemperature } from "../lib/presentation.js";
  import { heaterSetupText } from "../lib/heater.js";
  import { formatDuration } from "../lib/protocol.js";
  import { combustionText } from "../lib/history.js";
  import History from "../components/History.svelte";
  import Measurements from "../components/Measurements.svelte";
  import Icon from "../Icon.svelte";
  let {
    snapshot,
    api,
    online,
    recording,
    startRecording,
    stopRecording,
    downloadMeasurements,
    addMeasurementNote,
  } = $props();
  const localInitialized = $derived(
    snapshot.hardware?.local_controls_initialized,
  );
</script>

<div class="page-heading">
  <h1>Состояние</h1>
  <span class="state-label">{stateText(snapshot)}</span>
</div>
{#if snapshot.faults}<section class="surface info-panel section">
    <h2>Причины аварии</h2>
    {#each snapshot.fault_messages || [] as message}<p class="caption">
        {message}
      </p>{/each}
  </section>{/if}
<div class="readings">
  {#each [["room", "Помещение"], ["air", "Нагретый воздух"], ["exhaust", "Выхлоп"]] as [id, label]}<section
      class="reading surface"
    >
      <h2>{label}</h2>
      <strong>{compactTemperature(snapshot.sensors[id])}</strong><small
        >{!snapshot.sensors[id]?.valid || snapshot.sensors[id]?.status
          ? "Нет показаний"
          : !snapshot.sensors[id]?.fresh
            ? "Данные устарели"
            : ""}</small
      >
    </section>{/each}
</div>
{#if combustionText(snapshot.combustion?.phase)}<p class="caption">
    {combustionText(snapshot.combustion.phase)}
  </p>{/if}
<section class="settings-group">
  <h2>Настройка отопителя</h2>
  <div class="grouped-list surface">
    <a class="list-row" href="#/heater"
      ><span class="row-title">Параметры отопителя</span>
      <span class="row-value">{heaterSetupText(snapshot.envelope)}</span><Icon
        name="chevron"
        size={16}
      /></a
    >
  </div>
</section>
<section class="surface info-panel section">
  <h2>Команды на выходах</h2>
  <dl class="reading-list">
    {#each [["fan_request", "Двигатель", "Включён", "Выключен"], ["glow", "Свеча", "Включена", "Выключена"], ["fuel", "Муфта топливного насоса", "Включена", "Выключена"]] as [id, label, on, off]}<div
      >
        <dt>{label}</dt>
        <dd>{snapshot.outputs[id] ? on : off}</dd>
      </div>{/each}
    <div>
      <dt>ШИМ двигателя</dt>
      <dd>
        {Number(snapshot.outputs.speed_code).toLocaleString("ru-RU", {
          maximumFractionDigits: 1,
        })}
      </dd>
    </div>
  </dl>
</section>
<details class="surface advanced section">
  <summary>Диагностика контроллера</summary>
  <div class="disclosure-body">
    <dl class="reading-list">
      <div>
        <dt>Драйверы экрана и кнопки</dt>
        <dd>
          {localInitialized === true
            ? "Запущены"
            : localInitialized === false
              ? "Ошибка запуска"
              : "Нет данных"}
        </dd>
      </div>
      <div>
        <dt>Подключение экрана и кнопки</dt>
        <dd>Не определяется</dd>
      </div>
      <div>
        <dt>Память настроек</dt>
        <dd>
          {snapshot.storage_ok ? "Без ошибок" : "Ошибка чтения или записи"}
        </dd>
      </div>
    </dl>
  </div>
</details>
<Measurements
  {recording}
  {online}
  start={startRecording}
  stop={stopRecording}
  download={downloadMeasurements}
  addNote={addMeasurementNote}
/>
<History {snapshot} {api} {online} />
<section class="section surface info-panel">
  <h2>Журнал</h2>
  <p class="caption">Время от включения контроллера</p>
  {#if snapshot.events?.length}<ol class="events">
      {#each [...snapshot.events].reverse() as event}<li>
          <time>{formatDuration(event.at_ms)}</time><span
            >{event.kind_ru || "Неизвестное событие"}<small
              >{event.state_ru || ""}</small
            ></span
          >
        </li>{/each}
    </ol>{:else}<p class="empty">Событий пока нет</p>{/if}
</section>
<p class="device-caption">
  Контроллер включён {formatDuration(snapshot.uptime_ms)}
</p>
