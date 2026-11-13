<script>
  import Icon from "../Icon.svelte";
  import {
    commandAllowed,
    commandReason,
    connectionReady,
  } from "../lib/presentation.js";
  let { ui, session, notify } = $props();
  let file = $state(null);
  const allowed = $derived(
    connectionReady(ui) && commandAllowed(ui.snapshot, "OTA"),
  );
  const phase = $derived(ui.ota?.phase);
  function choose(event) {
    file = event.currentTarget.files?.[0] || null;
    if (file && !file.name.toLowerCase().endsWith(".sota")) {
      file = null;
      event.currentTarget.value = "";
      notify("error", "Выберите файл прошивки .sota");
    }
  }
</script>

<div class="page-heading"><h1>Обновление</h1></div>
<p class="intro">Версия {ui.snapshot.version}</p>
<section class="surface form-panel">
  {#if allowed}<label class="file-picker"
      ><Icon name="update" size={28} /><span
        >{file ? file.name : "Выбрать прошивку"}<small
          >{file ? `${Math.ceil(file.size / 1024)} КБ` : "Файл .sota"}</small
        ></span
      ><input
        aria-label="Файл прошивки"
        type="file"
        accept=".sota,application/octet-stream"
        onchange={choose}
      /></label
    >
    {#if file}<button
        class="primary full-width section"
        onclick={() => session.uploadOta(file)}>Установить обновление</button
      >{/if}
  {:else if ui.requestInFlight === "OTA"}<p class="activity" role="status">
      Загружаем прошивку…
    </p>
  {:else if !["activation_queued", "checking_startup"].includes(phase)}<p
      class="caption"
    >
      {commandReason(ui.snapshot, "OTA")}
    </p>{/if}
  {#if ui.snapshot.state.code === "MAINTENANCE" || ui.requestInFlight === "OTA"}<button
      class="secondary section"
      disabled={ui.requestInFlight === "STOP"}
      onclick={() => session.stop()}>Отменить обновление</button
    >{/if}
  {#if phase === "activation_queued"}<p class="activity" role="status">
      Перезапуск контроллера…
    </p>
  {:else if phase === "checking_startup"}<p class="activity" role="status">
      Проверяем новую прошивку…
    </p>
  {:else if phase === "running_expected_version"}<p
      class="activity"
      role="status"
    >
      Версия {ui.ota.observedVersion} установлена
    </p>
  {:else if phase === "different_version"}<p class="activity" role="alert">
      Запущена версия {ui.ota.observedVersion}, ожидалась {ui.ota
        .expectedVersion}
    </p>
  {:else if phase === "confirmation_timeout"}<p class="activity" role="alert">
      Нет связи после обновления
    </p>{/if}
</section>
