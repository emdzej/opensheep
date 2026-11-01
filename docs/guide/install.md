# Installing

OpenSheep is one file, `opensheep.wasm`. It runs on [gasm](https://gasm.emdzej.pl)'s runner,
`gasm-run`, on macOS, Linux and Windows, or in the browser on this site's [player](/play/){target="_self"}.

## A bundle

The easy way: a release bundle from [GitHub](https://github.com/emdzej/opensheep/releases), with the
module, `gasm-run` and a launcher that asks for your CD once and remembers it.

| System | Bundle | Start |
|---|---|---|
| macOS (Apple Silicon and Intel) | `opensheep-gasm-<version>-macos-universal.zip` | `Sheep (gasm).app`. It is signed ad hoc, not notarized: the first time, right-click it and choose Open. Hold Option while opening it to pick another CD. |
| Linux x86_64 / arm64 | `opensheep-gasm-<version>-linux-<arch>.tar.gz` | `./opensheep.sh` (`--install-desktop` adds a menu entry). Needs ALSA and a Vulkan or OpenGL driver. |
| Windows x86_64 | `opensheep-gasm-<version>-windows-x86_64.zip` | `OpenSheep.cmd` |

Each launcher takes `--help`; anything after the CD goes to `gasm-run`. The README inside says
where the saves and the log are.

## By hand

### The runner

Download `gasm-run` 0.16.0 or newer for your system from
[gasm's releases](https://github.com/emdzej/gasm/releases). From a checkout of OpenSheep,
`tools/fetch-gasm-runner.sh <platform>` does it for you and prints the folder it unpacked to
(platforms: `macos-universal`, `linux-x86_64`, `linux-arm64`, `windows-x86_64`).

### The module

`opensheep-<version>.wasm` is attached to every release; or [build it](/guide/build).

### Running

```sh
gasm-run opensheep.wasm --asset cd=Sheep.iso          # a disc image
gasm-run opensheep.wasm --asset-dir /Volumes/SHEEP    # the disc, or a folder copy of it
```

See [Game data](/guide/game-data) for what the disc has to be. The settings, save slots and best
times are kept in the runner's storage for the module.
