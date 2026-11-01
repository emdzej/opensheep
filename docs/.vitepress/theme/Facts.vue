<template>
  <ul class="facts" aria-label="OpenSheep in numbers">
    <li><b>4</b> shepherds, <b>4</b> breeds</li>
    <li><b>23</b> levels hash-checked</li>
    <li><b>0</b> bytes of game data</li>
  </ul>
</template>

<style scoped>
.facts { display: flex; flex-wrap: wrap; gap: 8px; list-style: none; margin: 28px 0 0; padding: 0; }
.facts li {
  font-size: 13px; line-height: 1; padding: 8px 12px; border-radius: 999px;
  background: var(--vp-c-bg-soft); color: var(--vp-c-text-2);
}
.facts b { color: var(--vp-c-text-1); font-weight: 600; font-variant-numeric: tabular-nums; margin-right: 2px; }
</style>
