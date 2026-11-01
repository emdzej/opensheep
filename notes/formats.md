# Sheep data formats

Disc: *Sheep* (Mind's Eye Productions / Empire Interactive, Windows, 2000). ISO label `SHEEP`,
502 MiB, built 2001-01-08 (multi-language release with a Polish install: `MiscPl/`). The game is
`Sheep.exe` (PE32 i386, 380,928 bytes, linked 2001-01-08, image base `0x400000`, MSVC C++,
DirectDraw / DirectSound / DirectInput, Bink via `binkw32.dll`). Everything else on the root
(`Setup.exe`, `_*.cab`, `directx.*`, `InstallCheck.exe`) is the InstallShield / DirectX installer.

Every format below is read by the port; the section says where. The Python decoders in `tools/`
(`xp.py`, `rnc.py`, `spr.py`, `blo.py`) were the first readers and still work as inspection aids.

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

## `.xp` archive (`src/xp.c`, `tools/xp.py`)

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

## RNC ProPack method 2 (`src/rnc.c`, `tools/rnc.py`)

Classic Rob Northen compressor. 18-byte header, big-endian: `"RNC" 0x02`, `u32 unpacked`,
`u32 packed`, `u16 crc(unpacked)`, `u16 crc(packed)`, `u8 leeway`, `u8 chunks`. CRC-16 is the
reflected 0xA001 polynomial. Method 2 bit stream (MSB-first, interleaved with literal bytes):
literals, short/long LZ matches, and raw literal runs of `12 + 4n` bytes. Verified on all 2,465
packed files (CRC match).

## Sprites `.SPR` (`src/sprites.c`, `tools/spr.py`)

Header (44 bytes + table), little-endian:

| Off | Type | |
|---|---|---|
| 0 | u32 | frame count |
| 4 | u32 | file size |
| 8 | u32 | 0 (2 in a few GUI sprites) |
| 12 | char[32] | name, NUL then `0xCD` fill (MSVC debug heap: memory written straight to disk) |
| 44 | {u32 offset, u16 group, u16 index} × frames | `group` = frames per direction (e.g. 8) |

Frame: `u32 size, i32 width, height, hot_x, hot_y`, then one run list per row, each ended by `0x00`:

| Byte | Then |
|---|---|
| `0x80..0xff` | skip `256 - b` pixels |
| `0x01..0x3f` | `b` pixels of `{u8 alpha, u16 rgb565}`, alpha 1..4 = 25/50/75/100 % |
| `0x40..0x7f` | `b - 0x3f` opaque `u16 rgb565` pixels |

`Sprite_Draw` (0x41f5e0) has plain, clipped, mirrored and scaled variants (0x420320) and a sheared
shadow pass (0x4197b0). Names encode the object: `C1SH_RUN_` (Shep running), `C2PA_*` (Pastoral
sheep), `C3*` world props; a `200` suffix is a 2× variant.

## Level files (one directory per level, e.g. `DATA/WORLD01/W01LEVEL01/`)

| File | Read by | What |
|---|---|---|
| `.BLO` | `src/level.c` | The level proper, stored raw (1.6-5.5 MB), read by `Level_LoadBLO` 0x4072d0; decoder `tools/blo.py`. Header: `"PC"`, u8 version (>= 13) at +2, u32 width in cells (128) +4, height (120) +8, tile count +0x10, counts A +0x14, B +0x18, C +0x1c, string bytes +0x24. Body from 0x38: strings (artists' source paths), C x 32 bytes `{u8 x4, char name[28]}` (tile-animation frames), B x 20 (object placements, from index 31: `{i16 x, y, spr, depth; u8 mirror, sortable, layer, mode; i16 id; u8 push}`, sprite = bank spr + 100), A x 256 (sprite strip paths), A x 4, map W*H u16 (`& 0x3fff` = tile number via the tile-animation remap, 0 = empty), tiles N x 32x24 rgb565, layer1 W*H u16, layer2 W*H u32 (low byte = cell type; the map renderer 0x407ba0 skips types 1 and 2), layer3 W*H u32, then 0x30000 and 0x9000 bytes of tables; that is the end of the file (checked on all 36 levels). Cells are 32x24 px, so a level is 4096x2880 px. |
| `.TGA` | `src/pause.c` | 640×480 24-bit RLE TGA: the overview map (Tab) |
| `.TGB` | `src/flow.c` | 640×480 32-bit RLE TGA: the level briefing picture |
| `.TIM` | `src/play.c` | 3 × u32; only +8 is read (0x406c00): the hard time limit in 16 ms ticks (7200 without the file). Normal is twice that, easy normal + 18000 |
| `.SLF` | `src/sound.c` | Text sound list: `NNN Name \t\t data\sound\W01Level01\File` (ids → WAV in `Sound.xp`) |
| `.POS` | `src/objects.c` | Animated-object placements, read by 0x402a90: a 0x38-byte header (u32 count first), then count x 56-byte records `{i32 id, u32 flags (bit 31 = shadow), ?, dir, countdown, move, i32 param[8]}`; `id` matches a BLO table-B placement |
| `.NPF` | `src/paths.c` | Paths for scripted movers (0x411b40): from +8 a u32 offset per path, each path 8-byte points `{i16 flag, i16 ticks, i16 x, i16 y}` |
| `.SPC` | `src/game.c` | Special cells (part of 0x401950): 256 records of 18 bytes `{i8 type, u8 busy, i16 a[7], u8 count, u8}`, the fields' meaning per type; older files have 8 or 16 bytes per record |
| `.ANM` | `src/objects.c` | Animations (0x401240; also `Data\Players\Player.anm`, `Data\Sheep\Sheep.anm`, `Data\GUI\Pickup.anm`): `{u32 count, u32 size, u32 offsets[count]}`; an animation is `{u32 nframes, ?, u32 frame offsets[]}`; a frame is a command list: 1/4 = sprite (bank, +0x100 for 4; 9 direction frames, pause byte at +11; 12 bytes; direction >= 9 is drawn mirrored as 16 - dir), 2 = loop mode (2 bytes), 3 = sound (mode 2 draws `crt_rand`; 9 bytes), 0 = end |
| `.REC` | `src/player.c` | Demo recordings (attract mode, the demos page, `RECORD`; 0x40fb40 / 0x40f990): four u32 run counts, then four run-length streams of `{u16 value, i16 ticks}`: buttons, input x, input y and magnitude. Not every level has one |

## Other files

| File | Read by | What |
|---|---|---|
| `DATA/LOCAL/*.TX?` | `src/hud.c` | UTF-16LE text, `NN text` lines, `//` comments. Extension = language: `E` English, `F` French, `G` German, `I` Italian, `S` Spanish, `P` Polish, `R` Russian, `C` Chinese (Traditional). Files: `GAMESTRINGS`, `CREDITS`, `README`, `INSTALL STRINGS`, one per level (briefings) |
| `DATA/MISC/*.FNT` | `src/font.c` | Bitmap fonts (`ARIEL12/18` in `ENG`/`FULL`/`RUSIAN` sets, `CS16`; 0x404880). Header: +0 size, +0x18 cell width, +0x1c line height, +0x2c range count; ranges `{u16 first, u16 last, u32 size, u32 glyph offsets[]}`; a glyph is `w, h, x off, y off, flags` and its coverage run-length coded over the box (`0 n` clear, `0xff n` solid, any other byte one pixel of that coverage) |
| `DATA/MISC/*.TXT` | `src/game.c` | The shepherds' and breeds' tuning (0x4180e0, 0x41e960): a line starting with a variable's name (the names are the exe's strings), then its number; the developers' pages write a breed's back |
| `*.BMP` | `src/surface.c` | Plain 640×480 24-bit BMPs (credits, high score, loading, prologue, world select, end splashes) |
| `*.WAV` | `src/sound.c` | RIFF PCM 16-bit; effects mostly 22050 Hz mono (also 11025/44100, a few stereo) |
| `*.bik` | `src/bink.c` | Bink 1, revision `i` (`BIKi`), 640×480, 25 fps, one audio track. Own decoder, following FFmpeg's as the reference; its tables are read from the disc's `binkw32.dll` |
| `HiScore.tab` | `src/hiscore.c` | 0x800 bytes (0x408850 / 0x4088b0): 60 entries of 0x20 `{u8 character, s32 score at +4, s32 time at +8, UTF-16 name[10] at +0xc}` (0-9 the games, then the best of each level), a checksum byte (the sum of the plain table). Stored byte `i` is the plain one minus `"KevDos Rules OK!"[(0x780 - i) & 15]`. Without it the defaults (0x40ef50) draw 60 `rand`s |
| `Saves\Game%02d.sav` | `src/save.c` | 0x44 bytes: +0 version 5, +1 the name, +0x24 the game in progress (0x407700, 0x427550) |
| `Sheep.ini` | `src/cheats.c` | Text, one `OPTION value` per line (0x422650); not on the retail disc |

## Engine hints from the exe

- 640×480, 16-bit DirectDraw (15/16-bit cards), windowed or fullscreen.
- C++ classes (`CInput::Init`, draw manager, surface manager); `Debug.Log` output.
- Input: keyboard, mouse, joystick with force feedback; `M`/`K`/`J` switch control scheme.
- Flock tuning strings ("Distance that sheep will wander in amble mode", "Max distance (pixels)
  that a sheep can be considered a neighbour", ...), so the sheep AI is distance-based in pixels.
- Demo record/playback buffers (`Record Buffer 1..4`).
- Polish strings have a missing glyph (`NACIŚNIJ` is stored as `NACINIJ`): keep as is.
