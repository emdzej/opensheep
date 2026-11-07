# Credits and licences

**OpenSheep** is released under the [GNU General Public License v3.0](https://github.com/emdzej/opensheep/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Sheep** is © 2000 Mind's Eye Productions / Empire Interactive. OpenSheep is not affiliated with them.

## Published and community sources

- **Rob Northen Compression (RNC ProPack, method 2)** — the unpacker ports the published algorithm.

## Reference implementations

- [FFmpeg](https://ffmpeg.org)'s libavcodec (LGPL 2.1+) — the Bink 1 video and audio decoder follows its structure and names. It is
  rewritten, not vendored, and its tables are read from your own `binkw32.dll`.

## Tools

[Ghidra](https://ghidra-sre.org), [Capstone](https://www.capstone-engine.org), AddressSanitizer and UBSan, Pillow, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk), [VitePress](https://vitepress.dev). Packaging, launchers and the release workflow come from OpenRF.

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C SDK, and on this site `@emdzej/gasm-host`. OpenSheep is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/opensheep/issues) — gaps in these credits are bugs.
