---
layout: home

hero:
  name: OpenSheep
  text: Sheep, native again
  tagline: A faithful, from-scratch reimplementation of the 2000 puzzle game, using the data from your own CD. Runs on macOS, Linux, Windows and in your browser through the gasm WebAssembly runtime.
  actions:
    - theme: brand
      text: Get started
      link: /guide/
    - theme: alt
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: How it works
      link: /architecture

features:
  - title: Faithful
    details: The flock, the shepherds, every world's levels, the menus and the results are ported function by function from the original program, quirks included. Each ported routine names its address in Sheep.exe.
  - title: Your original data
    details: The sprites, levels, Bink movies, sound and music come straight from your Sheep CD, and the original program's own tables are read from Sheep.exe on the disc. Nothing is included or converted.
  - title: No emulator
    details: Portable C11 with no DirectX, no Wine and no virtual machine. Its own Bink video decoder and sound mixer stand in for binkw32.dll and DirectSound.
  - title: Runs on gasm
    details: One opensheep.wasm for macOS, Linux, Windows and the browser, on the gasm WebAssembly game runtime. Deterministic, so the same inputs give the same frames and sound on every runner.
    link: /guide/install
    linkText: Installing
  - title: In the browser
    details: Choose your CD folder or disc image once and play in Chrome, Edge, Firefox or Safari, with sound, the mouse, the keyboard and gamepads.
    link: /play/
    linkText: Play
    target: _self
  - title: The whole game
    details: All the worlds and their bonus levels, the four shepherds and four breeds, power-ups, best times, save slots, the credits, the attract demos, and the developers' own debug keys.
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

OpenSheep is one WebAssembly module, `opensheep.wasm`, running on [gasm](https://gasm.emdzej.pl), a
portable game runtime: `gasm-run` on the desktop, or this site's [player](/play/){target="_self"} in the
browser. The same inputs give the same frames and sound everywhere, which is how the port is tested:
23 levels played headless, hash for hash.

</div>
