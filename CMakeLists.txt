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
# thing to avoid). Ignored by non-MSVC toolchains.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

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
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# Our subprojects
add_subdirectory(engine)
add_subdirectory(game)
