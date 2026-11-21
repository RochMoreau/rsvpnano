#!/usr/bin/env bash
# Build RSVP Nano for NextUI.
#
#   ports/nextui/build.sh desktop   # native SDL2 build for development (needs libsdl2-dev)
#   ports/nextui/build.sh pak       # TrimUI Brick / Smart Pro (tg5040) pak -> dist/RSVP.Nano.pak.zip
#
# The pak build cross-compiles with Zig (any recent release; `pip install ziglang` works) and only
# needs SDL2 from the TrimUI SDK, which is downloaded once into the cache directory.
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$PORT_DIR/../.." && pwd)"
CACHE_DIR="${RSVP_CACHE_DIR:-$ROOT_DIR/.nextui-cache}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
SDK_URL="https://github.com/trimui/toolchain_sdk_smartpro/releases/download/20231018/SDK_usr_tg5040_a133p.tgz"

ensure_submodules() {
    if [ ! -f "$ROOT_DIR/lib/zlib/upstream/zlib.h" ]; then
        git -C "$ROOT_DIR" submodule update --init --depth 1
    fi
}

ensure_zig() {
    if command -v zig >/dev/null 2>&1; then
        return
    fi
    if python3 -c "import ziglang" >/dev/null 2>&1; then
        mkdir -p "$CACHE_DIR/bin"
        printf '#!/bin/sh\nexec python3 -m ziglang "$@"\n' > "$CACHE_DIR/bin/zig"
        chmod +x "$CACHE_DIR/bin/zig"
        export PATH="$CACHE_DIR/bin:$PATH"
        return
    fi
    echo "Zig is required for the pak build: install it, or run: pip install ziglang" >&2
    exit 1
}

ensure_sysroot() {
    if [ -n "${TG5040_SYSROOT:-}" ]; then
        return
    fi
    TG5040_SYSROOT="$CACHE_DIR/tg5040-sdl2"
    if [ ! -f "$TG5040_SYSROOT/usr/lib/libSDL2-2.0.so.0" ]; then
        echo "Fetching SDL2 from the TrimUI tg5040 SDK..."
        mkdir -p "$CACHE_DIR" "$TG5040_SYSROOT"
        local archive="$CACHE_DIR/SDK_usr_tg5040_a133p.tgz"
        [ -f "$archive" ] || curl -fL --retry 3 -o "$archive" "$SDK_URL"
        tar -xzf "$archive" -C "$TG5040_SYSROOT" --wildcards 'usr/include/SDL2/*' 'usr/lib/libSDL2-2.0.so*'
    fi
    export TG5040_SYSROOT
}

build_desktop() {
    ensure_submodules
    local build="$ROOT_DIR/build/nextui-desktop"
    cmake -S "$PORT_DIR" -B "$build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DRSVP_DEPS_DIR="$CACHE_DIR/deps"
    cmake --build "$build" --target rsvpnano -j "$JOBS"
    echo "Built $build/rsvpnano (run it with --data <folder>)"
}

build_pak() {
    ensure_submodules
    ensure_zig
    ensure_sysroot
    local build="$ROOT_DIR/build/nextui-tg5040"
    cmake -S "$PORT_DIR" -B "$build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DRSVP_DEPS_DIR="$CACHE_DIR/deps" \
        -DCMAKE_TOOLCHAIN_FILE="$PORT_DIR/cmake/zig-aarch64-tg5040.cmake" -DTG5040_SYSROOT="$TG5040_SYSROOT"
    cmake --build "$build" --target rsvpnano -j "$JOBS"

    local dist="$ROOT_DIR/dist"
    local stage="$dist/pak"
    local pak="$stage/Tools/tg5040/RSVP Nano.pak"
    rm -rf "$stage" "$dist/RSVP.Nano.pak.zip"
    mkdir -p "$pak"
    cp "$build/rsvpnano" "$pak/rsvpnano" # stripped at link time in Release
    cp "$PORT_DIR/pak/launch.sh" "$PORT_DIR/pak/pak.json" "$pak/"
    cp "$PORT_DIR/README.md" "$pak/README.md"
    cp "$ROOT_DIR/LICENSE" "$pak/LICENSE"
    chmod +x "$pak/launch.sh" "$pak/rsvpnano"
    # Unzip at the root of the SD card: Tools/tg5040/RSVP Nano.pak/launch.sh
    (cd "$stage" && zip -qr "$dist/RSVP.Nano.pak.zip" Tools)
    echo "Built $dist/RSVP.Nano.pak.zip"
}

case "${1:-pak}" in
desktop) build_desktop ;;
pak) build_pak ;;
*)
    echo "usage: $0 [desktop|pak]" >&2
    exit 2
    ;;
esac
