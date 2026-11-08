<script>let { name, size = 24 } = $props();</script>
<svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  {#if name === 'home'}<path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7" />
  {:else if name === 'clock'}<circle cx="12" cy="12" r="8.5"/><path d="M12 7v5l3 2"/>
  {:else if name === 'settings'}<path d="M4 7h16M4 17h16"/><rect x="7" y="4" width="4" height="6" rx="1" fill="var(--surface)"/><rect x="14" y="14" width="4" height="6" rx="1" fill="var(--surface)"/>
  {:else if name === 'plus'}<path d="M5 12h14M12 5v14"/>
  {:else if name === 'minus'}<path d="M5 12h14"/>
  {:else if name === 'right'}<path d="m9 5 7 7-7 7"/>
  {:else if name === 'back'}<path d="m14 5-7 7 7 7M7 12h13"/>
  {:else if name === 'close'}<path d="m6 6 12 12M18 6 6 18"/>
  {:else if name === 'power'}<path d="M12 3v9M7 5.5a8 8 0 1 0 10 0"/>
  {:else if name === 'stop'}<rect x="6" y="6" width="12" height="12" rx="1"/>
  {:else if name === 'check'}<path d="m5 12 4 4 10-10"/>
  {:else if name === 'warning'}<path d="M12 3 2 21h20zM12 9v5M12 17v.2"/>
  {:else if name === 'wifi'}<path d="M3 8a14 14 0 0 1 18 0M6 12a9 9 0 0 1 12 0M9 16a4 4 0 0 1 6 0M12 20h.01"/>
  {:else if name === 'offline'}<path d="m3 3 18 18M6 12a9 9 0 0 1 6-2M14 6a14 14 0 0 1 7 2M9 16a4 4 0 0 1 6 0M12 20h.01"/>
  {:else if name === 'thermometer'}<path d="M9 14V5a3 3 0 0 1 6 0v9a5 5 0 1 1-6 0ZM12 8v10"/>
  {:else if name === 'upload'}<path d="M12 16V3m-5 5 5-5 5 5M4 15v6h16v-6"/>
  {:else if name === 'sun'}<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1 1M18 18l1 1M5 19l1-1M18 6l1-1"/>
  {/if}
</svg>
