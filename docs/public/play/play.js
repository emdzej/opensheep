// OpenSheep in the browser: opensheep.wasm in gasm's Worker mode. The page keeps input, display and
// audio; the worker runs the game and reads the CD on demand, either from the site's OPFS copy
// (FileSystemSyncAccessHandle) or straight from the picked files (FileReaderSync).
//
// Test and debug query parameters:
//   level, ini, view, trace   passed to the game (docs/guide/developer.md)
//   hashframes=N   run N frames with no input on virtual time as fast as possible, then print the
//                  same "frames=... video_fnv32=... audio_fnv32=..." line as gasm-run --headless
//                  (globalThis.__opensheepResult)
//   autoplay       start the imported CD right away
//   filter=sharp|nearest|xbr|fsr|crt, integer   how the frames are scaled up (also in the toolbar)
import { BrowserInput, ProcExit, Resampler, browserGamepads, gamepadPads } from './vendor/gasm/gasm-host.js';
import { GasmWorker } from './vendor/gasm/gasm-worker.js';
import { FILTERS, GlPresenter } from './vendor/gasm/gasm-present.js';
import * as cd from './cd.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const GAME_PARAMS = ['level', 'ini', 'view', 'trace'];
const HASH_FRAMES = Number(query.get('hashframes') || 0);
const hasOpfs = !!navigator.storage?.getDirectory;

function message(text, kind = 'info') {
  const m = $('message');
  m.hidden = !text;
  m.textContent = text ?? '';
  m.className = `message ${kind}`;
  if (text) console.log(`[play] ${text}`);
}
const show = (id, on) => { $(id).hidden = !on; };

// ---- keyboard, mouse and gamepads ------------------------------------------------------------
// The game reads the raw keyboard (the original's DirectInput keys), the pointer's motion and buttons,
// and the gamepads itself; the page only collects them (BrowserInput) and hides or locks the cursor
// when the game asks (the mouse controller: click the game to capture it, Esc releases it). The
// gamepads also go as virtual pads 0-3.
const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
const keyboard = new BrowserInput($('screen'), { ignore: (e) => !running || typing(e) }).attach();
// While playing, keys shouldn't scroll the page or open the browser's menus (Alt); browser shortcuts
// (Cmd/Ctrl + key, F4-F12) still work. A tap of Escape goes to the game (the pause menu); holding it
// for a second stops the game, like gasm-run.
const browserKey = (e) => e.metaKey || (e.ctrlKey && !/^(Control|Shift)/.test(e.code)) || /^F([4-9]|1[0-2])$/.test(e.code);
let escDown = 0;
addEventListener('keydown', (e) => {
  if (!running || typing(e)) return;
  if (e.code === 'Escape') { if (!e.repeat) escDown = performance.now(); return; }
  if (!browserKey(e)) e.preventDefault();
});
// Windows and Linux browsers act on Alt's release (Firefox's menu bar, Chrome's menu takes the focus)
addEventListener('keyup', (e) => {
  if (e.code === 'Escape') escDown = 0;
  else if (running && !typing(e) && /^Alt/.test(e.code)) e.preventDefault();
});
// The mouse controller asks for the pointer locked (the game reads its motion, as DirectInput's). Browsers
// lock it only on a user gesture: try at once when the game asks (the M key may still count), then on a
// click of the game (BrowserInput's pointerdown; a click too, for Safari). The browser releases it on Esc.
const lockWanted = () => running && !!((worker?.inputMode ?? 0) & 4);
const locked = () => document.pointerLockElement === $('screen');
const lock = () => { if (lockWanted() && !locked()) Promise.resolve($('screen').requestPointerLock?.()).catch(() => {}); };
let lockWas = false, lockFailed = false;
function followLock() {
  const want = lockWanted();
  if (want && !lockWas) lock();
  lockWas = want;
}
$('screen').addEventListener('click', lock);
document.addEventListener('pointerlockchange', () => { if (locked()) lockFailed = false; });
document.addEventListener('pointerlockerror', () => { lockFailed = true; });
const lockHint = () => !lockWanted() || locked() ? ''
  : lockFailed ? ' · the browser refused to capture the mouse: click the game again'
  : ' · click the game to capture the mouse (Esc releases it)';
const readPads = () => gamepadPads(browserGamepads());

// ---- audio: the gasm web player's AudioWorklet queue ------------------------------------------
const WORKLET = `
class GasmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('gasm-out', GasmOut);`;

