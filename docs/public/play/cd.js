// The player's Sheep CD: picking it, checking it, and importing it into the site's
// origin private file system (OPFS). Nothing here is game data; everything comes from the
// user's own disc and stays on their device.
//
// A source is either
//   { kind: 'folder', name, entries: [[disc path, File], ...] }   a mounted CD or an extracted folder
//   { kind: 'image',  name, file }                                 an .iso or raw MODE1/2352 .bin
// The game (opensheep.wasm) reads a folder as separate assets named by their disc paths
// (main.xp, Data/Biks/... as on the disc) and an image as the single asset "cd" (see src/platform_gasm.c).
import { directoryHandleEntries, fileListEntries } from './vendor/gasm/gasm-host.js';
import { clearNamespace, opfsFileSystem, persist } from '@emdzej/csfs-opfs';

/** OPFS directory the CD is imported into. */
export const NAMESPACE = 'opensheep-cd';
/** Written last: an import without it is incomplete. Hidden, so the game never sees it. */
const MARKER = '.opensheep-import.json';
/** What the game reads from the disc: the program (its tables), Bink's tables, the archives, Data and
 *  the front end's Misc* folders. The installer, DirectX and setup files are skipped. */
const NEEDED_ROOT = (n) => n === 'SHEEP.EXE' || n === 'BINKW32.DLL' || /\.XP$/.test(n);
const NEEDED_DIR = (n) => n === 'DATA' || /^MISC/.test(n);
/** Sheep.exe of the CD (src/exe.c checks the size and CRC-32 b7bcd581). */
export const SHEEP_EXE_SIZE = 380928;
const REQUIRED = ['SHEEP.EXE', 'BINKW32.DLL', 'MAIN.XP'];

const upper = (s) => s.replace(/[a-z]/g, (c) => c.toUpperCase());
export const mb = (n) => (n >= 1 << 30 ? `${(n / 2 ** 30).toFixed(1)} GB` : `${(n / 1048576).toFixed(n < 10485760 ? 1 : 0)} MB`);

// ---- picking --------------------------------------------------------------------------

/** Folder from showDirectoryPicker() (Chromium). */
export async function folderFromHandle(handle) {
  return checkFolder(handle.name, await directoryHandleEntries(handle));
}

/** Folder from <input type="file" webkitdirectory> (all browsers). */
export function folderFromFileList(files) {
  const name = files[0]?.webkitRelativePath.split('/')[0] || 'folder';
  return checkFolder(name, fileListEntries(files));
}

/**
 * Keep what the game needs and check it. Accepts the CD folder itself or a folder one level
 * above it (e.g. "Sheep CD/SHEEP/..."). Returns { source, problems, files, bytes }.
 */
export function checkFolder(name, entries) {
  const bin = entries
    .map(([p]) => p)
    .filter((p) => upper(p.split('/').pop()) === 'SHEEP.EXE' && p.split('/').length <= 2)
    .sort((a, b) => a.length - b.length)[0];
  const root = bin && bin.includes('/') ? bin.slice(0, bin.lastIndexOf('/') + 1) : '';
  const kept = [];
  for (const [path, file] of entries) {
    if (!path.startsWith(root)) continue;
    const rel = path.slice(root.length), segs = rel.split('/');
    const top = upper(segs[0]);
    if ((segs.length === 1 && NEEDED_ROOT(top)) || (segs.length > 1 && NEEDED_DIR(top))) kept.push([rel, file]);
  }
  const byName = new Map(kept.map(([p, f]) => [upper(p), f]));
  const problems = [];
  for (const req of REQUIRED) if (!byName.has(req)) problems.push(`${req} is missing`);
  const exe = byName.get('SHEEP.EXE');
  if (exe && exe.size !== SHEEP_EXE_SIZE) {
    problems.push(`Sheep.exe is ${exe.size.toLocaleString('en')} bytes; the CD's is ${SHEEP_EXE_SIZE.toLocaleString('en')}`);
  }
  if (![...byName.keys()].some((p) => p.startsWith('DATA/'))) problems.push('the Data folder is missing');
  const bytes = kept.reduce((n, [, f]) => n + f.size, 0);
  return { source: { kind: 'folder', name: root ? `${name}/${root.slice(0, -1)}` : name, entries: kept }, problems, files: kept.length, bytes };
}

