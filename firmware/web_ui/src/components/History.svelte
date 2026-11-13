<script>
  import { onMount } from "svelte";
  import { historyPoints, historyPlot } from "../lib/history.js";
  let { snapshot, api, online = true } = $props();
  let points = $state([]),
    channel = $state("exhaust"),
    error = $state(false);
  let bootId = $state(null),
    busy = false,
    mounted = false;
  let chartWidth = $state(320);
  const plot = $derived(historyPlot(points, channel, chartWidth));
  const label = $derived(
    { exhaust: "Выхлоп", air: "Выход воздуха", room: "Помещение" }[channel],
  );
  const minutes = $derived(
    plot ? Math.max(1, Math.round((plot.last - plot.first) / 60000)) : 0,
  );
  async function load() {
    if (!mounted || busy || !online || document.visibilityState === "hidden")
      return;
    busy = true;
    const requestedBoot = snapshot.boot_id;
    try {
      const data = await api.history();
      if (mounted && requestedBoot === snapshot.boot_id) {
        points = historyPoints(data, requestedBoot);
        error = false;
      }
    } catch {
      if (mounted && requestedBoot === snapshot.boot_id) error = true;
    } finally {
      busy = false;
    }
  }
  $effect(() => {
    if (bootId !== snapshot.boot_id) {
      bootId = snapshot.boot_id;
      points = [];
      error = false;
      void load();
    }
  });
  onMount(() => {
    mounted = true;
    void load();
    const timer = setInterval(load, 5000);
    document.addEventListener("visibilitychange", load);
    return () => {
      mounted = false;
      clearInterval(timer);
      document.removeEventListener("visibilitychange", load);
    };
  });
</script>

<section
  class="surface info-panel section history"
  aria-label="История температур"
>
  <div class="history-heading">
    <h2>История</h2>
    {#if plot}<span>{minutes} мин · °C</span>{/if}
  </div>
  <div class="segmented" role="group" aria-label="Датчик на графике">
    {#each [["exhaust", "Выхлоп"], ["air", "Воздух"], ["room", "Помещение"]] as [id, title]}
      <button aria-pressed={channel === id} onclick={() => (channel = id)}
        >{title}</button
      >
    {/each}
  </div>
  <div bind:clientWidth={chartWidth}>
    {#if plot}
      <svg
        class="history-chart"
        viewBox={`0 0 ${chartWidth} 204`}
        role="img"
        aria-label={`${label}: от ${plot.bottom} до ${plot.top} °C за ${minutes} мин`}
      >
        <title>{label}, °C</title>
        {#each [plot.top, (plot.top + plot.bottom) / 2, plot.bottom] as value, index}
          <line
            x1="40"
            x2={chartWidth - 16}
            y1={16 + index * 79}
            y2={16 + index * 79}
            class="grid"
          />
          <text x="32" y={20 + index * 79} text-anchor="end"
            >{Math.round(value)}</text
          >
        {/each}
        {#each plot.faults as x}<line
            x1={x}
            x2={x}
            y1="16"
            y2="174"
            class="fault-mark"><title>Авария</title></line
          >{/each}
        {#each plot.paths as path}<path d={path} />{/each}
        {#if plot.lastPoint}<circle
            cx={plot.lastPoint.x}
            cy={plot.lastPoint.y}
            r="3"
          />{/if}
        <text x="40" y="198">−{minutes} мин</text><text
          x={chartWidth - 16}
          y="198"
          text-anchor="end"
          >{online && !error ? "Последнее" : "Сохранённое"}</text
        >
      </svg>
    {:else}<p class="empty">
        {error ? "История временно недоступна" : "Ожидаем измерения"}
      </p>{/if}
  </div>
  {#if error && plot}<p class="caption">История пока не обновляется</p>{/if}
</section>

<style>
  .history-heading {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 16px;
    margin-bottom: 16px;
  }
  .history-heading h2 {
    margin: 0;
  }
  .history-heading span {
    font-size: 13px;
    color: var(--secondary);
    white-space: nowrap;
  }
  .history .segmented {
    margin-bottom: 16px;
  }
  .history .segmented button {
    min-width: 0;
    font-size: 13px;
    padding: 10px 4px;
    min-height: 44px;
  }
  .history-chart {
    display: block;
    width: 100%;
    overflow: visible;
  }
  .history-chart text {
    font-family: inherit;
    font-size: 12px;
    fill: var(--secondary);
  }
  .grid {
    stroke: var(--line);
    stroke-width: 1;
  }
  path {
    fill: none;
    stroke: var(--accent);
    stroke-width: 2.5;
    stroke-linecap: round;
    stroke-linejoin: round;
    vector-effect: non-scaling-stroke;
  }
  circle {
    fill: var(--accent);
  }
  .fault-mark {
    stroke: #c44f3b;
    stroke-width: 1;
    stroke-dasharray: 3 4;
  }
</style>
