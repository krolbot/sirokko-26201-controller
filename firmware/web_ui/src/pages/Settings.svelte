<script>
  import { automaticSlew, calibrationText } from "../lib/presentation.js";
  import Icon from "../Icon.svelte";
  let { snapshot, theme, selectTheme } = $props();
</script>

<div class="page-heading"><h1>Настройки</h1></div>
<section class="settings-group">
  <h2>Контроллер</h2>
  <div class="grouped-list surface">
    <a class="list-row" href="#/wifi"
      ><span class="row-icon"><Icon name="wifi" /></span><span class="row-title"
        >Wi-Fi</span
      ><span class="row-value"
        >{snapshot.wifi.password_required ? "С паролем" : "Без пароля"}</span
      ><Icon name="chevron" size={16} /></a
    >
    <label class="list-row theme-row">
      <span class="row-icon"><Icon name="moon" /></span>
      <span class="row-title">Тема</span>
      <select
        value={theme}
        onchange={(event) => selectTheme(event.currentTarget.value)}
      >
        <option value="system">Как в системе</option>
        <option value="light">Светлая</option>
        <option value="dark">Тёмная</option>
      </select>
    </label>
    <a class="list-row" href="#/update"
      ><span class="row-icon"><Icon name="update" /></span><span
        class="row-title">Обновление</span
      ><span class="row-value">{snapshot.version}</span><Icon
        name="chevron"
        size={16}
      /></a
    >
  </div>
</section>
<section class="settings-group">
  <h2>Отопитель</h2>
  <div class="grouped-list surface">
    <a class="list-row" href="#/heater"
      ><span class="row-icon"><Icon name="tune" /></span>
      <span class="row-title">Параметры отопителя</span><span class="row-value"
        >{snapshot.envelope?.provisioned ? "Заданы" : "Не настроены"}</span
      ><Icon name="chevron" size={16} /></a
    >
    <a class="list-row" href="#/debug"
      ><span class="row-icon"><Icon name="tune" /></span><span class="row-title"
        >Режим отладки</span
      ><span class="row-value"
        >{snapshot.state.code === "DEBUG" ? "Включён" : ""}</span
      ><Icon name="chevron" size={16} /></a
    >
    <a class="list-row" href="#/calibration"
      ><span class="row-icon"><Icon name="heat" /></span><span class="row-title"
        >Калибровка</span
      ><span class="row-value">{calibrationText(snapshot)}</span><Icon
        name="chevron"
        size={16}
      /></a
    >
  </div>
</section>
<details class="settings-advanced surface">
  <summary>Дополнительно<Icon name="chevron" size={16} /></summary>
  <a class="list-row" href="#/regulation">
    <span class="row-icon"><Icon name="tune" /></span>
    <span class="row-title">Плавность</span>
    <span class="row-value"
      >{automaticSlew(snapshot.settings)
        ? "Автоматически"
        : "Ручное ограничение"}</span
    >
    <Icon name="chevron" size={16} />
  </a>
</details>
<p class="device-caption">Sirokko 262.01</p>
