# Sheep data formats (working notes)

Disc: *Sheep* (Mind's Eye Productions / Empire Interactive, Windows, 2000). ISO label `SHEEP`,
502 MiB, built 2001-01-08 (multi-language release with a Polish install: `MiscPl/`). The game is
`Sheep.exe` (PE32 i386, 380,928 bytes, linked 2001-01-08, image base `0x400000`, MSVC C++,
DirectDraw / DirectSound / DirectInput, Bink via `binkw32.dll`). Everything else on the root
(`Setup.exe`, `_*.cab`, `directx.*`, `InstallCheck.exe`) is the InstallShield / DirectX installer.

Status legend: **done** = decoded and verified by a tool in `tools/`; **partial** = layout
mostly known; **TBD** = needs the reader found in `Sheep.exe`.

## Disc layout

| Path | What |
|---|---|
| `Sheep.exe` | The game |
| `main.xp` (35 MB) | Front end, GUI, shared sprites, players, sheep, fonts, strings (8 languages) |
| `Sound.xp` (32 MB) | 1003 sound effects (WAV) |
| `world00.xp` | Menu level (`MENULEV`), shown behind the front end |
| `world01..06.xp` | 4 levels each + one bonus level (`BONUS1`, `SACKRACE`, `BONUS3`, `BONUS4`, `RACECARS`, `DODGEMS`) |
| `world07.xp` | Final level (`W07LEVEL01`) |
| `world09.xp` | Training levels (`W09LEVEL01..04`) |
| `Data/Biks/*.bik` | Bink movies: `Intro`, `Logos`, `World01..07` (world intros), character bios (`Adam`, `BoPeep`, `Motley`, `shep`, `Factoral`, `LongWool`, `NeoGen`, `pastoral`) |
| `Data/Sound/Music/*.wav` | Music, PCM 22050 Hz stereo 16-bit: front end, training, credits, one per world (`w06_*` = four tracks for world 6) |
| `Misc{Uk,Fr,Gm,It,Sp,Pl}/` | Per-language `HiScore.tab` (default table, 2048 bytes, obfuscated) and `ReadMe.txt` |

Paths the exe opens (strings): `World%02d.xp`, `sound.xp`, `main.xp`, `Sheep.ini`,
`Saves\Game%02d.sav`, `HiScore.tab`, `data\biks\%s.bik`, `data\local\GameStrings%s`,
`data\misc\%s.fnt`, `data\misc\%s.slf`, `data\screenbits\...`. Paths under `data\` are resolved
inside the `.xp` archives (the archive tree starts at `DATA/`).

## `.xp` archive — done (`tools/xp.py`)

No header; data first, directory last.

- **Footer** (last 16 bytes): `u32 dir_off`, `u32 dir_len`, `"XCELZLIB"` (the name is historical:
  the data is RNC-packed, not zlib).
- **Directory** at `dir_off`, offsets inside it relative to `dir_off`:
  - *listing*: `u32 first_node, u32 count, name, NUL`. The root listing sits at 0 and is named `ROOT`.
  - *node*: `u32 data_off, u32 node_size, u32 stored, u32 unpacked, u32 next_node, u8 flags,
    u32 name_len, name, NUL`. `node_size` = 25 + name_len + 1.
  - Directory node: `stored == 0xFFFFFFFF`, `unpacked` = offset of its listing, `next_node` = sibling.
    Every listing starts with a `..` node.
  - File node: `data_off` absolute in the `.xp`; `flags & 1` = RNC-packed (`stored` packed bytes,
    `unpacked` after), else stored raw. Names are upper-case 8.3-ish (`ASOUNDOBJECT.SPR`).

## RNC ProPack method 2 — done (`tools/rnc.py`)

Classic Rob Northen compressor. 18-byte header, big-endian: `"RNC" 0x02`, `u32 unpacked`,
`u32 packed`, `u16 crc(unpacked)`, `u16 crc(packed)`, `u8 leeway`, `u8 chunks`. CRC-16 is the
reflected 0xA001 polynomial. Method 2 bit stream (MSB-first, interleaved with literal bytes):
literals, short/long LZ matches, and raw literal runs of `12 + 4n` bytes. Verified on all 2,465
packed files (CRC match).

## Sprites `.SPR` — partial

Header (44 bytes + table), little-endian:

| Off | Type | |
|---|---|---|
| 0 | u32 | frame count |
| 4 | u32 | file size |
| 8 | u32 | 0 (2 in a few GUI sprites: flags?) |
| 12 | char[32] | name, NUL then `0xCD` fill (MSVC debug heap: memory written straight to disk) |
| 44 | u32 | offset of frame 0 (= 44 + 8 × frames) |
| 48 | {u16 group, u16 index, u32 next_offset} × frames | `group` = frames per direction (e.g. 8) |

Frame: `u32 size, u32 width, u32 height, i32 hot_x, i32 hot_y`, then coded pixels. The codec is
byte oriented with transparent runs (`0xFF`-heavy in mostly transparent frames); colour format
is 16-bit (the exe checks for "15 Bit" / "16 Bit Graphics card" and converts with `GetRGB16`).
**TBD:** the run codes and whether pixels are 565 indices, a per-sprite palette, or literal
16-bit words. Names encode the object: `C1SH_RUN_` (Shep running), `C2PA_*` (Pastoral sheep),
`C3*` world props; a `200` suffix is a 2× variant.

## Level files (one directory per level, e.g. `DATA/WORLD01/W01LEVEL01/`)

| File | Status | What |
|---|---|---|
| `.BLO` | done | The level proper, stored raw (1.6-5.5 MB), read by `Level_LoadBLO` 0x4072d0; decoder `tools/blo.py`. Header: `"PC"`, u8 version (>= 13) at +2, u32 width in cells (128) +4, height (120) +8, tile count +0x10, counts A +0x14, B +0x18, C +0x1c, string bytes +0x24. Body from 0x38: strings (artists' source paths), C x 32 bytes `{u8 x4, char name[28]}` (tile-animation frames), B x 20 (object placements, from index 31: `{i16 x, y, spr, depth; u8 mirror, sortable, layer, mode; i16 id; u8 push}`, sprite = bank spr + 100), A x 256 (sprite strip paths), A x 4, map W*H u16 (`& 0x3fff` = tile number via the tile-animation remap, 0 = empty), tiles N x 32x24 rgb565, layer1 W*H u16, layer2 W*H u32 (low byte = cell type; the map renderer 0x407ba0 skips types 1 and 2), layer3 W*H u32, then 0x30000 and 0x9000 bytes of tables; that is the end of the file (checked on all 36 levels). Cells are 32x24 px, so a level is 4096x2880 px. |
| `.TGA` | done | 640×480 24-bit RLE TGA: the overview map (Tab) |
| `.TGB` | done | 640×480 32-bit RLE TGA: the level briefing picture |
| `.TIM` | partial | 3 × u32: par times for the gold/silver/bronze ratings (e.g. 28800, 10800, 5400; unit TBD) |
| `.SLF` | done | Text sound list: `NNN Name \t\t data\sound\W01Level01\File` (ids → WAV in `Sound.xp`) |
| `.POS` | done | Animated-object placements, read by 0x402a90: a 0x38-byte header (u32 count first), then count x 56-byte records `{i32 id, u32 flags (bit 31 = shadow), ?, dir, countdown, move, i32 param[8]}`; `id` matches a BLO table-B placement |
| `.NPF` | TBD | Offset table + small records (paths / waypoints?) |
| `.SPC` | TBD | 4608 bytes in every level: fixed-size table (special cells?) |
| `.ANM` | done | Animations (0x401240; also `Data\Players\Player.anm`, `Data\Sheep\Sheep.anm`, `Data\GUI\Pickup.anm`): `{u32 count, u32 size, u32 offsets[count]}`; an animation is `{u32 nframes, ?, u32 frame offsets[]}`; a frame is a command list: 1/4 = sprite (bank, +0x100 for 4; 9 direction frames, pause byte at +11; 12 bytes; direction >= 9 is drawn mirrored as 16 - dir), 2 = loop mode (2 bytes), 3 = sound (mode 2 draws `crt_rand`; 9 bytes), 0 = end |
| `.REC` | TBD | Demo recordings (attract mode, "Load A Recording", "RECORD"). Not every level has one. Usable as a replay oracle for the simulation |

## Other files

| File | Status | What |
|---|---|---|
| `DATA/LOCAL/*.TX?` | done | UTF-16LE text, `NN text` lines, `//` comments. Extension = language: `E` English, `F` French, `G` German, `I` Italian, `S` Spanish, `P` Polish, `R` Russian, `C` Chinese (Traditional). Files: `GAMESTRINGS`, `CREDITS`, `README`, `INSTALL STRINGS`, one per level (briefings) |
| `DATA/MISC/*.FNT` | TBD | Bitmap fonts (`ARIEL12/18` in `ENG`/`FULL`/`RUSIAN` sets, `CS16`) |
| `DATA/MISC/*.TXT`, `*.SLF` | partial | Per-character text and sound lists |
| `*.BMP` | done | Plain 640×480 24-bit BMPs (credits, high score, loading, prologue, world select, end splashes) |
| `*.WAV` | done | RIFF PCM 16-bit; effects mostly 22050 Hz mono (also 11025/44100, a few stereo) |
| `*.bik` | TBD | Bink 1, revision `i` (`BIKi`), 640×480, 25 fps, one audio track. Needs an own decoder |
| `HiScore.tab` | TBD | 2048 bytes, obfuscated |
| `Saves\Game%02d.sav`, `Sheep.ini` | TBD | Written by the game; no sample on the disc |

## Engine hints from the exe

- 640×480, 16-bit DirectDraw (15/16-bit cards), windowed or fullscreen.
- C++ classes (`CInput::Init`, draw manager, surface manager); `Debug.Log` output.
- Input: keyboard, mouse, joystick with force feedback; `M`/`K`/`J` switch control scheme.
- Flock tuning strings ("Distance that sheep will wander in amble mode", "Max distance (pixels)
  that a sheep can be considered a neighbour", ...), so the sheep AI is distance-based in pixels.
- Demo record/playback buffers (`Record Buffer 1..4`).
- Polish strings have a missing glyph (`NACIŚNIJ` is stored as `NACINIJ`): keep as is.
