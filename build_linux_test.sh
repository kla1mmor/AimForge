#!/bin/sh
# Linux test build (for headless verification under Xvfb). The real target is build_windows.sh.
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
XINC=${XINC:-../xinc}
"$PYTHON" tools/embed_fonts.py
mkdir -p build/linux/rl
CFLAGS="-O2 -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 -DSUPPORT_CUSTOM_FRAME_CONTROL=1 -D_GNU_SOURCE -D_GLFW_X11"
if [ ! -f build/linux/libraylib.a ]; then
  for f in rcore rshapes rtextures rtext rmodels raudio utils rglfw; do
    gcc $CFLAGS -I$RL -I$RL/external/glfw/include -I$XINC -w -c $RL/$f.c -o build/linux/rl/$f.o &
  done
  wait
  ar rcs build/linux/libraylib.a build/linux/rl/*.o
fi
gcc -O2 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-override-init -Wno-missing-field-initializers \
  -I$RL -I$RL/external/glfw/include src/*.c build/linux/libraylib.a -lm -ldl -lpthread -o build/linux/aimforge
echo built build/linux/aimforge
