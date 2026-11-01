#!/usr/bin/env bash
# Package opensheep.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENSHEEP_WASM=<file> picks the module
# (default build-gasm/opensheep.wasm). Produces in <out dir>:
#   macos-universal  Sheep (gasm).app, opensheep-gasm-<version>-macos-universal.zip
#   linux-<arch>     opensheep-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   opensheep-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/. No AOT .cwasm: gasm-run --compile only targets the host it runs
# on, so it can't be made for the other platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENSHEEP_WASM:-$ROOT/build-gasm/opensheep.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="opensheep-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" > "$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" > "$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi > "$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER and backslashes are meant literally)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start cd_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open "Sheep (gasm).app". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (or: xattr -dr com.apple.quarantine "Sheep (gasm).app").'
      cd_how='The first time, the app asks for your CD: "Choose CD folder..." for the CD itself, a
mounted disc image (double-click an .iso; it appears under /Volumes) or a folder you copied the CD
to; "Choose disc image..." for a raw .bin (of a .bin/.cue pair) or an .iso file.'
      change='Hold Option while opening the app to choose another CD, or delete
  ~/Library/Application Support/OpenSheep/cd-location
From Terminal: "Sheep (gasm).app/Contents/MacOS/OpenSheep" --help'
      saves='~/Library/Application Support/gasm/opensheep/'
      logs='~/Library/Logs/OpenSheep/gasm.log' ;;
    linux-*)
      start='Run ./opensheep.sh (from a terminal or your file manager). ./opensheep.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and
Ubuntu) and a Vulkan or OpenGL capable graphics driver.'
      cd_how='Give it your CD once: ./opensheep.sh /media/$USER/SHEEP (the CD or a mounted image), a folder
you copied the CD to, or a raw .bin (of a .bin/.cue pair) or an .iso file. Without an argument it
opens a chooser (zenity or kdialog) if one is installed.'
      change='./opensheep.sh --change-cd, or give it another CD as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/opensheep/cd-location
./opensheep.sh --help lists the options; anything after the CD goes to gasm-run.'
      saves='~/.local/share/gasm/opensheep/'
      logs='the terminal, or ~/.local/state/opensheep/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenSheep.cmd. (If Windows SmartScreen warns about gasm-run.exe: More info,
Run anyway.)'
      cd_how='The first time, it asks for your CD: "Choose CD folder..." for the CD drive, a mounted disc
image (right-click an .iso, Mount) or a folder you copied the CD to; "Choose disc image..." for a
raw .bin (of a .bin/.cue pair) or an .iso file. From a command prompt: OpenSheep.cmd D:\'
      change='OpenSheep.cmd --change-cd, or delete %APPDATA%\OpenSheep\cd-location.
OpenSheep.cmd --help lists the options; anything after the CD goes to gasm-run.'
      saves='%APPDATA%\gasm\opensheep\'
      logs='the console window' ;;
  esac
  cat > "$1" <<TXT
OpenSheep $VERSION for gasm ($PLATFORM)
https://opensheep.emdzej.pl

OpenSheep is a faithful reimplementation of Sheep (Mind's Eye Productions / Empire Interactive, 2000). This bundle runs it
as a WebAssembly module (opensheep.wasm) in the gasm runtime: gasm-run $GASM_VERSION is included
(https://gasm.emdzej.pl). It is the same game as the browser player.

You need your own Sheep CD (for Windows), or an image of it: none of the original
game's files are included.

START
$start

YOUR CD
$cd_how
The choice is remembered (the folder or file must still be there next time; a CD or mounted
image has to be inserted or mounted again).

CHANGE THE CD
$change

CONTROLS (the original's defaults; the game's options choose mouse, keyboard or pad)
                    Keyboard        Mouse           Pad
  Move              Arrow keys      Move it         D-pad, left stick
  Run               Left Shift      Left button     Button 1
  Creep             Left Alt        Right button    Button 2
  Act               Space                           Button 3
  Pause menu        Esc                             Button 4
In a level, M, K and J switch to the mouse, the keyboard and a pad. Hold Esc for a second to quit.
Full list: https://opensheep.emdzej.pl/guide/controls

SETTINGS, SAVES AND BEST TIMES
$saves

LOG
$logs

LICENSES
OpenSheep is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. Sheep is (c) Mind's Eye Productions / Empire Interactive.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/Sheep (gasm).app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenSheep"
  chmod +x "$APP/Contents/MacOS/OpenSheep" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/opensheep.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/opensheep.icns"
  cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Sheep (gasm)</string>
  <key>CFBundleDisplayName</key><string>Sheep (gasm)</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.opensheep.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenSheep $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenSheep</string>
  <key>CFBundleIconFile</key><string>opensheep</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.puzzle-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenSheep contributors, GPL-3.0; gasm-run MIT. Sheep is (c) Mind's Eye Productions / Empire Interactive.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$WASM" "$D/"
  [ "$(basename "$WASM")" = opensheep.wasm ] || mv "$D/$(basename "$WASM")" "$D/opensheep.wasm"
  fill "$SRC/opensheep.sh" "$D/opensheep.sh"
  cp "$SRC/opensheep-gasm.desktop" "$D/"
  python3 "$ROOT/tools/icon.py" 256 "$D/opensheep.png" "$ROOT/docs/public/favicon.png"
  chmod 755 "$D/gasm-run" "$D/opensheep.sh"; chmod 644 "$D/opensheep.wasm"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  cp "$WASM" "$D/opensheep.wasm"
  fill "$SRC/OpenSheep.cmd" "$D/OpenSheep.cmd"; fill "$SRC/opensheep.ps1" "$D/opensheep.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenSheep.cmd opensheep.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
