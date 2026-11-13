<script>
  import { defaultSlew } from "../lib/presentation.js";
  import { heaterSetupText } from "../lib/heater.js";
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
    <a class="list-row" href="#/trial"
      ><span class="row-icon"><Icon name="thermometer" /></span><span
        class="row-title">Первый запуск</span
      ><Icon name="chevron" size={16} /></a
    >
    <a class="list-row" href="#/heater"
      ><span class="row-icon"><Icon name="tune" /></span>
      <span class="row-title">Параметры отопителя</span><span class="row-value"
        >{heaterSetupText(snapshot.envelope)}</span
      ><Icon name="chevron" size={16} /></a
    >
  </div>
</section>
<details class="settings-advanced surface">
  <summary>Дополнительно<Icon name="chevron" size={16} /></summary>
  <a class="list-row" href="#/regulation">
    <span class="row-icon"><Icon name="tune" /></span>
    <span class="row-title">Скорость изменения ШИМ</span>
    <span class="row-value"
      >{defaultSlew(snapshot.settings)
        ? "Исходный темп"
        : "Замедлено вручную"}</span
    >
    <Icon name="chevron" size={16} />
  </a>
</details>
<p class="device-caption">Sirokko 262.01</p>
