#!/usr/bin/env bash
# Install the build dependencies for a Linux build of voxel-factory.
#
# Why this is a script and not a line in a README: SDL3 is compiled from
# source, and its X11 detection is all-or-nothing per sub-feature. Once
# SDL_X11 turns on (which one look at X11/Xlib.h is enough to do), a MISSING
# sub-feature is a hard configure error, not a dropped feature:
#
#   Couldn't find dependency package for XSCRNSAVER. Please install the
#   needed packages or configure with -DSDL_X11_XSCRNSAVER=OFF
#
# So a "mostly right" package list fails late and one package at a time. The
# list below is SDL3's own published set (README-linux) plus Wayland and our
# toolchain, and CI runs THIS FILE — so the command in the docs is the command
# that is tested on every push.
#
# Usage: bash tools/install-linux-deps.sh
set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi

if command -v apt-get >/dev/null 2>&1; then
    $SUDO apt-get update
    $SUDO apt-get install -y --no-install-recommends \
        build-essential git cmake ninja-build pkg-config \
        libasound2-dev libpulse-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
        libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libdecor-0-dev \
        libdrm-dev libgbm-dev \
        libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
        libdbus-1-dev libudev-dev libibus-1.0-dev

elif command -v dnf >/dev/null 2>&1; then
    $SUDO dnf install -y \
        gcc-c++ git cmake ninja-build pkgconf-pkg-config \
        alsa-lib-devel pulseaudio-libs-devel \
        libX11-devel libXext-devel libXrandr-devel libXcursor-devel \
        libXfixes-devel libXi-devel libXScrnSaver-devel libXtst-devel \
        libxkbcommon-devel \
        wayland-devel wayland-protocols-devel libdecor-devel \
        libdrm-devel mesa-libgbm-devel \
        mesa-libGL-devel mesa-libGLES-devel mesa-libEGL-devel \
        dbus-devel systemd-devel ibus-devel

elif command -v pacman >/dev/null 2>&1; then
    $SUDO pacman -S --needed --noconfirm \
        base-devel git cmake ninja pkgconf \
        alsa-lib libpulse \
        libx11 libxext libxrandr libxcursor libxfixes libxi libxss libxtst \
        libxkbcommon \
        wayland wayland-protocols libdecor \
        libdrm mesa \
        dbus ibus

else
    echo "No supported package manager found (apt-get / dnf / pacman)." >&2
    echo "Install SDL3's Linux build dependencies by hand:" >&2
    echo "  https://wiki.libsdl.org/SDL3/README-linux#build-dependencies" >&2
    exit 1
fi

echo "Linux build dependencies installed."
