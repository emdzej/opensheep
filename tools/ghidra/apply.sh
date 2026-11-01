#!/usr/bin/env bash
# Apply names.txt to the Ghidra project and regenerate re/all.c.
set -euo pipefail
cd "$(dirname "$0")/../.."
ghidra script run tools/ghidra/ApplyNames.java --project sheep --program sheep_game.exe -- "$PWD/tools/ghidra/names.txt" >/dev/null
ghidra script run tools/ghidra/DumpAllNamed.java --project sheep --program sheep_game.exe -- "$PWD/re/all.c" >/dev/null
grep -c '^// ====' re/all.c
