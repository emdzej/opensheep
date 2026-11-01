# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[opensheep.emdzej.pl](https://opensheep.emdzej.pl).

## What this is

OpenSheep is a faithful, from-scratch reimplementation of **Sheep** (Mind's Eye Productions / Empire
Interactive, Windows, 2000) in C11: a portable core plus a gasm platform backend. It is not an emulator
or a wrapper: every subsystem is ported function by function from the original program (`Sheep.exe` on
the CD, 380,928 bytes, CRC-32 `b7bcd581`). The game ships as `opensheep.wasm`, a guest for the
[gasm](https://github.com/emdzej/gasm) runtime, 0.16.0. The native CMake build is the headless tests and
tools only.

**Fidelity is the product.** Reproduce the original's behaviour, quirks and bugs included. If something
looks wrong, check the disassembly; if the original does it, keep it and say so in a comment. Only fix
deviations *of the port*. Real undefined behaviour (reads past an array, uninitialised stack words) is
guarded, with a comment describing what the original does.

## Hard rules

1. **Never commit original data or anything derived from it.** `cd/`, `re/` (decompiler dumps), `out/`,
   `sheep_game.exe` (the local copy of `Sheep.exe`) and the ISO are git-ignored: keep it that way.
2. **No tables copied from the executable.** What the game needs from `Sheep.exe` (strings, level and
   object tables, menu pages, fonts, palettes, constants in `.rdata`) is read at run time through
   `src/exe.h` (`exe_u8/u16/u32/s16/s32/f32/f64`, `exe_str`, `exe_wtext`). The same for `binkw32.dll`'s
   codec tables (`src/bink.c`). Instruction operands and small algorithmic constants are fine.
3. **Cite the original.** Every ported function names its address: `/* 0x40aea0 */`. Globals keep
   theirs in the declaration comment.
4. **Don't paste decompiler output** into source or docs. Write the port in your own words.

## Layout

| Path | What |
|---|---|
| `src/` | The core: app loop (`app.c`, `flow.c` = App_Frame's states), data (`vfs.c` + ISO 9660, `xp.c` archives, `rnc.c`), `exe.c`, sprites and surfaces, fonts, levels (`level.c`, `objects.c`, `play.c`, `lv1.c`..`lv9.c` per world), flock (`sheep.c`), shepherd (`player.c`, demos), moves, HUD, menus (`menu.c`, `pause.c`), results, high scores, saves, credits, cheats/debug keys, sound mixer, Bink (`bink.c`, `movie.c`), input. Backend: `platform_gasm.c` (the only file that includes `gasm.h`). Test host pieces: `vfs_host.c`, `storage_file.c`, `input_none.c` |
| `tests/` | `play_test.c`, `xp_test.c` (headless, against the real data), `hashes.txt` (`tools/regress.sh`) |
| `tools/` | `fn.sh <addr>` (a function from `re/all.c`), `dis.py` (disassembler), format decoders (`xp.py`, `spr.py`, `blo.py`, `rnc.py`), `bik.c` and `overview.c` (native), `regress.sh`, `fetch-gasm-sdk.sh` / `fetch-gasm-runner.sh`, `ghidra/` scripts |
| `docs/` | `architecture.md`, `formats.md` |

## Build and test

```sh
cmake -S . -B build && cmake --build build                      # native: tests and tools
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENSHEEP_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm                                        # -> build-gasm/opensheep.wasm
```

Sources are globbed: re-run both `cmake -S` lines after adding a `.c` file. Both builds must be free
of warnings.

**Before claiming a change works:**

```sh
OPENSHEEP_DATA=~/Downloads/Sheep.iso ./build/play_test          # "ok"
tools/regress.sh 2>/dev/null | diff - tests/hashes.txt && echo SAME
```

`regress.sh` runs 23 levels headless for 1500 frames and prints their video hashes. A change that
should not alter play must keep them; one that does (a newly ported routine) changes them for a
reason. Check that with screenshots, then re-baseline `tests/hashes.txt` in the same commit.

## Running headless

```sh
RUN=.deps/gasm-runner-macos-universal/gasm-run                # tools/fetch-gasm-runner.sh macos-universal
$RUN build-gasm/opensheep.wasm --asset cd=$HOME/Downloads/Sheep.iso \
  --param "ini=BYPASS_FRONTEND 1;START_WORLD 1;START_LEVEL 1" \
  --headless 300 --input "100-102:KEY(Enter),200-260:KEY(ArrowUp)" --screenshot /tmp/x.png
```

One frame is one original 16 ms tick (62.5 Hz). `--input` ranges are comma-separated
(`FROM-TO:ACTION`); actions include `KEY(Code)`, `MOVE(dx,dy)`, `PTR(x,y,L+R)`, `GP0(A0=1)`.
`--storage-dir <dir>` keeps what the game saves (settings, slots, `Sheep0001.bmp`, recordings);
headless runs otherwise use memory. `--param level=W.L` starts a level with no front end. Never open a
runner window in unattended runs.

## Traps

- The original's main loop plays a movie instead of App_Frame while one is set (`movie.c`); the frame
  that starts one skips its present.
- `level_load_resources` then `level_start` is 0x406cc0 mode 1; `level_regroup` is mode 2 (no reload).
- The pause menu's page routines switch on the item last *picked* (0x4b9104), not the cursor.
- Settings load only on the front-end path; a `level=` start keeps the defaults, which the regression
  hashes depend on.
- `/usr/bin/cc` for one-off native compiles (`cc` may be aliased).
- Run Python helpers with `-P` from the repo root: a stray `dis.py` in the working directory shadows the
  standard library's.