let audioCtx = null, audioNode = null, gain = null, resampler = null, audioReady = null;
let muted = localStorage.getItem('opensheep.muted') === '1';
/** Call from a click handler (browsers only start audio on a user gesture). */
function startAudio() {
  if (!audioCtx) {
    try {
      audioCtx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) { message(`No sound: ${e.message}`, 'warn'); return; }
    audioReady = audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))).then(() => {
      audioNode = new AudioWorkletNode(audioCtx, 'gasm-out', { outputChannelCount: [2] });
      gain = new GainNode(audioCtx, { gain: muted ? 0 : 1 });
      audioNode.connect(gain).connect(audioCtx.destination);
      resampler = new Resampler(audioCtx.sampleRate);
    }).catch((e) => { message(`No sound: ${e.message}`, 'warn'); audioCtx = null; });
  }
  audioCtx?.resume();
}
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out.slice());
}
function setMuted(m) {
  muted = m;
  localStorage.setItem('opensheep.muted', m ? '1' : '0');
  if (gain) gain.gain.value = m ? 0 : 1;
  $('sound').textContent = m ? 'Sound: off' : 'Sound: on';
}
$('sound').onclick = () => { startAudio(); setMuted(!muted); };
setMuted(muted);

// ---- display ------------------------------------------------------------------------------
// gasm's presenter (WebGL 2): letterboxed at the canvas's device-pixel size with an upscaling filter
// (?filter= / the toolbar, kept in localStorage; default sharp = plain pixel doubling at whole factors),
// optionally whole multiples only (?integer). Without WebGL 2, a canvas 2D scaled up by CSS.
const canvas = $('screen');
const view = {
  filter: FILTERS.includes(query.get('filter')) ? query.get('filter') : localStorage.getItem('opensheep.filter') ?? 'sharp',
  integerScale: query.has('integer') || localStorage.getItem('opensheep.integer') === '1',
};
if (!FILTERS.includes(view.filter)) view.filter = 'sharp';
let presenter = null, ctx2d = null, shown = null;   // shown: the frame on screen [rgba, w, h]
function present(rgba, w, h) {
  shown = [rgba, w, h];
  if (!ctx2d && !presenter) {
    presenter = GlPresenter.create(canvas);
    if (presenter) canvas.classList.add('gl');
    else { ctx2d = canvas.getContext('2d'); show('view-controls', false); }
  }
  if (presenter) {
    const size = [Math.round(canvas.clientWidth * devicePixelRatio) || w, Math.round(canvas.clientHeight * devicePixelRatio) || h];
    return presenter.draw(rgba, w, h, size, view);
  }
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  ctx2d.putImageData(new ImageData(rgba, w, h), 0, 0);
}
const redraw = () => { if (shown) present(...shown); };
new ResizeObserver(redraw).observe(canvas);
for (const f of FILTERS) $('filter').add(new Option(f, f));
$('filter').value = view.filter;
$('integer').checked = view.integerScale;
$('filter').onchange = () => { view.filter = $('filter').value; localStorage.setItem('opensheep.filter', view.filter); redraw(); };
$('integer').onchange = () => {
  view.integerScale = $('integer').checked;
  localStorage.setItem('opensheep.integer', view.integerScale ? '1' : '0');
  redraw();
};
/** The frame on screen at its own size, as a PNG data URL (tests; independent of the filter). */
globalThis.__opensheepFramePng = () => {
  if (!shown) return null;
  const [rgba, w, h] = shown, c = document.createElement('canvas');
  c.width = w; c.height = h;
  c.getContext('2d').putImageData(new ImageData(rgba, w, h), 0, 0);
  return c.toDataURL('image/png');
};
$('fullscreen').onclick = () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else ($('stage').requestFullscreen ?? $('stage').webkitRequestFullscreen)?.call($('stage'));
};
canvas.ondblclick = () => $('fullscreen').onclick();

// ---- running the game ---------------------------------------------------------------------
let worker = null, running = false, inflight = false, rafId = 0, wasmModule = null, lastLog = '';
let acc = 0, last = 0, fpsN = 0, fpsT = 0;

/** opensheep.wasm, compiled once; every start shares the module with its worker. */
async function loadWasm() {
  if (!wasmModule) {
    const r = await fetch(new URL('opensheep.wasm', import.meta.url));
    if (!r.ok) throw new Error(`opensheep.wasm: HTTP ${r.status}`);
    wasmModule = await WebAssembly.compile(await r.arrayBuffer());
  }
  return wasmModule;
}

function onLog(msg) {
  console.log(msg);
  if (/^\[guest\]/.test(msg)) lastLog = msg.replace(/^\[guest\]\s*/, '');
}

async function stopGame() {
  cancelAnimationFrame(rafId);
  running = false; inflight = false; escDown = 0;
  const w = worker; worker = null;
  await w?.exit();          // flushes the saves, releases the OPFS handles
}

function stopped(e) {
  running = false;
  if (e instanceof ProcExit) message('The game has ended. Press Play to start again.');
  else message(`The game stopped: ${e.message}${lastLog ? ` (${lastLog})` : ''}`, 'error');
  endGameView();
}

