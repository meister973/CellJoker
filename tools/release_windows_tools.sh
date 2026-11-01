#!/bin/sh
# Fills the Windows tool's folder with what it needs to run: its Python code, the compiler and
# packer built for Windows, NVIDIA's Cg library, and a private copy of Python with Pillow.
# Runs inside plain ubuntu:22.04. Downloads are kept in dist/release_downloads.
set -eu

ROOT=/work
OUTPUT="$ROOT/dist/make_pkg"
DOWNLOADS="$ROOT/dist/release_downloads"
PSL1GHT_COMMIT=7e1bd96307eeac8000f18fd75e643d2fa826b5c1
CG_URL=https://developer.download.nvidia.com/cg/Cg_3.1/Cg-3.1_April2012_Setup.exe
CG_DLL_CRC32=e5228ec2
PYTHON_VERSION=3.12.10
PILLOW_VERSION=11.3.0

apt-get update -qq > /dev/null
apt-get install -y -qq --no-install-recommends ca-certificates curl git innoextract unzip \
    python3-pip g++-mingw-w64-i686 gcc-mingw-w64-x86-64 > /dev/null

mkdir -p "$DOWNLOADS" "$OUTPUT/tool"
rm -rf "$OUTPUT/python" "$OUTPUT/tool"/*
WORK=$(mktemp -d)

echo "--- compiler and packer"
git clone -q https://github.com/ps3dev/PSL1GHT "$WORK/psl1ght"
git -C "$WORK/psl1ght" checkout -q "$PSL1GHT_COMMIT"
# cgcomp reads two buffers past their end: Cg's argument list has no NULL end, and an assembly
# file has no terminating zero. Fresh Linux memory hides this, and real Windows crashes on it.
CGCOMP_MAIN="$WORK/psl1ght/tools/cgcomp/source/main.cpp"
sed -i -e 's/argv = new char\*\[numArgs\];/argv = new char*[numArgs + 1];/' \
    -e 's/memset(argv, 0, numArgs);/memset(argv, 0, (numArgs + 1) * sizeof(char*));/' \
    -e '195i\	prg[len] = 0;' "$CGCOMP_MAIN"
grep -q 'numArgs + 1) \* sizeof' "$CGCOMP_MAIN"
sed -n '195p' "$CGCOMP_MAIN" | grep -q 'prg\[len\] = 0'
# 32-bit, to match the 32-bit cg.dll.
i686-w64-mingw32-g++ -DWIN32 -O2 -static -s -I"$WORK/psl1ght/tools/cgcomp/include" \
    "$WORK"/psl1ght/tools/cgcomp/source/*.cpp -o "$OUTPUT/tool/cgcomp.exe"
# pkg.c's Windows folder walk stores paths with a leading slash, so the PS3 would not find
# USRDIR/EBOOT.BIN. Its POSIX walk works with MinGW, and needs far more stack than the 1 MB default.
sed -i '291s/#ifdef _WIN32/#if 0/' "$WORK/psl1ght/tools/sfo_pkg/pkg.c"
x86_64-w64-mingw32-gcc -O2 -static -s -Wl,--stack,67108864 -include dirent.h -include sys/stat.h \
    "$WORK/psl1ght/tools/sfo_pkg/pkg.c" "$WORK/psl1ght/tools/sfo_pkg/sha1.c" -o "$OUTPUT/tool/pkg.exe"
cp "$WORK/psl1ght/LICENSE" "$OUTPUT/tool/PSL1GHT_LICENSE.txt"

echo "--- NVIDIA Cg library, unpacked and never run"
[ -f "$DOWNLOADS/cg_setup.exe" ] || curl -sSLf -o "$DOWNLOADS/cg_setup.exe" "$CG_URL"
mkdir "$WORK/cg"
(cd "$WORK/cg" && innoextract -s -e "$DOWNLOADS/cg_setup.exe" > /dev/null)
python3 -c "import sys, zlib; sys.exit(0 if '%08x' % zlib.crc32(open(sys.argv[1], 'rb').read()) == sys.argv[2] else 1)" \
    "$WORK/cg/app/bin/cg.dll" "$CG_DLL_CRC32"
cp "$WORK/cg/app/bin/cg.dll" "$OUTPUT/tool/cg.dll"
cp "$WORK/cg/app/docs/license.txt" "$OUTPUT/tool/Cg_LICENSE.txt"

echo "--- private Python and Pillow"
PYTHON_ZIP="python-$PYTHON_VERSION-embed-amd64.zip"
[ -f "$DOWNLOADS/$PYTHON_ZIP" ] || curl -sSLf -o "$DOWNLOADS/$PYTHON_ZIP" \
    "https://www.python.org/ftp/python/$PYTHON_VERSION/$PYTHON_ZIP"
mkdir "$OUTPUT/python"
unzip -q "$DOWNLOADS/$PYTHON_ZIP" -d "$OUTPUT/python"
pip3 download -q --no-deps --only-binary=:all: --platform win_amd64 --python-version 3.12 \
    -d "$DOWNLOADS" "pillow==$PILLOW_VERSION"
mkdir -p "$OUTPUT/python/Lib/site-packages"
unzip -q "$DOWNLOADS"/pillow-"$PILLOW_VERSION"-*win_amd64.whl -d "$OUTPUT/python/Lib/site-packages"
# An embedded Python ignores PYTHONPATH, and reads its search path from this file.
printf 'Lib\\site-packages\r\n..\\tool\r\n' >> "$OUTPUT/python/python312._pth"

echo "--- the tool's own files"
for name in make_pkg make_game_data texture_cook shader_build shader_translate cgcomp_full_precision; do
    cp "$ROOT/tools/$name.py" "$OUTPUT/tool/"
done
cp "$ROOT/tools/release/make_pkg.bat" "$ROOT/tools/release/HOW_TO_USE.txt" "$ROOT/tools/release/NOTICES.txt" "$OUTPUT/"

rm -rf "$WORK"
ls -la "$OUTPUT/tool" | cut -c30-
