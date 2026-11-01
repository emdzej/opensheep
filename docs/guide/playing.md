# Playing

The game starts with its logos and the intro movie, then the front end: a new
game, loading a saved one, the options, the best times and the credits.

- **A game** goes world by world. Each world has its levels and a bonus game; the results screen
  after a level counts the sheep you saved and the time you took.
- **Shepherds and breeds.** You pick one of the four shepherds, each with their own speed and
  pull on the flock, and the sheep come in four breeds that behave differently.
- **Power-ups** turn up in the levels and change the rules for a while.
- **Pause** with Esc: continue the game, replay the level or quit it.

## Saves and settings

What the original kept in the registry and in files next to the game is kept in the runner's
storage for `opensheep.wasm` (in the browser, the site's storage):

- the options (difficulty, controller, sound and music volumes),
- the four save slots,
- the best times per level,
- demo recordings and screenshots, when the [developer options](/guide/developer) are on.

## Languages

The CD has the game's texts in several languages; `--param "ini=LANGUAGE <n>"` picks one. Languages
past the seventh need fonts the disc doesn't have.
