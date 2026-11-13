import { columns } from "./measurements.js";

const index = Object.fromEntries(columns.map((name, i) => [name, i]));
const get = (row, key) => row[index[key]];
const temperature = (row) => get(row, "exhaust_c");
const run = (row) =>
  get(row, "state") === "RUN" &&
  get(row, "trial_active") &&
  get(row, "fuel_command") &&
  !get(row, "glow_command");

// Only a proposal for this observed run, never evidence of flame detection.
// Two minutes with <= 5 C spread reject a still-warming/cooling PWM point.
const steadyMs = 120000;
const steadySpreadC = 5;

export function trialSuggestion(recording, snapshot) {
  const unavailable = (reason) => ({ ready: false, reason });
  const pwm = recording.selectedPwm;
  if (!pwm) return unavailable("Выберите нижний ШИМ после проверки горения.");
  if (snapshot?.boot_id !== recording.bootId)
    return unavailable(
      "Контроллер перезапущен. Нужна новая запись первого запуска.",
    );
  const rows = recording.rows;
  const samples = rows.filter((row) => get(row, "event") === "sample");
  if (!samples.length) return unavailable("Нет измерений первого запуска.");
  if (
    samples.some(
      (row) =>
        get(row, "profile_fingerprint") !== snapshot.envelope.fingerprint,
    )
  )
    return unavailable("Параметры отопителя изменились. Нужна новая запись.");
  if (
    rows.some((row) =>
      ["disconnected", "sampling_gap", "restart", "limit"].includes(
        get(row, "event"),
      ),
    ) ||
    recording.reason === "limit"
  )
    return unavailable(
      "В записи есть пропуски. Повторите первый запуск с открытой страницей.",
    );
  if (
    samples.some(
      (row) => get(row, "faults") || !Number.isFinite(temperature(row)),
    )
  )
    return unavailable(
      "В записи есть ошибка или недостоверная температура. Повторите запуск после устранения причины.",
    );
  if (
    !Number.isInteger(pwm) ||
    pwm < 1 ||
    pwm > snapshot.envelope.parameters.maximum_speed_code
  )
    return unavailable("Выбранный ШИМ выше значения «ШИМ при розжиге».");

  const firstFuel = samples.findIndex((row) => get(row, "fuel_command"));
  if (firstFuel < 1)
    return unavailable("Не записана температура до подачи топлива.");
  const baseline = Math.max(...samples.slice(0, firstFuel).map(temperature));
  const working = samples.filter(
    (row) => run(row) && get(row, "motor_pwm_code") >= pwm,
  );
  if (!working.length) return unavailable("Нет измерений при выбранном ШИМ.");
  let window = [];
  let steady = false;
  let previousTime = null;
  let previousSensor = null;
  for (const row of samples) {
    const time = get(row, "uptime_ms");
    const sensorTime = get(row, "exhaust_sample_ms");
    if (
      !run(row) ||
      get(row, "motor_pwm_code") !== pwm ||
      (previousTime !== null && time - previousTime > 2500)
    )
      window = [];
    if (
      run(row) &&
      get(row, "motor_pwm_code") === pwm &&
      sensorTime !== previousSensor
    ) {
      steady = false;
      window.push(row);
      while (
        window.length > 1 &&
        time - get(window[1], "uptime_ms") >= steadyMs
      )
        window.shift();
      if (time - get(window[0], "uptime_ms") >= steadyMs) {
        const values = window.map(temperature);
        steady = Math.max(...values) - Math.min(...values) <= steadySpreadC;
      }
    }
    previousTime = time;
    previousSensor = sensorTime;
  }
  if (!steady)
    return unavailable(
      recording.active
        ? "Подержите выбранный ШИМ: нужны 2 минуты с колебаниями выхлопа не более 5 °C."
        : "Не записаны 2 минуты стабильной температуры на выбранном ШИМ. Повторите запуск и дождитесь колебаний не более 5 °C.",
    );

  const parameters = snapshot.envelope.parameters;
  const low = Math.min(...working.map(temperature));
  const reference = Math.max(
    baseline,
    parameters.cooling_exhaust_milli_c / 1000,
  );
  // Midpoint keeps the initial setting clear of both cold/cooling temperatures
  // and the lowest observed confirmed operation, including PWM transitions.
  const proposedC = Math.floor((reference + low) / 2);
  if (
    proposedC <= reference ||
    proposedC >= low ||
    (parameters.maximum_exhaust_milli_c &&
      proposedC * 1000 >= parameters.maximum_exhaust_milli_c)
  )
    return unavailable(
      "Температуры до розжига и при работе слишком близки для предложения.",
    );
  const fuelTime = get(samples[firstFuel], "uptime_ms");
  let reached = null;
  let ignitionVerified = false;
  for (const row of samples.slice(firstFuel)) {
    const time = get(row, "uptime_ms");
    if (time - fuelTime >= parameters.ignition_timeout_ms) break;
    if (
      !get(row, "fuel_command") ||
      get(row, "glow_command") ||
      temperature(row) < proposedC
    )
      reached = null;
    else {
      reached ??= time;
      if (
        time - reached >=
        snapshot.envelope.limits.temperature_confirmation_ms
      )
        ignitionVerified = true;
    }
  }
  if (!ignitionVerified)
    return unavailable(
      "При этом запуске выхлоп не успел подтвердить предложенную температуру за время на розжиг.",
    );
  return {
    ready: true,
    pwm,
    temperatureC: proposedC,
    minimumC: low,
    referenceC: reference,
    complete: !recording.active && recording.reason === "trial_finished",
  };
}
