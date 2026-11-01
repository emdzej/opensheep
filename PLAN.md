# OpenSheep plan

A faithful, from-scratch reimplementation of **Sheep** (Mind's Eye Productions / Empire
Interactive, Windows, 2000), built the way [OpenRF](../openrf) was: a portable C11 core ported
function by function from `Sheep.exe`, shipped as `opensheep.wasm` for
[gasm](../gasm) **0.16.0** (native bundles for macOS / Linux / Windows through `gasm-run`, and a
browser player). Site: **opensheep.emdzej.pl** (VitePress, `/play/` browser player).

Data findings so far: [docs/formats.md](docs/formats.md).

## Ground rules (carried over from OpenRF)

1. **Never commit original data or anything derived from it**: `cd/`, `re/`, `out/`, the ISO, the
   exe, decoded sprites/sounds/movies are git-ignored (`.gitignore` already set up).
2. **No tables copied from the exe into the source tree.** Tables the game needs from `Sheep.exe`
   (flock tuning, object descriptors, string tables, ...) are loaded at runtime by `src/exe.c`,
   which checks size (380,928) and CRC-32 of the file. Generators emit addresses/types/loaders only.
3. **Cite the original**: every ported function names its address (`/* Flock_Update 0x4123a0 */`),
   names kept in sync with the Ghidra project.
4. **No decompiler output** pasted into code or docs; describe layouts and formulas in our own words.
5. **Fidelity is the product**: reproduce behaviour, quirks and bugs; only fix deviations of the port.
6. Same native-build split as OpenRF: the CMake native build is headless tests and tools only; the
   game ships only as the gasm guest.

## What the player needs

The user's own disc, given to the runner as a folder (`--asset-dir`) or the `.iso` image
(`--asset cd=`, read through our ISO 9660 reader, reused from OpenRF's `vfs.c`). The browser player
takes the `.iso` via file picker / OPFS import, like OpenRF's `cd.js`. Files used: `Sheep.exe`,
`main.xp`, `Sound.xp`, `world0*.xp`, `Data/Biks/*.bik`, `Data/Sound/Music/*.wav`, `Misc*/HiScore.tab`.

## Phases

Each phase ends with something verifiable (a tool output, a test, a screenshot hash).

### 0. Scaffold (small)
- `AGENTS.md` (rules above, layout, traps), `README.md`, `LICENSE` (match OpenRF), `CMakeLists.txt`
  (`project(OpenSheep VERSION 0.1.0)`), `src/`, `tests/`, `tools/`, `docs/` (VitePress, own theme
  sampled from the game's palette, no emojis, "Built for gasm" badge), CI (`ci.yml`, `pages.yml`,
  `release.yml` copied from OpenRF and renamed).
- `tools/fetch-gasm-sdk.sh` with `GASM_VERSION=0.16.0`; `@emdzej/gasm-host` 0.16.0 in `docs/package.json`.
- Hello-world guest: opens the ISO, lists `main.xp`, presents a 640×480 test frame. Proves the
  asset path on `gasm-run` and in the browser.

### 1. Reverse-engineering setup
- Ghidra project `sheep`, program `Sheep.exe` (MSVC C++; recover vtables (and RTTI, if present) for
  the `C*` classes). Name the CRT, DirectX and Bink imports first, then the file layer
  (`.xp` open/read, RNC unpack) from the strings that reference `main.xp` / `World%02d.xp`.
- `re/all_named.c` full dump script (as in OpenRF), `docs/CONTEXT.md` agent briefing,
  `docs/architecture.md` module map, main loop, timing (frame rate / tick length).

### 2. Formats (Python decoders in `tools/`, notes in `docs/`)
Done: `.xp` archive + directory (`tools/xp.py`), RNC method 2 (`tools/rnc.py`), TGA/BMP/WAV,
UTF-16 string tables. Remaining, in order of need:
1. **SPR** sprite codec → PNG sheets for every sprite (`tools/spr.py`). Gate for anything visual.
2. **BLO** level: map dimensions, block graphics, layers, animated tiles → render a whole level to
   one PNG and compare against the `.TGA` overview map.
3. **FNT** fonts → render a strings page in each language (Polish first).
4. **POS / NPF / SPC / ANM / TIM** level objects, paths, specials, tile animations, par times.
5. **REC** demo recordings (input streams) — the replay oracle for phase 5.
6. **SLF** sound lists (trivial), `HiScore.tab` obfuscation, `Game%02d.sav` and `Sheep.ini` layouts.
7. **Bink 1** (`BIKi`): container, video planes/bundles/DCT, Bink audio (RDFT/DCT). Write a
   Python reference decoder first for intro/world movies, compare frames with a known-good player
   on the side (local only), then port to C.

### 3. Core engine in C (`src/`)
- Platform contract + gasm backend (`platform.h`, `platform_gasm.c`): `video_present` of a
  640×480 RGBA frame converted from the game's 16-bit surface (keep the 16-bit pipeline internally,
  so the colour quantisation matches), `audio_push`, keyboard/mouse/gamepad, `gasm:storage` for
  saves / hi-scores / settings.
