#!/usr/bin/env bash
# Makes a self-contained Windows folder of the Cretaceous player:
#
#   ocean/cretaceous/tools/package_windows.sh [DEST]      (default /mnt/d/cretaceous_win = D:\cretaceous_win)
#
# It holds cretaceous.exe (tools/build_windows.sh; built first if missing) and
# the files the game loads, at the repository-relative paths it opens them by
# (it runs with the folder as its working directory):
#   ocean/cretaceous/shaders, config/water.ini, assets (textures, foliage,
#   props, dinos, audio and their CREDITS/SOURCES), and the four renderer
#   sources whose bytes key the texture cache (render/view.c, water_flow.h,
#   veg.c, veg.h; without them the keys fall back to the build time and every
#   new exe rebuilds the cache);
#   build/cretaceous: the baked world of the default seed and its horizon
#   map, the cloud noise and the derived-data cache. All are little-endian
#   arrays of fixed-width fields and floats, so the Linux files load as they
#   are; any item whose key does not match is rebuilt by the exe on its first
#   start and written back into the folder.
# Plus Play Cretaceous.bat, Host.bat, Join.bat, Gallery.bat, Scenario.bat, README.txt and CREDITS.md.
#
# The assets are gitignored, so they come from CRET_ASSETS (default: this
# checkout's ocean/cretaceous/assets) and the caches from CRET_BUILD (default:
# this checkout's build/cretaceous). The game files are mirrored (stale ones
# removed); cache items already in DEST are kept, since the exe keeps them current.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
DEST=${1:-/mnt/d/cretaceous_win}
EXE=${CRET_WIN_EXE:-$ROOT/build/cretaceous/win/cretaceous.exe}
ASSETS=${CRET_ASSETS:-$ROOT/ocean/cretaceous/assets}
BUILD=${CRET_BUILD:-$ROOT/build/cretaceous}
SRC=$ROOT/ocean/cretaceous

[ -f "$EXE" ] || "$SRC/tools/build_windows.sh" "$(dirname "$EXE")"
for d in textures foliage props dinos audio; do
    if [ -z "$(find "$ASSETS/$d" -maxdepth 1 -type f ! -name '*.md' 2>/dev/null | head -1)" ]; then
        echo "no assets in $ASSETS/$d (set CRET_ASSETS= to a checkout that has them; tools/fetch_*.sh fetch them)" >&2; exit 1
    fi
done

mkdir -p "$DEST"
G=$DEST/ocean/cretaceous
mkdir -p "$G/render" "$G/config" "$G/assets" "$DEST/build/cretaceous/worlds" "$DEST/build/cretaceous/cache"
cp "$EXE" "$DEST/cretaceous.exe"
rsync -a --delete "$SRC/shaders/" "$G/shaders/"
cp "$SRC/config/water.ini" "$G/config/"
rsync -a --delete --include="*.ini" --exclude="*" "$SRC/config/scenarios/" "$G/config/scenarios/"   # play scenario NAME
cp "$SRC/render/view.c" "$SRC/render/water_flow.h" "$SRC/render/veg.c" "$SRC/render/veg.h" "$G/render/"
# The game reads every file in these folders; only the loaded types are copied.
rsync -a --delete --include='*/' --include='*.jpg' --include='*.md' --exclude='*' "$ASSETS/textures/" "$G/assets/textures/"
rsync -a --delete --include='*/' --include='*.png' --include='*.md' --exclude='*' "$ASSETS/foliage/" "$G/assets/foliage/"
rsync -a --delete --include='*.prop' --include='*.jpg' --include='*.md' --exclude='*' "$ASSETS/props/" "$G/assets/props/"
rsync -a --delete --include='*.dino' --include='*.jpg' --include='*.md' --exclude='*' "$ASSETS/dinos/" "$G/assets/dinos/"
rsync -a --delete --include='*.ogg' --include='*.md' --exclude='*' "$ASSETS/audio/" "$G/assets/audio/"

# The world the player loads by default (seed 1, 2048^2 at 4 m: sim.c
# cret_default_config), named by terra_params_hash as world.c does.
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/world_name.c" <<'EOF'
#include <stdio.h>
#include "terra/terra.h"
int main(void) {
    TerraParams p = terra_default_params(1); p.n = 2048; p.cell = 4.0f;
    printf("s1_n2048_%016llx\n", (unsigned long long)terra_params_hash(&p));
    return 0;
}
EOF
${CC:-cc} -O1 -I"$SRC" "$TMP/world_name.c" "$SRC/terra/terra.c" -lm -o "$TMP/world_name"
WORLD=$("$TMP/world_name")
if [ -f "$BUILD/worlds/$WORLD.terra" ]; then
    rsync -a --ignore-existing "$BUILD/worlds/$WORLD.terra" "$DEST/build/cretaceous/worlds/"
    [ -f "$BUILD/worlds/horizon_$WORLD.bin" ] && rsync -a --ignore-existing "$BUILD/worlds/horizon_$WORLD.bin" "$DEST/build/cretaceous/worlds/"
