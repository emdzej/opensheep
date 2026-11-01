# Developer options

## Launch parameters

Pass with `--param name=value` to `gasm-run`, or as URL query parameters to the
[player](/play/){target="_self"} (`/play/?level=3.2`).

| Parameter | What |
|---|---|
| `level=<world>.<level>` | The bare level, e.g. `level=3.2`: play and drawing only, with no front end, pause menu or results (the regression tests' harness; PageUp / PageDown step through the levels). For the real thing, `ini=BYPASS_FRONTEND 1;START_WORLD 3;START_LEVEL 2` |
| `ini=<lines>` | A `Sheep.ini`, its lines separated by `;` (the retail disc has none) |
| `view=1` | The level viewer |
| `trace=1` | Log the app's states |

## Sheep.ini

The original read a `Sheep.ini` next to the program; the retail game never shipped one. Its
options:

| Option | What |
|---|---|
| `BYPASS_FRONTEND 1` | Skip the front end, start at `START_WORLD` / `START_LEVEL` |
| `CONTROL_TYPE n` | 0 mouse, 1 keyboard, 2 analog pad, 3 digital pad |
| `LANGUAGE n` | The texts' language |
| `SOUND_FX 0` | No sound effects |
| `MONO 1` | Mono sound |
| `DISSABLE_TIMER 1` | No level clock (sic) |
| `WORLD_CHEAT 1` | Every world open |
| `OUR_MENU 1` | The developers' keys, below |
| `RECORD 1` | Demo recording from the pause menu |

```sh
gasm-run opensheep.wasm --asset cd=Sheep.iso --param "ini=BYPASS_FRONTEND 1;START_WORLD 2;START_LEVEL 3"
```

## The developers' keys

With `OUR_MENU 1`:

- **P** saves a screenshot, `Sheep0001.bmp` and on, into the storage.
- **I** draws the font chart into `MyFont.bmp`.
- **V** and **C** show the version and the level's theme and the shepherd's cell.
- The pause menu gets the developers' pages: another shepherd, another breed (its tuning is written
  back to the storage, as the original wrote its text files), and with `RECORD 1` demo recording
  and playback (`Recording.rec`).
- When the actors are frozen, the shepherd roams the level freely; creep drops the flock around it.

`--storage-dir <dir>` on `gasm-run` keeps what the game saves as files in a folder.
