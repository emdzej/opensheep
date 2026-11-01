# Sheep.exe architecture

Addresses are in `Sheep.exe` (image base `0x400000`). Names follow `tools/ghidra/names.txt`.
Decompile dump: `re/all.c` (`tools/ghidra/apply.sh`), one function: `tools/fn.sh <addr>`.

## Address map

| Range | What |
|---|---|
| `0x401000-0x403fff` | Level resources: anim banks, object positions, tile anims, per-object move routines, archive mounting per world |
| `0x404000-0x4065ff` | File layer (CFile, archive set wrappers), fonts, sound buffers |
| `0x406cc0-0x40aea0` | Level setup and loading (`Level_LoadBLO`), saves, hi-score table, world screens, **app state machine** (`App_Frame`) |
| `0x40b000-0x410fff` | Level frame, HUD, menus, credits, hi-score screen, demo record/playback, results |
| `0x411000-0x414fff` | Hints, paths, shared object behaviours |
| `0x415000-0x4181ff` | Player (shepherd) movement and actions, player vars file |
| `0x4189d0-0x419fff` | DirectSound, sound effects |
| `0x41a000-0x41efff` | Sheep (flock) AI, flock vars file, level object script |
| `0x41f000-0x421fff` | Sprite banks and blitters (`Sprite_Draw`), allocator, file load helpers |
| `0x422000-0x4233ff` | Debug/error, INI, RNG, screenshots, TGA loader, strings |
| `0x423000-0x435fff` | Per-level code (one block per world and per bonus game) |
| `0x436000-0x438200` | DirectDraw draw manager and surfaces |
| `0x438220-0x43a7ef` | WinMain, CD check, DirectInput (keyboard, mouse, joystick + force feedback) |
| `0x43a7f0-0x43b4ff` | `.xp` archive set |
| `0x43b500-` | MSVC runtime (`rand` 0x43ca87, `sprintf` 0x43ca2b, ...) |

## Main loop and timing

`WinMain` (0x438220) reads the registry (`Language`, `CD_Path`, `Install_Path`...), picks the
language letter from `"eefgiseepckjrh"`, sets up DirectDraw at 640x480, mounts `main.xp`, loads
`data\local\GameStrings<L>`, then loops: Windows messages; while a Bink movie plays it is driven
instead of the game; otherwise `App_Frame` (0x40a170) then present.

The in-level frame (0x40b9b0) runs `elapsed_ms / 16` simulation ticks (`0x44b398` = 16, capped
at 6 per frame), then draws once. One tick: player update, sheep update (`0x41caf0`) every tick
(`0x44b39c` = 1), level tick hook, objects. **The port runs one tick per 62.5 Hz frame.**

## App state machine (`g_appState` 0x4a1ed0, sub-state 0x4a1eb0)

| State | What |
|---|---|
| 0 | Start: Logos.bik (or continue a saved game) |
| 1 | Prologue screen (`0x40d460`) |
| 2 | Intro.bik |
| 3, 4, 8, 9 | Load (world map or level) and run the level frame; leaves with an exit code in `0x4a4100` |
| 6 | Back to the world map |
| 10 | Level results (`0x410bc0`), world movies |
| 11 | Credits (`0x40d7a0`, `0x40d9e0`, `0x40dc70`) |
| 12 | Hi-score entry (`0x40e420`, `0x40e6b0`, `0x40e8f0`) |
| 13-15 | End splashes (`EndSplash*.bmp`, 600 frames each), then quit |
| 16-17 | `Hr3.bmp` / `EndSplash3.bmp`, then restart |

## Levels

`g_world` (0x4b0b60) 0..9, `g_level` (0x4b0f18) 0..5; descriptor table at `0x44adec` indexed
`world*6 + level`. Level 0 of a world is its world-map screen (shared descriptor 0x4519e0, world 0 =
the menu level); 1-4 are levels; 5 the bonus game. Descriptor fields (`src/world.h`): `+0x00` name
(UTF-16), `+0x08` zone rectangles, `+0x0c` init (after load), `+0x10` tick, `+0x18` the shepherd's
states >= 100, `+0x1c` / `+0x20` draw hooks, `+0x2c` tile animations, `+0x30` release, `+0x44` /
`+0x48` the sheep's and the shepherd's sprite banks, `+0x4c` the level's move routines, `+0x54` gates.
The hooks and move routines are exe addresses; the port maps each to its C routine (`src/levels.c`,
`src/moves.c`), and one without a port logs `... not ported`.

