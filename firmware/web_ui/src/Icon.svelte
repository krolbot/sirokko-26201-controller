<script>
  let { name, size = 20 } = $props();
</script>

<svg
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill="none"
  stroke="currentColor"
  stroke-width="1.5"
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden="true"
>
  {#if name === "home"}<path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7" />
  {:else if name === "settings"}<path d="M4 7h16M4 17h16" /><rect
      x="7"
      y="4"
      width="4"
      height="6"
      rx="1"
      fill="var(--surface)"
    /><rect x="14" y="14" width="4" height="6" rx="1" fill="var(--surface)" />
  {:else if name === "thermometer"}<path
      d="M9 14V5a3 3 0 0 1 6 0v9a5 5 0 1 1-6 0ZM12 8v10"
    />
  {:else if name === "clock"}<circle cx="12" cy="12" r="9" /><path
      d="M12 6v6l4 2"
    />
  {:else if name === "chevron"}<path d="m9 5 7 7-7 7" />
  {:else if name === "power"}<path d="M12 2v10M6 5a9 9 0 1 0 12 0" />
  {:else if name === "check"}<path d="m5 12 4 4L19 6" />
  {:else if name === "wifi"}<path
      d="M2 8a16 16 0 0 1 20 0M5 12a11 11 0 0 1 14 0M8 16a6 6 0 0 1 8 0"
    /><circle cx="12" cy="20" r=".5" />
  {:else if name === "moon"}<path d="M20 15A9 9 0 0 1 9 4a9 9 0 1 0 11 11Z" />
  {:else if name === "update"}<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5" />
  {:else if name === "tune"}<path d="M4 7h16M4 17h16" /><circle
      cx="8"
      cy="7"
      r="3"
      fill="var(--surface)"
    /><circle cx="16" cy="17" r="3" fill="var(--surface)" />
  {:else if name === "fan"}<circle cx="12" cy="12" r="2" /><path
      d="M10 10C4 11 3 7 5 5s7-1 7 5M14 10c-1-6 3-7 5-5s1 7-5 7M14 14c6-1 7 3 5 5s-7 1-7-5M10 14c1 6-3 7-5 5s-1-7 5-7"
    />
  {:else if name === "heat"}<path
      d="M8 3c-5 6 5 8 0 14M16 3c-5 6 5 8 0 14M4 21h16"
    />
  {/if}
</svg>