function endGameView() {
  cancelAnimationFrame(rafId);
  keyboard.setMode(0);          // the cursor back
  show('setup', true);
  refreshImported();
}

/** Start the game from 'opfs' (the imported CD) or a checked source (read in place). */
async function play(source) {
  startAudio();                        // still inside the click's user gesture
  message('');
  await stopGame();
  lastLog = '';
  show('game', true);
  show('setup', HASH_FRAMES > 0);      // keep the page visible in test runs
  $('status').textContent = 'Starting...';
  const params = Object.fromEntries(GAME_PARAMS.filter((k) => query.has(k)).map((k) => [k, query.get(k)]));
  try {
    await audioReady;
    worker = await GasmWorker.start({
      wasm: await loadWasm(), assets: cd.assetSpecs(source), params, storage: 'opensheep',
      hashing: HASH_FRAMES > 0, virtualTime: HASH_FRAMES > 0, onLog, onAudio,
    });
  } catch (e) {
    worker = null;
    show('game', false);
    const busy = /NoModificationAllowed|InvalidState|lock|access handle/i.test(`${e.name} ${e.message}`);
    if (busy) message('The imported CD is in use by another OpenSheep tab or window. Close it and try again.', 'error');
    else if (e instanceof ProcExit) message(`The game could not start${lastLog ? `: ${lastLog}` : ''}.`, 'error');
    else message(`The game could not start: ${e.message}`, 'error');
    endGameView();
    return;
  }
  document.title = 'Sheep | OpenSheep';
  if (HASH_FRAMES > 0) return hashRun(HASH_FRAMES);
  $('stage').focus?.();
  acc = 0; last = performance.now(); running = true;
  rafId = requestAnimationFrame(tick);
}

function tick(now) {
  rafId = requestAnimationFrame(tick);
  if (!running || !worker) return;
  if (escDown && now - escDown >= 1000) {   // Escape held: stop (the tap already went to the game)
    $('stop').onclick();
    return;
  }
  const period = 1000 / worker.frameRate;
  acc += Math.min(now - last, 100);    // clamp after tab switches
  last = now;
  // Fixed timestep; when catching up (at most 4 frames), only the last one is shown.
  const due = Math.min(4, Math.floor(acc / period));
  if (!inflight && due > 0) {          // one batch in flight: the worker sets the pace
    const pads = readPads();
    inflight = true;
    acc -= due * period;
    const steps = Array.from({ length: due }, (_, k) => ({ pads, input: keyboard.frame(k === 0) }));
    worker.frames(steps, true).then((r) => {
      inflight = false;
      keyboard.setMode(worker?.inputMode ?? 0);   // the cursor hidden / locked as the game asked
      followLock();
      fpsN += due;
      if (r.frame) present(r.frame.rgba, r.frame.width, r.frame.height);
    }, stopped);
  }
  if (acc > period * 4) acc = 0;       // fell far behind: resync
  if (now - fpsT >= 1000) { $('status').textContent = `${fpsN} frames/s${lockHint()}`; fpsN = 0; fpsT = now; }
}

const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
async function hashRun(n) {
  let left = n, r = null;
  try {
    while (left > 0) {
      const k = Math.min(250, left);
      r = await worker.frames(Array.from({ length: k }, () => [0, 0, 0, 0]), left === k);
      if (r.frame) present(r.frame.rgba, r.frame.width, r.frame.height);
      left -= k;
    }
  } catch (e) { if (!(e instanceof ProcExit)) { message(`hash run failed: ${e.message}`, 'error'); throw e; } }
  const s = worker.stats;
  const out = `frames=${s.frames} presented=${s.presented} size=${s.width}x${s.height} ` +
              `video_fnv32=${hex(s.videoHash)} audio_fnv32=${hex(s.audioHash)} audio_frames=${s.audioFrames}`;
  $('status').textContent = out;
  console.log(out);
  globalThis.__opensheepResult = out;
}

$('stop').onclick = async () => { await stopGame(); show('game', false); endGameView(); };
addEventListener('pagehide', () => { worker?.exit(); });

// ---- choosing and importing the CD ----------------------------------------------------------
let checked = null; // result of cd.folderFrom... / cd.checkImage