else
    echo "note: $BUILD/worlds/$WORLD.terra not baked yet; the exe bakes it on its first start (slow: one thread)" >&2
fi
# Worlds of older terra versions are never loaded again.
find "$DEST/build/cretaceous/worlds" -maxdepth 1 -type f ! -name "$WORLD.terra" ! -name "horizon_$WORLD.bin" -delete
for f in cloud_shape_v2.bin cloud_detail_v2.bin; do
    [ -f "$BUILD/$f" ] && rsync -a --ignore-existing "$BUILD/$f" "$DEST/build/cretaceous/"
done
[ -d "$BUILD/cache" ] && rsync -a --ignore-existing --include='*.bin' --exclude='*' "$BUILD/cache/" "$DEST/build/cretaceous/cache/"

# Launchers and notes (CRLF line ends for cmd.exe and Notepad).
crlf() { sed 's/$/\r/' > "$1"; }
crlf "$DEST/Play Cretaceous.bat" <<'EOF'
@echo off
rem Starts Cretaceous from this folder (it loads its files relative to it).
cd /d "%~dp0"
cretaceous.exe %*
if errorlevel 1 pause
EOF
crlf "$DEST/Host.bat" <<'EOF'
@echo off
rem Hosts a game on UDP port 7777; you play slot 0. Others use Join.bat with this PC's address.
cd /d "%~dp0"
cretaceous.exe host --port 7777 %*
if errorlevel 1 pause
EOF
crlf "$DEST/Join.bat" <<'EOF'
@echo off
rem Joins a game: Join.bat ADDRESS [--species stego], or run it and type the address.
cd /d "%~dp0"
set "ADDR=%~1"
if "%ADDR%"=="" set /p "ADDR=Server address (IP or name): "
if "%ADDR%"=="" exit /b 1
if not "%~1"=="" shift
cretaceous.exe join %ADDR% --port 7777 %1 %2 %3 %4 %5 %6
if errorlevel 1 pause
EOF
crlf "$DEST/Gallery.bat" <<'EOF'
@echo off
rem The gallery: every tree, prop and dinosaur on a flat plain, with graphics toggles.
cd /d "%~dp0"
cretaceous.exe play gallery %*
if errorlevel 1 pause
EOF
crlf "$DEST/Scenario.bat" <<'EOF'
@echo off
rem Scripted fights: opens on the scenario menu (Esc in a scenario brings it back). Scenario.bat NAME starts one.
cd /d "%~dp0"
cretaceous.exe play scenario %1
if errorlevel 1 pause
EOF
crlf "$DEST/README.txt" <<'EOF'
Cretaceous (native Windows build)
=================================

Play Cretaceous.bat   title screen: Play, Host, Join, Settings, Credits
Host.bat              host a game on UDP port 7777 (you play slot 0)
Join.bat ADDRESS      join a host (or run it and type the address)
Gallery.bat           every tree, prop and dinosaur on a plain (graphics toggles)
Scenario.bat          scripted fights: pick one from the menu (Esc in a fight opens it)

In game: ` or F12 opens the debug console (help lists commands), F6 spawns
a dummy and F7 a slow passive animal 8 m ahead, F1 lists the hotkeys.

Keep the folder together: the game loads its shaders, assets and caches
from the folders next to cretaceous.exe. The console window shows the log.

Options (add them in the .bat files or run cretaceous.exe from a terminal
in this folder):
  cretaceous.exe play --species stego --day-minutes 30
  cretaceous.exe join 192.168.1.20 --species trex
  species: raptor, stego, trex, pachy, sauropelta, galli

Mouse: the cursor is captured in play (Tab or Esc frees it); wheel zooms.
WASD move, Shift sprint, Space jump (raptor), E eat or drink, left mouse
attack, right mouse secondary attack, 1 2 3 abilities, G call, M map,
F1 help, Esc menu.

Settings and saved characters: %APPDATA%\Cretaceous
(settings.ini and characters\NAME.ini).

Hosting: allow cretaceous.exe through Windows Firewall when it asks
(UDP 7777).

The first start after an update may take longer: it rebuilds any texture
cache item that changed (build\cretaceous\cache) and keeps it for next time.

Credits: CREDITS.md (also under Credits in the game). Dinosaur models are
CC BY 4.0 by their authors; textures, foliage, props and sounds list their
sources in ocean\cretaceous\assets\*\SOURCES.md.
EOF
{
    cat "$ASSETS/dinos/CREDITS.md"
    for d in textures foliage props audio; do
        [ -f "$ASSETS/$d/SOURCES.md" ] && { printf '\n\n'; cat "$ASSETS/$d/SOURCES.md"; }
    done
} | crlf "$DEST/CREDITS.md"

echo "Packaged: $DEST ($(du -sh "$DEST" | cut -f1))"