/** An .iso or raw .bin: find Sheep.exe, main.xp and Data in its root directory. */
export async function checkImage(file) {
  const problems = [];
  const source = { kind: 'image', name: file.name, file };
  const result = { source, problems, files: 1, bytes: file.size };
  if (/\.cue$/i.test(file.name)) {
    problems.push('this is the cue sheet: choose the .bin file it names');
    return result;
  }
  const read = async (off, len) => new Uint8Array(await file.slice(off, off + len).arrayBuffer());
  // ISO 9660: the primary volume descriptor is sector 16. Cooked .iso: 2048-byte sectors;
  // raw MODE1/2352 .bin: 16-byte sync + header before each sector's 2048 bytes of data.
  let layout = null;
  for (const [size, head] of [[2048, 0], [2352, 16]]) {
    const pvd = await read(16 * size + head, 2048);
    if (pvd[0] === 1 && String.fromCharCode(...pvd.subarray(1, 6)) === 'CD001') { layout = { size, head, pvd }; break; }
  }
  if (!layout) {
    problems.push('not an ISO 9660 disc image (.iso, or a raw MODE1/2352 .bin)');
    return result;
  }
  const u32 = (b, o) => b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] * 16777216);
  const sectors = async (lba, n) => {
    const out = new Uint8Array(n * 2048);
    for (let i = 0; i < n; i++) out.set(await read((lba + i) * layout.size + layout.head, 2048), i * 2048);
    return out;
  };
  const root = layout.pvd.subarray(156);
  const dir = await sectors(u32(root, 2), Math.min(8, Math.ceil(u32(root, 10) / 2048)));
  const names = new Map();
  for (let p = 0; p < dir.length;) {
    const len = dir[p];
    if (!len) { p = (Math.floor(p / 2048) + 1) * 2048; continue; }
    const name = String.fromCharCode(...dir.subarray(p + 33, p + 33 + dir[p + 32])).replace(/;1$/, '');
    names.set(upper(name), { size: u32(dir, p + 10), isDir: (dir[p + 25] & 2) !== 0 });
    p += len;
  }
  const exe = names.get('SHEEP.EXE');
  if (!exe) problems.push('Sheep.exe is not on this disc');
  else if (exe.size !== SHEEP_EXE_SIZE) problems.push(`Sheep.exe is ${exe.size.toLocaleString('en')} bytes; the CD's is ${SHEEP_EXE_SIZE.toLocaleString('en')}`);
  if (!names.has('MAIN.XP')) problems.push('main.xp is not on this disc');
  if (!names.get('DATA')?.isDir) problems.push('the Data folder is not on this disc');
  return result;
}

// ---- OPFS import ----------------------------------------------------------------------

/** The completed import's description ({ kind, name, files, bytes, date }), or null. */
export async function existingImport() {
  try {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle(NAMESPACE);
    const f = await (await dir.getFileHandle(MARKER)).getFile();
    return JSON.parse(await f.text());
  } catch {
    return null;
  }
}

export async function removeImport() {
  await clearNamespace(NAMESPACE);
  try { await (await navigator.storage.getDirectory()).removeEntry(NAMESPACE, { recursive: true }); } catch {}
}

/** Ask the browser to keep the data under storage pressure. Firefox may never answer a prompt. */
export async function requestPersist() {
  return persist({ signal: AbortSignal.timeout(10000) }).catch(() => false);
}

export async function storageInfo() {
  const est = await navigator.storage?.estimate?.().catch(() => null);
  const persisted = await navigator.storage?.persisted?.().catch(() => false);
  return { usage: est?.usage, quota: est?.quota, persisted: !!persisted };
}

/**
 * Copy a checked source into OPFS (csfs writes each file through a stream, so a large
 * file never sits in memory). onProgress({ bytes, total, file, files, done }).
 */
export async function importSource(source, onProgress = () => {}) {
  const items = source.kind === 'image' ? [['cd', source.file]] : source.entries;
  const total = items.reduce((n, [, f]) => n + f.size, 0);
  await persist({ signal: AbortSignal.timeout(3000) }).catch(() => false);
  await removeImport();
  const fs = await opfsFileSystem({ namespace: NAMESPACE });
  let bytes = 0, last = 0;
  for (let i = 0; i < items.length; i++) {
    const [path, file] = items[i];
    const counted = file.stream().pipeThrough(new TransformStream({
      transform(chunk, ctrl) {
        bytes += chunk.byteLength;
        const now = performance.now();
        if (now - last > 100) { last = now; onProgress({ bytes, total, file: path, files: items.length, done: i }); }
        ctrl.enqueue(chunk);
      },
    }));
    await fs.write(`/${path}`, counted);
    onProgress({ bytes, total, file: path, files: items.length, done: i + 1 });
  }
  const info = { kind: source.kind, name: source.name, files: items.length, bytes: total, date: new Date().toISOString() };
  await fs.write(`/${MARKER}`, new TextEncoder().encode(JSON.stringify(info)));
  return info;
}

// ---- running --------------------------------------------------------------------------

/** gasm Worker-mode asset specs for a source ('opfs' = the imported CD). */
export function assetSpecs(source) {
  if (source === 'opfs') return [{ kind: 'opfs', dir: NAMESPACE }];
  if (source.kind === 'image') return [{ kind: 'files', entries: [['cd', source.file]] }];
  return [{ kind: 'files', entries: source.entries }];
}
