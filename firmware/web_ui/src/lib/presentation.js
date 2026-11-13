import { MAXIMUM_SLEW_PER_S, trialActive } from "./protocol.js";
export function commandAllowed(snapshot, action) {
  return snapshot?.commands?.[action]?.allowed === true;
}
export function commandReason(snapshot, action) {
  return (
    snapshot?.commands?.[action]?.reason ||
    "Контроллер не разрешает эту команду"
  );
}
export function connectionReady(ui) {
  return (
    ui.auth === "authenticated" &&
    ui.link === "online" &&
    !ui.pending &&
    !ui.requestInFlight &&
    !["activation_queued", "checking_startup"].includes(ui.ota?.phase)
  );
}
export function needsStop(snapshot) {
  return (
    trialActive(snapshot) ||
    snapshot?.outputs?.fuel === true ||
    snapshot?.outputs?.glow === true ||
    ["TRIAL_READY", "PREHEAT", "IGNITION", "RUN"].includes(
      snapshot?.state?.code,
    )
  );
}
export function stateText(snapshot) {
  return snapshot?.state?.ru || "Нет состояния отопителя";
}
export function temperature(sensor) {
  if (!sensor?.valid || sensor.status) return "Нет показаний";
  if (!sensor.fresh) return "Данные устарели";
  return `${(sensor.milli_c / 1000).toLocaleString("ru-RU", { maximumFractionDigits: 1 })} °C`;
}
export const routes = {
  control: { title: "Отопление", section: "control" },
  timer: { title: "Таймер", section: "timer" },
  diagnostics: { title: "Состояние", section: "diagnostics" },
  settings: { title: "Настройки", section: "settings" },
  wifi: { title: "Wi-Fi", section: "settings" },
  regulation: { title: "Скорость изменения ШИМ", section: "settings" },
  heater: { title: "Параметры отопителя", section: "settings" },
  trial: { title: "Первый запуск", section: "settings" },
  update: { title: "Обновление", section: "settings" },
};
export function routeFromHash(hash) {
  const name = hash.replace(/^#\/?/, "");
  return Object.hasOwn(routes, name) ? name : "control";
}

export function blockedActionLink(snapshot, action) {
  switch (snapshot?.commands?.[action]?.code) {
    case "ENVELOPE_MISSING":
    case "ENVELOPE_INVALID":
      return action === "START" && snapshot?.commands?.TRIAL_START
        ? { href: "#/trial", label: "Первый запуск и измерения" }
        : { href: "#/heater", label: "Настроить отопитель" };
    case "SENSOR_INVALID":
    case "SENSOR_STALE":
    case "INITIAL_THERMAL_STATE":
      return { href: "#/diagnostics", label: "Показания датчиков" };
    case "STORAGE_UNAVAILABLE":
    case "FAULT_LATCHED":
    case "LOCAL_CONTROLS_UNAVAILABLE":
      return { href: "#/diagnostics", label: "Посмотреть состояние" };
    default:
      return null;
  }
}

export function errorText(error, fallback, snapshot) {
  if (error?.kind === "auth" || error?.status === 401)
    return "Сеанс истёк. Войдите снова";
  if (error?.kind === "network") return `${fallback}: нет связи с контроллером`;
  if (error?.kind === "timeout") return `${fallback}: контроллер не ответил`;
  const code = error?.code;
  const blocked = Object.values(snapshot?.commands || {}).find(
    (command) =>
      command.allowed === false && command.code === code && command.reason,
  );
  if (blocked) return blocked.reason;
  switch (code) {
    case "WIFI_BUSY":
      return "Смена пароля доступна после остановки и охлаждения отопителя";
    case "STORAGE":
      return "Не удалось сохранить данные в памяти контроллера";
    case "BUSY":
      return "Команда недоступна на текущем этапе работы";
    case "RATE_LIMIT":
    case "SESSIONS_FULL":
      return "Контроллер занят. Повторите позже";
    case "CHALLENGE":
    case "GRANT":
    case "REPLAY":
    case "NOT_ACCEPTED":
      return "Состояние изменилось. Повторите действие";
    case "PASSWORD_LENGTH_OR_CHARACTERS":
      return "Пароль должен содержать 8–63 латинских букв, цифр или символов без пробелов";
    case "VALUE":
      return "Проверьте введённые значения";
    case "NOT_SAFE":
    case "MAINTENANCE_LEASE":
      return "Обновление недоступно в текущем состоянии отопителя";
    case "OTA_TIMEOUT":
    case "TRUNCATED":
      return "Файл прошивки передан не полностью";
    case "TOO_LARGE":
      return "Файл прошивки слишком большой";
    case "PROJECT":
    case "TARGET":
      return "Прошивка предназначена для другого устройства";
    case "SIGNATURE":
    case "KEY":
      return "Не удалось подтвердить подлинность прошивки";
    case "MAGIC":
    case "FORMAT":
    case "SIZE":
    case "DIGEST":
    case "IMAGE":
      return "Файл прошивки повреждён или имеет неподходящий формат";
    default:
      return fallback;
  }
}

export function compactTemperature(sensor) {
  return sensor?.valid && !sensor.status && sensor.fresh
    ? temperature(sensor)
    : "—";
}
export function timerLabel(milliseconds) {
  if (!milliseconds) return "Без таймера";
  const minutes = Math.ceil(milliseconds / 60000);
  const hours = Math.floor(minutes / 60);
  return [hours ? `${hours} ч` : "", minutes % 60 ? `${minutes % 60} мин` : ""]
    .filter(Boolean)
    .join(" ");
}

export function defaultSlew(settings) {
  return (
    settings?.slew_up_per_s === MAXIMUM_SLEW_PER_S &&
    settings?.slew_down_per_s === MAXIMUM_SLEW_PER_S
  );
}
export function countdownText(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds < 0) return "—";
  const seconds = Math.ceil(milliseconds / 1000);
  return [
    Math.floor(seconds / 3600),
    Math.floor(seconds / 60) % 60,
    seconds % 60,
  ]
    .map((value) => String(value).padStart(2, "0"))
    .join(":");
}