function showCheck(result) {
  checked = result;
  const list = $('check-list');
  list.textContent = '';
  const item = (text, ok) => {
    const li = document.createElement('li');
    li.className = ok ? 'ok' : 'bad';
    li.textContent = text;
    list.append(li);
  };
  const what = result.source.kind === 'image' ? 'disc image' : 'folder';
  item(`${what} "${result.source.name}": ${result.source.kind === 'image' ? mbText(result.bytes) : `${result.files} files, ${mbText(result.bytes)}`}`, true);
  if (result.problems.length) {
    for (const p of result.problems) item(p, false);
    item('This does not look like the Sheep CD.', false);
  } else {
    item(result.source.kind === 'image' ? 'Sheep.exe, main.xp and Data found on the disc' : 'Sheep.exe, binkw32.dll, main.xp and Data found', true);
  }
  show('check', true);
  show('check-actions', !result.problems.length);
  $('import').hidden = !hasOpfs;
  globalThis.__opensheepChecked = { problems: result.problems, files: result.files, bytes: result.bytes };
}
const mbText = cd.mb;

$('pick-folder').onclick = async () => {
  message('');
  if (!window.showDirectoryPicker) return $('folder-input').click(); // Firefox, Safari
  let handle;
  try { handle = await showDirectoryPicker({ id: 'opensheep-cd', mode: 'read' }); } catch (e) {
    if (e.name !== 'AbortError') message(e.message, 'error');
    return;
  }
  message('Reading the folder...');
  try { showCheck(await cd.folderFromHandle(handle)); message(''); } catch (e) { message(`Could not read the folder: ${e.message}`, 'error'); }
};
$('folder-input').onchange = (e) => {
  if (e.target.files.length) showCheck(cd.folderFromFileList(e.target.files));
  e.target.value = '';
};
$('pick-image').onclick = () => { message(''); $('image-input').click(); };
$('image-input').onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = '';
  if (f) showCheck(await cd.checkImage(f));
};

$('import').onclick = async () => {
  if (!checked) return;
  startAudio();
  const source = checked.source;
  show('check', false);
  show('progress', true);
  $('pick-folder').disabled = $('pick-image').disabled = true;
  const t0 = performance.now();
  try {
    await stopGame();
    const info = await cd.importSource(source, (p) => {
      $('progress-bar').value = p.total ? p.bytes / p.total : 1;
      $('progress-text').textContent = `Copying ${p.file}: ${mbText(p.bytes)} of ${mbText(p.total)} (${p.done} of ${p.files} files)`;
    });
    $('progress-text').textContent = `Imported ${info.files} files, ${mbText(info.bytes)} in ${((performance.now() - t0) / 1000).toFixed(0)} s.`;
    globalThis.__opensheepImported = info;
    await refreshImported();
    show('progress', false);
    await play('opfs');
  } catch (e) {
    show('progress', false);
    const full = /quota/i.test(`${e.name} ${e.message}`);
    message(full ? 'Not enough browser storage for the CD (about 490 MB). Free some space, or play without importing.'
                 : `Import failed: ${e.message}. You can still play without importing.`, 'error');
    show('check', true);
    await cd.removeImport().catch(() => {});
    refreshImported();
  } finally {
    $('pick-folder').disabled = $('pick-image').disabled = false;
  }
};
$('play-direct').onclick = () => { if (checked) play(checked.source); };
$('play-opfs').onclick = () => play('opfs');
$('remove').onclick = async () => {
  await stopGame();
  show('game', false);
  try { await cd.removeImport(); message('The imported CD was removed from this browser.'); } catch (e) { message(`Could not remove it: ${e.message}`, 'error'); }
  refreshImported();
};
$('persist').onclick = async () => {
  const ok = await cd.requestPersist();
  message(ok ? 'The browser will keep the imported CD.' : 'The browser did not agree to keep the data; it stays until space runs low.', ok ? 'info' : 'warn');
  refreshImported();
};

async function refreshImported() {
  const info = hasOpfs ? await cd.existingImport() : null;
  show('imported', !!info);
  $('choose-title').textContent = info ? 'Your CD' : 'Choose your CD';
  if (info) {
    $('imported-desc').textContent = `${info.kind === 'image' ? 'disc image' : 'folder'} "${info.name}", ${info.files} files, ${mbText(info.bytes)}`;
    const s = await cd.storageInfo();
    $('storage').textContent = s.usage !== undefined
      ? `This site uses ${mbText(s.usage)} of the ${mbText(s.quota ?? 0)} the browser allows${s.persisted ? '; kept even when space runs low' : ''}.`
      : '';
    show('persist-row', !s.persisted && !!navigator.storage?.persist);
  }
  return info;
}

if (!hasOpfs) message('This browser has no private file storage (OPFS), so the CD can\'t be imported. Play without importing works.', 'warn');
if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') message('This browser can\'t run OpenSheep (it needs WebAssembly and Web Workers).', 'error');
const ready = refreshImported();
ready.then((info) => {
  globalThis.__opensheepReady = true;
  if (info && query.has('autoplay')) play('opfs');
});
globalThis.opensheepPlay = { play, stopGame, refreshImported, cd };
