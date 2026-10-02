#!/usr/bin/env bash
#
# EWSL - build with Zig (no MinGW installation required)
#
# Zig ships its own mingw-w64 headers and import libraries, so
#     zig c++ -target x86_64-windows-gnu
# produces a native Windows executable with nothing else installed.
#
# Usage:
#   ZIG=/path/to/zig ./build-zig.sh
#
set -euo pipefail

cd "$(dirname "$0")"

ZIG="${ZIG:-zig}"
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

# Zig's mingw-w64 headers already define _WIN32_WINNT/WINVER (0x0a00).
# Do not override them here or the compile emits -Wmacro-redefined.
FLAGS=(
    -target x86_64-windows-gnu
    -std=c++17
    -O2
    -DUNICODE
    -D_UNICODE
)

LDFLAGS=(
    -Wl,--subsystem,windows
    -static
)

LIBS=(
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

if [ ! -x "$ZIG" ] && ! command -v "$ZIG" >/dev/null 2>&1; then
    echo "error: zig not found: $ZIG" >&2
    echo "  download: https://ziglang.org/download/" >&2
    echo "  or:       pip install ziglang  (wheel ships a full zig toolchain)" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"

set -x
"$ZIG" c++ "${FLAGS[@]}" -o "$OUT" "${SRCS[@]}" "${LDFLAGS[@]}" "${LIBS[@]}"
set +x

# icon.jpg -> RT_ICON + RT_GROUP_ICON, patched straight into the .rsrc
# section by tools/make-rsrc.py (embedding a hand-written .res through lld
# truncates the icon group, so we write the resource tree ourselves instead).
if command -v python >/dev/null 2>&1; then
    # make-rsrc.py needs Pillow. Failing here beats shipping an exe with no icon:
    # this script runs under set -e, so a non-zero exit stops the build, and the
    # message below is the whole diagnosis.
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