- File layer: ISO 9660 + `.xp` directory + RNC unpacker (C port of `tools/rnc.py`).
- Draw manager: surfaces, sprite blitter (SPR codec), tile map renderer, scrolling camera, fonts.
- Sound manager: WAV effects mixer (22.05 kHz base), music streamed from `Data/Sound/Music`,
  per-level SLF lists.
- Movie player: own Bink 1 decoder (`src/bink.c`), audio in sync, skippable.
- Input: keyboard (arrows, Shift run, Alt creep/activate, Space shout/sweets, Tab map, Esc pause),
  mouse, joystick; `M`/`K`/`J` switching, as in `ReadMe.txt`.

### 4. Front end and meta-game
Logos/intro movies → main menu over the `MENULEV` level → new game / load / save / demo
record-play → player select (Adam, Bo Peep, Motley, Shep; 1 or 2 players) → sheep breed (Pastoral,
LongWool, Factoral, NeoGenetic) → control type → difficulty → world map → briefing (`.TGB` +
level text) → level → results (time, sheep saved, bonuses, gold/silver/bronze from `.TIM`) →
hi-score table → credits. Language selection over the 8 string sets (Polish default for our
own builds is a decision for later; see open questions).

### 5. Gameplay port (the bulk)
- Object system and per-frame update order; collision against the block map.
- Shepherd control: walk, run, creep, shout, carry, drop sweets, activate.
- **Flock AI**: amble/wander, neighbour distances, fleeing the player, herding, the per-breed
  differences; tuning values loaded from the exe at runtime.
- Hazards and props per world (combine harvesters, electric fences, conveyors, ...), driven by
  `POS`/`NPF`/`SPC`/`ANM`; scripted events and sounds via `SLF`.
- Scoring, timers, power-ups (`CPUI_PU*`), coins, bonus levels (sack race, race cars, dodgems,
  bonus 1/3/4), training levels, final level.
- Deterministic simulation (integer maths, ported MSVC `rand()`, no wall clock), so the `.REC`
  demos replay exactly: **demo replays are the main regression test** (state hashes per frame,
  like OpenRF's fire-demo hashes).
- Order: training levels (W09) → world 1 → bonus levels → worlds 2–7.

### 6. Release
- gasm bundles (`package-gasm.sh`, launchers, smoke tests) copied from OpenRF and renamed;
  browser player at `opensheep.emdzej.pl/play/`; screenshots via headless `gasm-run`.
- Semver tags without `v`, release job as in OpenRF.

## Rough size

| Area | Effort |
|---|---|
| Scaffold + gasm hello | small, mostly copy from OpenRF |
| SPR + BLO + fonts | medium, needs the exe readers |
| Bink 1 video + audio decoder | large (the single biggest isolated piece) |
| Front end / menus / meta | medium |
| Gameplay + flock AI + 35 levels' objects | large |
| Bundles, site, CI | small, copy from OpenRF |

## Risks

- **Bink 1**: complex codec with several revisions; we need only `BIKi`. Mitigation: Python
  reference first, test every movie on the disc, frame hashes as tests.
- **BLO size and layout**: biggest unknown in the data; first RE target after the sprite codec.
- **Timing**: frame rate and tick model not known yet; REC playback will tell whether we got it right.
- **C++ engine**: virtual dispatch makes the call graph harder to follow than OpenRF's C; recover
  vtables early.

## Open questions for you

1. Default language in our builds: follow the original (`Sheep.ini` / installer choice) with a
   menu to pick, or default to Polish?
2. Two-player (the menus have player 1 / player 2 select): same-keyboard only, as the original,
   with online lockstep as a later roadmap item like OpenRF?
3. GitHub repo name / org: `emdzej/opensheep`?
