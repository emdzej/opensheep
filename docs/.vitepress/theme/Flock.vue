<script setup lang="ts">
// The home page's field: a small flock seen from above that drifts about and keeps away from the
// pointer, the way Sheep's flock keeps away from the shepherd. Drawn on a canvas, no game assets.
// With prefers-reduced-motion the flock is settled once and drawn still.
import { onBeforeUnmount, onMounted, ref } from "vue";

const canvas = ref<HTMLCanvasElement | null>(null);
let stop = () => {};

type Sheep = { x: number; y: number; vx: number; vy: number; a: number; wob: number };

onMounted(() => {
  const c = canvas.value!;
  const g = c.getContext("2d")!;
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  let w = 0, h = 0, dpr = 1, raf = 0, visible = true, frame = 0;
  let colours = { a: "#3b6e2a", b: "#2f5f22", wool: "#f7f4ea", shade: "#d9d3c3", face: "#26221d" };
  const pointer = { x: -1e4, y: -1e4 };
  const flock: Sheep[] = [];
  // a fixed seed: the same flock on every visit
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);

  function readColours() {
    const s = getComputedStyle(c);
    const v = (n: string, d: string) => s.getPropertyValue(n).trim() || d;
    colours = { a: v("--flock-field-1", colours.a), b: v("--flock-field-2", colours.b), wool: v("--flock-wool", colours.wool),
                shade: v("--flock-shade", colours.shade), face: v("--flock-face", colours.face) };
  }

  function resize() {
    const r = c.getBoundingClientRect();
    dpr = Math.min(2, devicePixelRatio || 1);
    const first = !w;
    w = r.width; h = r.height;
    c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
    if (first) {
      for (let i = 0; i < 8; i++)
        flock.push({ x: w * (0.3 + rnd() * 0.4), y: h * (0.3 + rnd() * 0.4), vx: 0, vy: 0, a: rnd() * 6.28, wob: rnd() * 6.28 });
    }
  }

  function step() {
    for (const s of flock) {
      let ax = 0, ay = 0;
      // amble: a slowly turning wish to walk somewhere
      s.wob += (rnd() - 0.5) * 0.3;
      ax += Math.cos(s.wob) * 0.012; ay += Math.sin(s.wob) * 0.012;
      // stay with the flock, not on top of each other
      let cx = 0, cy = 0;
      for (const o of flock) {
        cx += o.x; cy += o.y;
        if (o === s) continue;
        const dx = s.x - o.x, dy = s.y - o.y, d = Math.hypot(dx, dy) || 1;
        if (d < 34) { ax += (dx / d) * (34 - d) * 0.01; ay += (dy / d) * (34 - d) * 0.01; }
      }
      cx /= flock.length; cy /= flock.length;
      ax += (cx - s.x) * 0.0004; ay += (cy - s.y) * 0.0004;
      // keep away from the shepherd (the pointer)
      const px = s.x - pointer.x, py = s.y - pointer.y, pd = Math.hypot(px, py) || 1;
      if (pd < 120) { const f = ((120 - pd) / 120) ** 2 * 0.5; ax += (px / pd) * f; ay += (py / pd) * f; }
      // the field's hedges
      const m = 28;
      if (s.x < m) ax += (m - s.x) * 0.01; if (s.x > w - m) ax -= (s.x - (w - m)) * 0.01;
      if (s.y < m) ay += (m - s.y) * 0.01; if (s.y > h - m) ay -= (s.y - (h - m)) * 0.01;
      s.vx = (s.vx + ax) * 0.94; s.vy = (s.vy + ay) * 0.94;
      const sp = Math.hypot(s.vx, s.vy), max = pd < 120 ? 2.4 : 0.45;
      if (sp > max) { s.vx *= max / sp; s.vy *= max / sp; }
      s.x += s.vx; s.y += s.vy;
      if (sp > 0.05) {   // turn towards where it walks, gently
        let d = Math.atan2(s.vy, s.vx) - s.a;
        d = Math.atan2(Math.sin(d), Math.cos(d));
        s.a += d * 0.08;
      }
    }
  }

  function drawSheep(s: Sheep) {
    g.save();
    g.translate(s.x, s.y);
    g.rotate(s.a);
    g.fillStyle = "rgba(0,0,0,0.14)";
    g.beginPath(); g.ellipse(2, 4, 15, 11, 0, 0, 6.29); g.fill();
    g.fillStyle = colours.shade;
    for (const [x, y, r] of [[-7, 0, 9], [0, -5, 9], [0, 5, 9], [6, 0, 9]]) { g.beginPath(); g.arc(x, y + 1, r, 0, 6.29); g.fill(); }
    g.fillStyle = colours.wool;
    for (const [x, y, r] of [[-7, 0, 8.5], [0, -5, 8.5], [0, 5, 8.5], [6, 0, 8.5]]) { g.beginPath(); g.arc(x, y, r, 0, 6.29); g.fill(); }
    g.fillStyle = colours.face;
    g.beginPath(); g.ellipse(16, 0, 5.5, 4.5, 0, 0, 6.29); g.fill();
    g.restore();
  }

  function draw() {
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const grad = g.createLinearGradient(0, 0, w, h);
    grad.addColorStop(0, colours.a); grad.addColorStop(1, colours.b);
    g.fillStyle = grad; g.fillRect(0, 0, w, h);
    // mowing stripes, barely there
    g.fillStyle = "rgba(255,255,255,0.018)";
    for (let x = -h; x < w; x += 56) {
      g.beginPath(); g.moveTo(x, 0); g.lineTo(x + 28, 0); g.lineTo(x + 28 + h, h); g.lineTo(x + h, h); g.fill();
    }
    for (const s of [...flock].sort((a, b) => a.y - b.y)) drawSheep(s);
  }

  function tick() {
    raf = requestAnimationFrame(tick);
    if (!visible || document.hidden) return;
    if (frame++ % 30 === 0) readColours();
    step(); draw();
  }

  const move = (e: PointerEvent) => { const r = c.getBoundingClientRect(); pointer.x = e.clientX - r.left; pointer.y = e.clientY - r.top; };
  const leave = () => { pointer.x = pointer.y = -1e4; };
  const ro = new ResizeObserver(() => { resize(); if (still) draw(); });
  const io = new IntersectionObserver(([e]) => { visible = e.isIntersecting; });
  const mo = new MutationObserver(() => { readColours(); if (still) draw(); });
  resize(); readColours();
  ro.observe(c); io.observe(c);
  mo.observe(document.documentElement, { attributes: true, attributeFilter: ["class"] });
  if (still) {
    for (let i = 0; i < 300; i++) step();
    draw();
  } else {
    c.addEventListener("pointermove", move);
    c.addEventListener("pointerleave", leave);
    raf = requestAnimationFrame(tick);
  }
  stop = () => { cancelAnimationFrame(raf); ro.disconnect(); io.disconnect(); mo.disconnect(); };
});
onBeforeUnmount(() => stop());
</script>

<template>
  <div class="flock-field">
    <canvas ref="canvas" aria-hidden="true"></canvas>
  </div>
</template>

<style scoped>
.flock-field {
  position: relative;
  width: 100%;
  height: 100%;
  min-height: 300px;
  border-radius: 28px;
  overflow: hidden;
  box-shadow: 0 1px 2px rgba(20, 30, 10, 0.06), 0 18px 40px -18px rgba(20, 40, 10, 0.35);
}
canvas { display: block; width: 100%; height: 100%; cursor: default; touch-action: pan-y; }
</style>
