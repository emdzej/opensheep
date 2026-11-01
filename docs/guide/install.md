# Installing

OpenSheep is one file, `opensheep.wasm`. It runs on [gasm](https://gasm.emdzej.pl)'s runner,
`gasm-run`, on macOS, Linux and Windows, or in the browser on this site's [player](/play/){target="_self"}.

## The runner

Download `gasm-run` 0.16.0 or newer for your system from
[gasm's releases](https://github.com/emdzej/gasm/releases). From a checkout of OpenSheep,
`tools/fetch-gasm-runner.sh <platform>` does it for you and prints the folder it unpacked to
(platforms: `macos-universal`, `linux-x86_64`, `linux-arm64`, `windows-x86_64`).

## The module

Take `opensheep.wasm` from the latest CI run's `opensheep-wasm` artifact, or
[build it](/guide/build).

## Running

```sh
gasm-run opensheep.wasm --asset cd=Sheep.iso          # a disc image
gasm-run opensheep.wasm --asset-dir /Volumes/SHEEP    # the disc, or a folder copy of it
```

See [Game data](/guide/game-data) for what the disc has to be. The settings, save slots and best
times are kept in the runner's storage for the module.
