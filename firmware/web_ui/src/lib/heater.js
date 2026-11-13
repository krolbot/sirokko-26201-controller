export const heaterGroups = [
  {
    title: "Время этапов",
    fields: [
      ["preheat_ms", "Накал свечи", "с", 1000],
      ["prepurge_ms", "Продувка перед розжигом", "с", 1000],
      ["ignition_ms", "Розжиг", "с", 1000],
      ["verify_ms", "Проверка запуска", "с", 1000],
      ["postpurge_ms", "Охлаждение после остановки", "с", 1000],
      ["recovery_purge_ms", "Продувка после обрыва питания", "с", 1000],
    ],
  },
  {
    title: "Температурные пределы",
    fields: [
      ["maximum_exhaust_milli_c", "Отключение по выхлопу", "°C", 1000],
      ["maximum_air_milli_c", "Отключение по нагретому воздуху", "°C", 1000],
      [
        "maximum_purge_exit_exhaust_milli_c",
        "Выхлоп для завершения охлаждения",
        "°C",
        1000,
      ],
      [
        "maximum_purge_exit_air_milli_c",
        "Воздух для завершения охлаждения",
        "°C",
        1000,
      ],
      [
        "minimum_ignition_rise_milli_c",
        "Рост температуры выхлопа при розжиге",
        "°C",
        1000,
      ],
      [
        "minimum_ignition_air_rise_milli_c",
        "Рост температуры воздуха при розжиге",
        "°C",
        1000,
      ],
      [
        "minimum_run_exhaust_milli_c",
        "Остановка при падении выхлопа ниже",
        "°C",
        1000,
      ],
    ],
  },
  {
    title: "Вентилятор",
    fields: [
      ["purge_speed_code", "Команда при продувке", "ШИМ", 1],
      ["reference_speed_code", "Опорная команда калибровки", "ШИМ", 1],
      ["exploration_floor_code", "Нижняя граница исследования", "ШИМ", 1],
    ],
  },
];
export const heaterFields = heaterGroups.flatMap((group) => group.fields);

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
      errors[key] = "Укажите значение";
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
  if (
    p.preheat_ms &&
    (p.preheat_ms < limits.preheat_min_ms ||
      p.preheat_ms > limits.preheat_max_ms)
  )
    errors.preheat_ms = `От ${limits.preheat_min_ms / 1000} до ${limits.preheat_max_ms / 1000} с`;
  if (p.postpurge_ms && p.postpurge_ms < limits.postpurge_min_ms)
    errors.postpurge_ms = `Не менее ${limits.postpurge_min_ms / 1000} с`;
  if (
    p.ignition_ms &&
    p.verify_ms &&
    p.ignition_ms + p.verify_ms > limits.firing_max_ms
  )
    errors.verify_ms = `Розжиг и проверка вместе — не более ${limits.firing_max_ms / 1000} с`;
  for (const [low, high] of [
    ["maximum_purge_exit_exhaust_milli_c", "maximum_exhaust_milli_c"],
    ["maximum_purge_exit_air_milli_c", "maximum_air_milli_c"],
    ["minimum_ignition_rise_milli_c", "maximum_exhaust_milli_c"],
    ["minimum_ignition_air_rise_milli_c", "maximum_air_milli_c"],
    ["minimum_run_exhaust_milli_c", "maximum_exhaust_milli_c"],
  ])
    if (p[low] && p[high] && p[low] >= p[high])
      errors[low] = "Должно быть ниже порога отключения";
  if (
    p.exploration_floor_code &&
    p.reference_speed_code &&
    p.reference_speed_code - p.exploration_floor_code < 2
  )
    errors.exploration_floor_code = "Минимум на 2 единицы ниже опорной команды";
  return { parameters, errors, valid: Object.keys(errors).length === 0 };
}
