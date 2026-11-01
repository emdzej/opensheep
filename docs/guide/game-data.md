# Game data

OpenSheep needs your own **Sheep CD for Windows** (Mind's Eye / Empire, 2000). It reads it in
place; nothing is converted or installed.

## What it reads

| On the disc | What |
|---|---|
| `Sheep.exe` | The original program. OpenSheep checks it is the supported version (380,928 bytes, CRC-32 `b7bcd581`) and reads its tables: levels, objects, menu pages, strings, fonts, palettes and constants. None of its code runs. |
| `binkw32.dll` | The Bink decoder's tables (OpenSheep has its own decoder). |
| `main.xp`, `sound.xp`, `world00.xp` ... `world09.xp` | The archives: sprites, levels, sounds, per world. |
| `Data/` | The Bink movies (`Data/Biks`) and the music. |
| `Misc*` | The installer's high-score tables and read-me, per language. Not read: OpenSheep starts with the default table the program makes itself. |

The installer, DirectX and the setup files on the disc are not used. All of it together is about
490 MB.

## Handing it over

- **A disc image** (`.iso`, or a raw MODE1/2352 `.bin`): `--asset cd=Sheep.iso`. For a `.cue` and
  `.bin` pair, pass the `.bin`.
- **The disc itself, or a folder copy**: `--asset-dir /Volumes/SHEEP` (on Windows, `--asset-dir D:\`).
- **In the browser**: the [player](/play/){target="_self"} asks for the folder or the image and can
  copy it into the browser's private storage for the site, so later visits start at once.

If something is missing or the program is another version, the game says what it found and stops.
