# Building

You need CMake and a POSIX shell. `tools/fetch-gasm-sdk.sh` downloads wasi-sdk and gasm's C SDK
0.16.0 into `.deps/`.

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENSHEEP_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j      # -> build-gasm/opensheep.wasm
```

## Tests

The native build (macOS or Linux, no libraries) has the headless tests and the tools. They read the
disc from `./cd` or `OPENSHEEP_DATA=<folder or image>`:

```sh
cmake -S . -B build && cmake --build build
OPENSHEEP_DATA=~/Downloads/Sheep.iso ./build/play_test      # prints "ok"
tools/regress.sh | diff - tests/hashes.txt                  # 23 levels' video hashes
```

`regress.sh` plays 23 levels for 1500 frames each with `gasm-run --headless` and prints the hashes
of their video. A change that shouldn't alter the game keeps them.

## Release bundles

```sh
tools/package-gasm.sh <version> <platform> "$(tools/fetch-gasm-runner.sh <platform>)" out/
tools/smoke-gasm-bundle.sh out/opensheep-gasm-<version>-<platform>.zip   # or .tar.gz on Linux
```

The bundle has the module, the released `gasm-run` and a launcher, and no game data. The smoke test
checks it without a CD; `OPENSHEEP_SMOKE_CD=<folder or image>` also plays a level and compares the
hashes. A semver tag (`0.1.0`, no `v`) makes all of them in CI and attaches them to a GitHub release.

## The site

```sh
docs/scripts/copy-wasm.sh                 # build-gasm/opensheep.wasm -> docs/public/play/
cd docs && pnpm install && scripts/vendor-web.sh && pnpm dev
```
