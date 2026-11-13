<script>
  import { connectionReady, errorText } from "../lib/presentation.js";
  let { ui, session, notify } = $props();
  let password = $state("");
  let confirmation = $state("");
  let saving = $state(false);
  const snapshot = $derived(ui.snapshot);
  const allowed = $derived(
    connectionReady(ui) && snapshot.wifi.change_allowed === true && !saving,
  );
  async function save(remove = false) {
    if (!allowed) return;
    const next = remove ? "" : password;
    if (
      !remove &&
      (!/^[\x21-\x7e]{8,63}$/.test(next) || next !== confirmation)
    ) {
      notify(
        "error",
        next !== confirmation
          ? "Пароли не совпадают"
          : "Введите 8–63 латинских букв, цифр или символов без пробелов",
      );
      return;
    }
    saving = true;
    try {
      await session.api.saveWifi(next);
      password = confirmation = "";
      await session.poll();
      notify("ok", "Пароль сохранён. Изменение применится после перезапуска");
    } catch (error) {
      notify("error", errorText(error, "Не удалось сохранить пароль"));
    } finally {
      saving = false;
    }
  }
</script>

<div class="page-heading"><h1>Wi-Fi</h1></div>
<p class="intro">
  {snapshot.wifi.password_required
    ? "Подключение с паролем"
    : "Подключение без пароля"}
</p>
{#if snapshot.wifi.restart_required}<div class="notice">
    После перезапуска: {snapshot.wifi.next_boot_password_required
      ? "подключение с новым паролем"
      : "подключение без пароля"}
  </div>{/if}
{#if snapshot.wifi.change_allowed !== true}<div class="notice" role="status">
    {snapshot.wifi.change_reason || "Смена пароля сейчас недоступна"}
  </div>{/if}
<form
  class="form-column surface form-panel"
  onsubmit={(event) => {
    event.preventDefault();
    save();
  }}
>
  <label
    >Новый пароль<input
      type="password"
      autocomplete="new-password"
      minlength="8"
      maxlength="63"
      bind:value={password}
      disabled={!allowed}
    /></label
  >
  <label
    >Повторите пароль<input
      type="password"
      autocomplete="new-password"
      minlength="8"
      maxlength="63"
      bind:value={confirmation}
      disabled={!allowed}
    /></label
  >
  <p class="field-hint">
    8–63 символа без пробелов. Один пароль для Wi-Fi и входа в пульт
  </p>
  <div class="form-actions">
    <button class="primary" type="submit" disabled={!allowed || !password}
      >{saving ? "Сохранение…" : "Сохранить пароль"}</button
    >
    {#if snapshot.wifi.next_boot_password_required}<button
        class="secondary"
        type="button"
        disabled={!allowed}
        onclick={() => save(true)}>Убрать пароль</button
      >{/if}
  </div>
</form>
