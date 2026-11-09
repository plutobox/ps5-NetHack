#!/bin/bash
# One-time setup for the NetHack PS5 port. Run from sys/ps5:  ./setup.sh
# Needs: clang-18 lld-18 cmake make git wget unzip python3 (Ubuntu 24.04 / WSL works)
#
# 1. Downloads the ps5-payload-sdk into deps/
# 2. Builds the PS5 SDL2 port and installs it into the SDK sysroot
# 3. Fetches the PDCursesMod submodule
# 4. Builds NetHack natively (deps/nethack-host) to generate data files
#
# ./setup.sh --native does only 3 and 4: the native title (Makefile.native)
# brings its own toolchain and SDL2 (see README.md).
set -e
PS5DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NH="$(cd "$PS5DIR/../.." && pwd)"
DEPS="$PS5DIR/deps"
SDK="$DEPS/ps5-payload-sdk"
NATIVE_ONLY=
[ "${1:-}" = "--native" ] && NATIVE_ONLY=1
mkdir -p "$DEPS"

if [ -z "$NATIVE_ONLY" ] && [ ! -x "$SDK/bin/prospero-clang" ]; then
    echo "== Downloading ps5-payload-sdk"
    wget -q -O "$DEPS/sdk.zip" \
        https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
    unzip -q -o "$DEPS/sdk.zip" -d "$DEPS"
    rm "$DEPS/sdk.zip"
fi

if [ -z "$NATIVE_ONLY" ] && [ ! -f "$SDK/target/user/homebrew/lib/libSDL2.a" ]; then
    echo "== Building SDL2 for PS5 (configure step takes a few minutes)"
    [ -d "$DEPS/SDL" ] || git clone -q --depth 1 -b release-2.30.x-ps5 \
        https://github.com/ps5-payload-dev/SDL "$DEPS/SDL"
    "$SDK/bin/prospero-cmake" -S "$DEPS/SDL" -B "$DEPS/SDL/build" \
        -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
        > "$DEPS/sdl-config.log" 2>&1
    make -C "$DEPS/SDL/build" -j"$(nproc)" > "$DEPS/sdl-build.log" 2>&1
    # CMAKE_INSTALL_PREFIX is the PS5 path /user/homebrew; DESTDIR maps it
    # into the SDK sysroot.
    make -C "$DEPS/SDL/build" install DESTDIR="$SDK/target" > "$DEPS/sdl-install.log" 2>&1
fi

echo "== Fetching PDCursesMod"
git -C "$NH" submodule update --init --depth 1 submodules/pdcursesmod

if [ ! -f "$DEPS/nethack-host/include/date.h" ]; then
    echo "== Building NetHack natively for data files"
    rm -rf "$DEPS/nethack-host"
    mkdir -p "$DEPS/nethack-host"
    git -C "$NH" archive HEAD | tar -x -C "$DEPS/nethack-host"
    (cd "$DEPS/nethack-host/sys/unix" && sh setup.sh hints/linux-minimal) > "$DEPS/host-setup.log" 2>&1
    make -C "$DEPS/nethack-host" -j"$(nproc)" all > "$DEPS/host-build.log" 2>&1
fi

if [ -n "$NATIVE_ONLY" ]; then
    echo "== Setup done. Now run: make -f Makefile.native && bash package-native.sh --build"
else
    echo "== Setup done. Now run: make && make package"
fi
