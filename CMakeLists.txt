cmake_minimum_required(VERSION 3.21)
project(VoxelFactory VERSION 0.1.0 LANGUAGES C CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All binaries land next to each other so the exe finds SDL3.dll + shaders/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Bake the MSVC runtime into the binaries so the bin/ folder runs on machines
# without the VC++ Redistributable. Set before FetchContent so SDL3 and all of
# our targets agree on the CRT (mixing CRTs across the DLL boundary is the
# thing to avoid).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

# On Linux SDL3 detects its video backends from headers that are present at
# configure time and silently drops the ones that aren't -- with neither X11
# nor Wayland it still builds, then dies at SDL_CreateWindow with only the
# "dummy" driver. Catch that here, before the multi-minute SDL build, and name
# the packages instead.
if(UNIX AND NOT APPLE)
    find_path(VF_X11_HEADER X11/Xlib.h)
    find_path(VF_WAYLAND_HEADER wayland-client.h)
    if(NOT VF_X11_HEADER AND NOT VF_WAYLAND_HEADER)
        message(FATAL_ERROR
            "No X11 or Wayland development headers found -- SDL3 would build "
            "with no usable video driver.\n"
            "  Debian/Ubuntu: sudo apt install libx11-dev libxext-dev "
            "libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev "
            "libxkbcommon-dev libwayland-dev wayland-protocols "
            "libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev\n"
            "  Fedora: sudo dnf install libX11-devel libXext-devel "
            "libXrandr-devel libXcursor-devel libXi-devel libXfixes-devel "
            "libxkbcommon-devel wayland-devel mesa-libGL-devel "
            "mesa-libEGL-devel dbus-devel systemd-devel\n"
            "  Arch: sudo pacman -S libx11 libxext libxrandr libxcursor libxi "
            "libxfixes libxkbcommon wayland wayland-protocols mesa")
    endif()
endif()

# --- SDL3: window, OpenGL context, input, events ---
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

# --- glm: vector / matrix math (header-only) ---
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# --- glad: OpenGL 3.3 core function loader (vendored, pre-generated) ---
# SYSTEM keeps its headers outside consumers' warning ratchet (see engine/).
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC third_party/glad/include)

# Our subprojects
add_subdirectory(engine)
add_subdirectory(game)
