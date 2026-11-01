# OpenSheep: the plan, closed

The plan was a faithful C11 port of **Sheep** (Mind's Eye Productions / Empire Interactive, 2000),
function by function from `Sheep.exe`, shipped as `opensheep.wasm` for [gasm](https://gasm.emdzej.pl)
0.16.0, with a site at [opensheep.emdzej.pl](https://opensheep.emdzej.pl). It is done; the original
plan is in the git history. The rules it set are in [AGENTS.md](AGENTS.md), the findings in
[notes/architecture.md](notes/architecture.md) and [notes/formats.md](notes/formats.md).

## What was built

| Phase | Result |
|---|---|
| Scaffold | CMake (native: tests and tools; gasm: the game), CI, the gasm SDK and runner fetchers |
| Reverse engineering | The Ghidra project and its scripts (`tools/ghidra/`), `tools/fn.sh`, `tools/dis.py` |
| Formats | Every file the game reads: `.xp`, RNC, SPR, BLO, POS, ANM, NPF, SPC, TIM, SLF, REC, FNT, the texts, `HiScore.tab`, saves, `Sheep.ini`, Bink 1 video and audio |
| Core | ISO 9660 and archives, surfaces and sprites, fonts, the sound mixer, the Bink decoder, input; `exe.c` reads the program's tables at run time |
| Front end | Logos and intro movies, the menus over the menu level, shepherd, breed, controller, difficulty, save slots, best times, credits, attract demos |
| Gameplay | All 35 levels (six worlds of four plus a bonus game, the final level, four training levels) with their own routines; the flock, four shepherds, four breeds, power-ups, results; the developers' keys |
| Release | Bundles for macOS, Linux and Windows (`tools/package-gasm.sh`), the browser player, the site |

Two of the open questions were settled: the language follows the original (`Sheep.ini`'s
`LANGUAGE`, English by default) and the repository is `emdzej/opensheep`.

## What is left

- **The first release**: `git tag 0.1.0 && git push origin 0.1.0` runs `release.yml`.
- **Play it through.** Every level runs headless with no `not ported` hook or move routine in
  3000 frames, but routines are found at run time, so a path no run took can still meet one.
- **Compare with the original** side by side (timings, the flock, sounds), on a machine that still
  runs it.
- **The regression run covers 23 of the 35 levels**; 1.2, 1.3, 2.2, 2.3, 3.2-3.4, 4.2-4.4, 5.1 and
  5.2 aren't in `tools/regress.sh`.
- **Languages past the seventh** need fonts that aren't on the disc.
- **Linux and Windows bundles** are built by CI but have only been smoke-tested on macOS.
- **The installed `HiScore.tab`**: the disc's `Misc*` folders hold a default table per language,
  which the installer presumably copied next to the game. OpenSheep doesn't read them and makes the
  program's defaults (0x40ef50), which draw 60 `rand`s. Worth checking against an installed copy.
