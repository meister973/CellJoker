#!/bin/sh
# Builds the harness and plays one long run. Runs inside a harness image:
#
#   docker run --rm -v <repo>:/work -v <unzipped Balatro>:/game:ro \
#       -e HARNESS_MAX_ANTE=12 balatro-harness:3 sh harness/run.sh
#
# The image picks the target through CC, ZLIB_ROOT, LINK_FLAGS and RUNNER:
# Dockerfile builds for the host, Dockerfile.ppc builds 32-bit big-endian PowerPC and
# runs it under qemu-ppc. The game files are copied, never changed.
set -eu

CC=${CC:-gcc}
ZLIB_ROOT=${ZLIB_ROOT:-/usr}
LINK_FLAGS=${LINK_FLAGS:-}
# HARNESS_CFLAGS=-DHARNESS_CALLGRIND lets the driver switch callgrind collection on and off.
HARNESS_CFLAGS=${HARNESS_CFLAGS:-}
RUNNER=${RUNNER:-}
TARGET_NAME=${TARGET_NAME:-host}

BUILD=/tmp/harness-build
INSTALL=/tmp/install/
OUT=/work/harness/out
mkdir -p "$BUILD" "$OUT"

# The PS3 build's interpreter: Lua 5.1.5 with docker/lua/fast_table_access.patch, built from
# the repository's patch on every run. HARNESS_STOCK_LUA=1 leaves the patch out.
LUA_SRC=/tmp/lua-build/src
rm -rf /tmp/lua-build
mkdir -p /tmp/lua-build
tar -xzf /opt/lua-5.1.5.tar.gz -C /tmp/lua-build --strip-components=1
if [ "${HARNESS_STOCK_LUA:-0}" != 1 ]; then
    patch -s -d /tmp/lua-build -p1 < /work/docker/lua/fast_table_access.patch
fi
for f in "$LUA_SRC"/*.c; do
    case "$f" in */lua.c|*/luac.c|*/print.c) continue ;; esac
    $CC -O2 -g -DLUA_USE_POSIX -c "$f" -o "${f%.c}.o"
done

$CC -O2 -g -Wall $HARNESS_CFLAGS $LINK_FLAGS -o "$BUILD/harness" \
    -I"$LUA_SRC" -I"$ZLIB_ROOT/include" \
    -I/work/runtime/src/lua -I/work/runtime/src/love \
    /work/harness/host_main.c \
    /work/runtime/src/lua/lua_runtime.c \
    /work/runtime/src/lua/bit.c \
    /work/runtime/src/lua/luajit_compat.c \
    /work/runtime/src/lua/game_native.c \
    /work/runtime/src/lua/moveable_native.c \
    /work/runtime/src/lua/ui_native.c \
    /work/runtime/src/love/lua_data.c \
    "$LUA_SRC"/*.o -L"$ZLIB_ROOT/lib" -lz -lm

rm -rf "$INSTALL"
mkdir -p "$INSTALL"
cp -R /game/. "$INSTALL"
cp /work/runtime/lua/*.lua "$INSTALL"
cp /work/harness/late_run_driver.lua "${INSTALL}play_driver.lua"
cp /work/harness/heap_census.lua "$INSTALL"
cp /work/harness/state_dump.lua "$INSTALL"
# HARNESS_NO_FIXES=1 measures the game without runtime/lua/game_fixes.lua, as a baseline.
if [ "${HARNESS_NO_FIXES:-0}" = 1 ]; then echo 'return function() end' > "${INSTALL}game_fixes.lua"; fi

LOG="$OUT/run-$TARGET_NAME-$(date +%Y%m%d-%H%M%S).log"
$RUNNER "$BUILD/harness" "$INSTALL" /work/harness/host_love_native.lua > "$LOG" 2>&1 && STATUS=0 || STATUS=$?
grep -E '^(metric|driver|harness|leak_check|census_[a-z]+):' "$LOG" || true
echo "harness: log $LOG"
echo "harness: exit $STATUS"
exit $STATUS
