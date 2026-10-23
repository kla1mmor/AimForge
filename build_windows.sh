#!/bin/sh
# Cross-compile AimForge.exe for Windows x64 with zig cc (static, no console window).
# On Windows you can run the same commands with `zig cc` directly (see README).
set -e
cd "$(dirname "$0")"
# python: $PYTHON, else the first of python3 / python / py that really runs (Windows has a fake "python3" store alias)
if [ -z "$PYTHON" ]; then
  for c in python3 python py; do
    if command -v "$c" >/dev/null 2>&1 && "$c" -c "import sys" >/dev/null 2>&1; then PYTHON=$c; break; fi
  done
fi
PYTHON=${PYTHON:-python3}
RL=${RAYLIB_SRC:-deps/raylib/src}
ZIG=${ZIG:-"$PYTHON -m ziglang"}
OUT=build/win
"$PYTHON" tools/embed_fonts.py
SERVER_DEF=""
[ -f server/server_url.txt ] && SERVER_DEF="-DAIMFORGE_DEFAULT_SERVER=\"$(tr -d '\r\n' < server/server_url.txt)\""
mkdir -p $OUT/rl
T="-target x86_64-windows-gnu"
CFLAGS="-O2 -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 -DSUPPORT_CUSTOM_FRAME_CONTROL=1"
if [ ! -f $OUT/libraylib.a ]; then
  for f in rcore rshapes rtextures rtext rmodels raudio utils rglfw; do
    $ZIG cc $T $CFLAGS -I$RL -I$RL/external/glfw/include -w -c $RL/$f.c -o $OUT/rl/$f.o &
  done
  wait
  $ZIG ar rcs $OUT/libraylib.a $OUT/rl/*.o
fi
$ZIG cc $T -O2 -std=c11 $SERVER_DEF -Wall -Wno-unused-parameter -Wno-initializer-overrides \
  -I$RL -I$RL/external/glfw/include \
  src/*.c res/aimforge.rc $OUT/libraylib.a \
  -lopengl32 -lgdi32 -lwinmm -luser32 -lshell32 -lole32 -lwinhttp -lbcrypt \
  -Wl,--subsystem,windows -static -s -o $OUT/AimForge.exe
ls -la $OUT/AimForge.exe
