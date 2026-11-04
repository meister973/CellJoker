#!/bin/sh
# Puts the signed runtime in the Windows tool's folder. Runs inside the toolchain image, after
# tools/build_love.ps1. The runtime holds no game content: shaders are made on the user's PC.
set -eu

ROOT=/work
OUTPUT="$ROOT/dist/make_pkg/runtime"
APPID=BLTR00001
CONTENTID="UP0001-${APPID}_00-0000000000000000"

rm -rf "$OUTPUT"
mkdir -p "$OUTPUT"

# build/ holds the copy that went through sprxlinker, which is the only bootable one.
make_self_npdrm "$ROOT/love-ps3/build/love-ps3.elf" "$OUTPUT/EBOOT.BIN" "$CONTENTID" > /dev/null
cp "$ROOT"/runtime/lua/*.lua "$OUTPUT/"
cp "$ROOT/love-ps3/ICON0.PNG" "$OUTPUT/ICON0.PNG"
sfo --title Balatro --appid "$APPID" -f "$PS3DEV/bin/sfo.xml" "$OUTPUT/PARAM.SFO"

ls -la "$OUTPUT" | wc -l
