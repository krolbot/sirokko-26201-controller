<script>
  import Icon from "../Icon.svelte";
  import { compactTemperature } from "../lib/presentation.js";
  let {
    value,
    min,
    max,
    step = 1,
    manual = false,
    sensor,
    heating = false,
    disabled = false,
    change,
  } = $props();
  const radius = 114,
    cx = 160,
    cy = 136;
  let dragging = $state(false);
  const fraction = $derived(
    max > min ? Math.max(0, Math.min(1, (value - min) / (max - min))) : 1,
  );
  const measured = $derived(
    !manual && max > min && sensor?.valid && !sensor.status && sensor.fresh
      ? Math.max(0, Math.min(1, (sensor.milli_c / 1000 - min) / (max - min)))
      : null,
  );
  function point(f) {
    const a = ((135 + 270 * f) * Math.PI) / 180;
    return { x: cx + radius * Math.cos(a), y: cy + radius * Math.sin(a) };
  }
  function arc(from, to) {
    const a = point(from),
      b = point(to);
    return `M ${a.x} ${a.y} A ${radius} ${radius} 0 ${to - from > 2 / 3 ? 1 : 0} 1 ${b.x} ${b.y}`;
  }
  function set(next) {
    change(Math.max(min, Math.min(max, Math.round(next / step) * step)));
  }
  function move(event) {
    if (!dragging || disabled) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const x = ((event.clientX - rect.left) * 320) / rect.width - cx;
    const y = ((event.clientY - rect.top) * 280) / rect.height - cy;
    let a = ((Math.atan2(y, x) * 180) / Math.PI - 135 + 360) % 360;
    if (a > 270) a = a < 315 ? 270 : 0;
    set(min + (a / 270) * (max - min));
  }
  function down(event) {
    if (disabled) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const x = ((event.clientX - rect.left) * 320) / rect.width - cx,
      y = ((event.clientY - rect.top) * 280) / rect.height - cy;
    if (Math.abs(Math.hypot(x, y) - radius) > 24) return;
    dragging = true;
    event.currentTarget.setPointerCapture(event.pointerId);
    move(event);
  }
  function key(event) {
    if (disabled) return;
    const delta = {
      ArrowUp: step,
      ArrowRight: step,
      ArrowDown: -step,
      ArrowLeft: -step,
    };
    if (event.key in delta) {
      event.preventDefault();
      set(value + delta[event.key]);
    } else if (event.key === "Home" || event.key === "End") {
      event.preventDefault();
      set(event.key === "Home" ? min : max);
    }
  }
</script>

<div class="dial" class:dragging>
  <svg
    viewBox="0 0 320 280"
    role="slider"
    tabindex={disabled ? -1 : 0}
    aria-label={manual ? "Уровень двигателя" : "Заданная температура"}
    aria-valuemin={min}
    aria-valuemax={max}
    aria-valuenow={value}
    aria-valuetext={`${value} ${manual ? "процентов" : "градусов"}`}
    aria-disabled={disabled}
    onkeydown={key}
    onpointerdown={down}
    onpointermove={move}
    onpointerup={() => (dragging = false)}
    onpointercancel={() => (dragging = false)}
    onlostpointercapture={() => (dragging = false)}
  >
    <path class="dial-track" d={arc(0, 1)} />
    {#if fraction > 0}<path class="dial-selection" d={arc(0, fraction)} />{/if}
    {#if heating && measured !== null && measured < fraction}<path
        class="dial-heating"
        d={arc(measured, fraction)}
      />{/if}
    {#if measured !== null}<circle
        cx={point(measured).x}
        cy={point(measured).y}
        r="3"
        class="dial-measured"
      />{/if}
    <circle
      cx={point(fraction).x}
      cy={point(fraction).y}
      r="11"
      class="dial-marker"
    />
    <path class="dial-hit" d={arc(0, 1)} />
  </svg>
  <div class="dial-content">
    <p class="eyebrow">{manual ? "Уровень двигателя" : "Задано"}</p>
    <div class="setpoint-value">
      <strong>{value}</strong><span>{manual ? "%" : "°C"}</span>
    </div>
    <p class="room-reading">
      <Icon name="thermometer" size={17} /><span
        >{compactTemperature(sensor)}</span
      >
    </p>
  </div>
  <div class="dial-steps">
    <button
      class="step"
      aria-label="Уменьшить"
      disabled={disabled || value <= min}
      onclick={() => set(value - step)}>−</button
    ><button
      class="step"
      aria-label="Увеличить"
      disabled={disabled || value >= max}
      onclick={() => set(value + step)}>+</button
    >
  </div>
</div>
