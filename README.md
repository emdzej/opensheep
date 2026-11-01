# OpenSheep — Sheep, native again

**Site: [opensheep.emdzej.pl](https://opensheep.emdzej.pl)**

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

Sheep is a 2000 puzzle game by Mind's Eye Productions and Empire Interactive. You play a shepherd who
herds a flock across farms, a factory, a space station and a few odder places. It's a lovely game, and
it no longer runs well on anything modern: DirectDraw, DirectInput, Bink movies and a registry full of
settings. So I ported it, function by function, the way I did [OpenRF](https://github.com/emdzej/openrf).

OpenSheep is a from-scratch, portable reimplementation of the Windows game in C11. It runs as
`opensheep.wasm`, a module for the [gasm](https://gasm.emdzej.pl) WebAssembly game runtime (0.16.0),
so it plays on macOS, Linux, Windows and in the browser. It contains no original code, assets or
data. Everything comes from your own Sheep CD: the files on it, and the tables of the original
program (`Sheep.exe`), which are read straight from the disc at run time.

## Running

You need your own Sheep CD, as the disc, a folder copy or an `.iso` image. The
[releases](https://github.com/emdzej/opensheep/releases) have bundles for macOS, Linux and Windows
(the module, `gasm-run` and a launcher that asks for the CD once); by hand:

```sh
gasm-run opensheep.wasm --asset cd=Sheep.iso          # an image
gasm-run opensheep.wasm --asset-dir /Volumes/SHEEP    # the disc or a folder copy
```

Or play in the browser at [opensheep.emdzej.pl/play](https://opensheep.emdzej.pl/play/), with the same
module and your CD folder or image.

`tools/fetch-gasm-runner.sh <platform>` downloads the released `gasm-run` into `.deps/` and prints its
folder (platforms: `macos-universal`, `linux-x86_64`, `linux-arm64`, `windows-x86_64`).

Launch parameters (`--param name=value`; in the browser, URL query parameters):

| Parameter | What |
|---|---|
| `level=<world>.<level>` | the bare level, e.g. `level=3.2`: play and drawing only (no menus; the tests' harness) |
| `ini=<lines>` | a `Sheep.ini`, lines separated by `;` (the retail disc has none) |
| `view=1` | the level viewer |
| `trace=1` | log the app states |

The `Sheep.ini` options are the original's: `BYPASS_FRONTEND`, `START_WORLD`, `START_LEVEL`,
`CONTROL_TYPE` (0 mouse, 1 keyboard, 2 analog pad, 3 digital pad), `LANGUAGE`, `SOUND_FX`, `MONO`,
`DISSABLE_TIMER` (sic), `WORLD_CHEAT`, `OUR_MENU` (the developers' keys) and `RECORD` (demo recording).
For example `--param "ini=BYPASS_FRONTEND 1;START_WORLD 2;START_LEVEL 3"`.

Settings (difficulty, controller, volumes), the four save slots, the best times and demo recordings
are kept in the runner's storage, where the original used the registry and files next to the game.

## Controls

The original defaults: arrows move the shepherd, Left Shift runs, Left Alt creeps, Space acts, Esc
opens the pause menu. With the mouse, its motion steers the shepherd (left button runs, right button
creeps); a gamepad works as an analog or digital pad (buttons 1 and 2 run and creep, 3 acts, 4
pauses). M, K and J switch to mouse, keyboard and pad in a level.

## Build

The game (needs CMake; `tools/fetch-gasm-sdk.sh` downloads wasi-sdk and gasm's C SDK 0.16.0 into `.deps/`):

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENSHEEP_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j      # -> build-gasm/opensheep.wasm
```

The native build (`cmake -S . -B build && cmake --build build`, macOS or Linux, no libraries) has the
headless tests and tools only. They read the disc from `./cd` or `OPENSHEEP_DATA=<folder or image>`:

```sh
OPENSHEEP_DATA=~/Downloads/Sheep.iso ./build/play_test      # prints "ok"
tools/regress.sh | diff - tests/hashes.txt                  # 23 levels' video hashes, headless
```

## Status

The whole game is ported: the front end and its menus, every world's levels with their own
routines, the flock, the shepherds, power-ups, results, high scores, saves, the credits, the
attract demos, Bink movies (own decoder), sound and music, mouse, keyboard and pads, and the
developers' debug keys. Languages past the seventh need fonts the disc doesn't have.

It hasn't been played through end to end yet, nor compared with the original side by side;
[PLAN.md](PLAN.md) lists what is left.

## Layout

- `src/` — the engine (C11): a portable core and the gasm backend (`platform_gasm.c`, the only file
  that includes `gasm.h`). Every ported function cites its address in `sheep_game.exe`.
- `tests/` — headless tests against the real data; `tests/hashes.txt` the regression hashes.
- `tools/` — format decoders and development aids (Python, and `bik` / `overview` in C).
- `PLAN.md` — what was built, and what is left.
- `docs/` — the site (VitePress, [opensheep.emdzej.pl](https://opensheep.emdzej.pl)): the user guide, the
  browser player (`docs/public/play/`) and the reverse-engineering notes, `architecture.md` and `formats.md`.

## License

OpenSheep is free software under the [GNU GPL v3](LICENSE). The Bink decoder follows FFmpeg's
(LGPL 2.1+) as the format's reference. Sheep is © 2000 Mind's Eye Productions / Empire Interactive;
OpenSheep contains none of its code or assets — you need your own copy of the game.
