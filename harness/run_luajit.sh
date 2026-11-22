#!/bin/sh
# Same run as run.sh, on LuaJIT 2.1 instead of the PS3's Lua 5.1. LuaJIT is what the PC
# game runs on, so a difference between the two logs comes from the port's interpreter.
# HARNESS_JIT=1 turns the JIT compiler on; instruction counts are then meaningless.
set -eu
BUILD=/tmp/harness-build
INSTALL=/tmp/install/
OUT=/work/harness/out
mkdir -p "$BUILD" "$OUT"

gcc -O2 -Wall -DHARNESS_LUAJIT -o "$BUILD/harness" \
    -I/usr/include/luajit-2.1 -I/work/runtime/src/love \
    /work/harness/host_main.c /work/runtime/src/love/lua_data.c \
    -lluajit-5.1 -lz -lm

rm -rf "$INSTALL"
mkdir -p "$INSTALL"
cp -R /game/. "$INSTALL"
cp /work/runtime/lua/*.lua "$INSTALL"
cp /work/harness/late_run_driver.lua "${INSTALL}play_driver.lua"
cp /work/harness/heap_census.lua "$INSTALL"

LOG="$OUT/run-luajit-$(date +%Y%m%d-%H%M%S).log"
"$BUILD/harness" "$INSTALL" /work/harness/host_love_native.lua > "$LOG" 2>&1 || true
grep -E '^(metric|driver|harness|census_[a-z]+):' "$LOG" || true
echo "harness: log $LOG"
