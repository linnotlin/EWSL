#!/usr/bin/env bash
#
# EWSL - build script
#
# Arch Linux (cross compile):
#   sudo pacman -S --needed mingw-w64-gcc
#   ./build.sh
#
# MSYS2 / MINGW64 (native):
#   CXX=g++ ./build.sh
#
# The source files are UTF-8 and contain Chinese UI strings, so the
# -finput-charset / -fexec-charset / -fwide-exec-charset flags are required.
#
set -euo pipefail

cd "$(dirname "$0")"

CXX="${CXX:-x86_64-w64-mingw32-g++}"
OUT="${OUT:-dist/EWSL.exe}"

SRCS=(
    src/main.cpp
    src/terminal.cpp
    src/render.cpp
    src/wsl.cpp
    src/fs.cpp
    src/lang.cpp
    src/catalog.cpp
    src/download.cpp
    src/editor.cpp
    src/ui.cpp
)

CPPFLAGS=(
    -DUNICODE
    -D_UNICODE
    -DWINVER=0x0601
    -D_WIN32_WINNT=0x0601
)

CXXFLAGS=(
    -std=c++17
    -O2
    -Wall
    -Wextra
    -Wno-unused-parameter
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -fwide-exec-charset=UTF-16LE
)

LDFLAGS=(
    -Wl,--subsystem,windows
    -static
    -static-libgcc
    -static-libstdc++
)

LDLIBS=(
    -lgdi32
    -luser32
    -lkernel32
    -lgdiplus
    -ldwmapi
    -lole32
    -lshell32
    -lwinhttp
    -ladvapi32
)

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "error: compiler not found: $CXX" >&2
    echo "  Arch:  sudo pacman -S --needed mingw-w64-gcc" >&2
    echo "  MSYS2: CXX=g++ ./build.sh" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"

set -x
"$CXX" "${CPPFLAGS[@]}" "${CXXFLAGS[@]}" -o "$OUT" "${SRCS[@]}" "${LDFLAGS[@]}" "${LDLIBS[@]}"
set +x

# icon.jpg -> RT_ICON + RT_GROUP_ICON, patched into the .rsrc section by
# tools/make-rsrc.py. Same step build-zig.sh runs; without it the exe comes out
# with a default blank icon and the taskbar button reads as an empty square.
if command -v python >/dev/null 2>&1; then
    if ! python tools/make-rsrc.py "$OUT"; then
        echo "error: tools/make-rsrc.py failed (it needs Pillow)" >&2
        echo "  python -m pip install pillow" >&2
        exit 1
    fi
else
    echo "warning: python not found - skipping icon resources" >&2
    echo "  $OUT will build without an icon. Install python + pillow to embed it." >&2
fi

ls -lh "$OUT"
echo "built: $OUT"
