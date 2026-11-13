export const heaterGroups = [
  {
    title: "Время этапов",
    fields: [
      ["preheat_ms", "Накал свечи до подачи топлива", "с", 1000],
      ["ignition_timeout_ms", "Предельное время розжига", "с", 1000],
      ["postpurge_ms", "Минимальное время охлаждения", "с", 1000],
    ],
  },
  {
    title: "Температура выхлопа",
    fields: [
      [
        "minimum_run_exhaust_milli_c",
        "Порог горения по температуре выхлопа",
        "°C",
        1000,
      ],
      [
        "maximum_exhaust_milli_c",
        "Отключить топливо при выхлопе ≥",
        "°C",
        1000,
      ],
      [
        "cooling_exhaust_milli_c",
        "Выключить двигатель при выхлопе <",
        "°C",
        1000,
      ],
    ],
  },
  {
    title: "Двигатель",
    fields: [
      ["reference_speed_code", "ШИМ двигателя при розжиге", "ШИМ", 1],
      ["purge_speed_code", "ШИМ двигателя при охлаждении", "ШИМ", 1],
      ["exploration_floor_code", "Нижняя граница ШИМ при калибровке", "ШИМ", 1],
    ],
  },
];
export const heaterFields = heaterGroups.flatMap((group) => group.fields);

export function missingHeaterFields(envelope) {
  return heaterFields.filter(([key]) => !(envelope?.parameters?.[key] > 0));
}

export function heaterSetupText(envelope) {
  if (envelope?.provisioned) return "Заполнены";
  const count = missingHeaterFields(envelope).length;
  return count ? `Не заданы: ${count}` : "Проверьте параметры";
}

export function heaterDraft(envelope) {
  return Object.fromEntries(
    heaterFields.map(([key, , , scale]) => [
      key,
      envelope?.parameters?.[key] > 0 ? envelope.parameters[key] / scale : "",
    ]),
  );
}

export function heaterValidation(value, limits) {
  const errors = {},
    parameters = {};
  for (const [key, , unit, scale] of heaterFields) {
    const number = value?.[key];
    if (number === "" || number == null) {
      parameters[key] = 0;
      continue;
    }
    const raw = number * scale;
    const max = key.endsWith("_ms")
      ? limits?.timer_max_ms
      : scale === 1
        ? limits?.pwm_max
        : limits?.temperature_max_milli_c;
    if (
      !Number.isFinite(number) ||
      raw <= 0 ||
      !Number.isFinite(max) ||
      raw > max ||
      Math.abs(raw - Math.round(raw)) > 0.000001
    ) {
      errors[key] = max
        ? `Допустимо от ${1 / scale} до ${max / scale} ${unit}`
        : "Не получены ограничения контроллера";
      continue;
    }
    parameters[key] = Math.round(raw);
  }
  const p = parameters;
  for (const key of [
    "cooling_exhaust_milli_c",
    "maximum_exhaust_milli_c",
    "minimum_run_exhaust_milli_c",
  ])
    if (p[key] >= limits?.temperature_max_milli_c)
      errors[key] =
        `Ниже ${limits.temperature_max_milli_c / 1000} °C — предела измерения термопары`;
  if (!p.cooling_exhaust_milli_c)
    errors.cooling_exhaust_milli_c =
      "Укажите температуру выхлопа для отключения двигателя";
  if (
    p.preheat_ms &&
    (p.preheat_ms < limits.preheat_min_ms ||
      p.preheat_ms > limits.preheat_max_ms)
  )
    errors.preheat_ms = `От ${limits.preheat_min_ms / 1000} до ${limits.preheat_max_ms / 1000} с`;
  if (p.postpurge_ms && p.postpurge_ms < limits.postpurge_min_ms)
    errors.postpurge_ms = `Не менее ${limits.postpurge_min_ms / 1000} с`;
  if (
    p.ignition_timeout_ms &&
    (p.ignition_timeout_ms <= limits?.temperature_confirmation_ms ||
      p.ignition_timeout_ms > limits?.firing_max_ms)
  )
    errors.ignition_timeout_ms = `Больше ${limits.temperature_confirmation_ms / 1000} и не более ${limits.firing_max_ms / 1000} с`;
  if (
    p.minimum_run_exhaust_milli_c &&
    p.minimum_run_exhaust_milli_c <= p.cooling_exhaust_milli_c
  )
    errors.minimum_run_exhaust_milli_c =
      "Должен быть выше температуры отключения двигателя";
  for (const [low, high] of [
    ["cooling_exhaust_milli_c", "maximum_exhaust_milli_c"],
    ["minimum_run_exhaust_milli_c", "maximum_exhaust_milli_c"],
  ])
    if (p[low] && p[high] && p[low] >= p[high])
      errors[low] =
        "Значение должно быть ниже температуры отключения топлива при нагреве выхлопа";
  if (
    p.exploration_floor_code &&
    p.reference_speed_code &&
    p.reference_speed_code - p.exploration_floor_code < 2
  )
    errors.exploration_floor_code = "Минимум на 2 единицы ниже ШИМ при розжиге";
  return {
    parameters,
    errors,
    valid: Object.keys(errors).length === 0,
    complete: missingHeaterFields({ parameters }).length === 0,
  };
}