`Level_LoadBLO` (0x4072d0): see `docs/formats.md`. Tiles are 32x24 rgb565, re-packed at load into
the surface's pixel format (identity in the 16-bit rgb565 mode). Level paths: `World_SetPaths`
(0x4033a0) builds `Data\World0<w>\<name>\` (`World00` for level 0) from the descriptor's UTF-16
name and mounts `main.xp`, `sound.xp`, `World00.xp` / `World%02d.xp`; `Level_Path` (0x401000) adds
`<name>.<ext>`.

`Level_InitTileAnims` (0x407830): one surface per tile; map cells are 1-based tile numbers in bits
0-13 (through the remap table at 0x4ae444, identity after load) and a transform in bits 14-15
(1 mirror, 2 flip, 3 both). Each distinct transformed value gets its own surface appended after the
file's tiles and the cell is rewritten to point at it.

`Level_DrawMap` (0x407ba0): the camera (`g_camX` 0x4adeb0, `g_camY` 0x4adeb4) is clamped to the
header's bounds (+0x28..+0x34) by `Camera_Update` (0x40cb40), which also sets the fine scroll
(`x & 31`, `y % 24`). 20x20 cells (21 per axis when scrolled) are BltFast'ed; the outer column /
row is clipped with a source rectangle. Cells whose type (low byte of the u32 layer) is 1 or 2 are
not drawn. The menu level has no bounds: `Menu_SetPage` (0x427870) sets a 640x480 page from a
table at 0x450c48.

## Objects and the level frame

Static objects: 1024 x 0x78 at 0x47e070; animated objects: 512 x 0x10c at 0x459078 (slots from 40);
an id -> slot map at 0x47a878 (`| 0x8000` = static). Both carry a 0x58-byte draw record. The level
frame (0x40b9b0) ticks 0x4056f0, 0x408ca0, 0x40b870, the player 0x415f10, the sheep 0x41caf0, the
camera, the level hook, objects 0x402700 and 0x403890, then draws the map, the player
(0x40bec0/0x417070), the sheep 0x41e6d0, objects 0x402830, hook +0x1c, the sorted pass 0x40c1f0,
hook +0x20, 0x4181d0 and 0x40b590.

Sorted pass 0x40c1f0 (`src/render.c`): records go into 23 rows x 4 layers of buckets (cap 0x7e) by
mode (0/4 foot y, 1 the cell mask row 0x40c700, 2 last row, 3 first row) after the push adjust
0x40c070, are bubble-sorted by depth + height, and each row draws its sprites, then that row's
foreground cells (types 1-2 whole via 0x408180, >= 3 through the 32x24 per-type mask at `tab2 +
(type-1)*0x300` via 0x408630), then 2-3 extra rows below the screen. Record kinds: 7 shadow
(sheared), 1-5 sprite (2 scaled by the exe value at 0x44b3a0), 6 text.

## Simulation state

- Player (shepherd) struct at `0x4b9788`; sheep: 30 slots of `0x124` bytes at `0x4b99c0`.
  Positions are `float` (x87 code; the port keeps float storage and evaluates like MSVC would).
- Game RNG: 1024 values from MSVC `rand()` after `srand(0)` (`0x422830`, filled from index 1023
  down), read with a wrapping index (`0x422870`). The index is reset when a demo starts.
- Difficulty (`0x4a1e93`) 0/1/2 selects the sheep to pen (`0x4a1ea0`: 5/10/20; 1 in training) and
  the time limit (`0x4a2090`): the `.TIM` file's hard limit, twice that on normal, and normal + 18000
  ticks on easy (0x406c00).

## Where it lives in the port

| Original | `src/` |
|---|---|
| WinMain, the main loop, Bink in place of the frame | `app.c`, `movie.c`, `bink.c` |
| `App_Frame` and its states | `flow.c`, `results.c`, `credits.c`, `hiscore.c` |
| File layer, archives, RNC, ISO 9660 | `vfs.c`, `xp.c`, `rnc.c` |
| Level loading, map, tile animations | `level.c`, `world.c`, `levels.c` |
| Objects, `.POS` / `.ANM`, specials, attractors | `objects.c`, `game.c`, `paths.c` |
| The level frame, camera, sorted drawing | `play.c`, `render.c` |
| Shepherd, demos | `player.c` |
| Flock | `sheep.c` |
| Move routines, hints | `moves.c`, `hint.c` |
| Per-world code | `lv1.c` .. `lv7.c`, `lv9.c`, `bonus.c`, `bonus_cars.c` |
| HUD, texts, fonts | `hud.c`, `font.c` |
| Menus, pause menu, saves and settings | `menu.c`, `pause.c`, `save.c` |
| DirectDraw surfaces, sprites | `surface.c`, `sprites.c` |
| DirectSound | `sound.c` (a mixer in the core) |
| DirectInput | `input.c`, the backend's devices in `platform_gasm.c` |
| INI, debug keys | `cheats.c` |
| MSVC `rand`, the game RNG | `rng.c` |
