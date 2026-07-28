add_library(engine STATIC
    src/Clock.cpp
    src/Window.cpp
    src/Input.cpp
    src/Camera.cpp
    src/Shader.cpp
    src/Mesh.cpp
    src/Texture.cpp
    src/Image.cpp
    src/UiRenderer.cpp
    src/Audio.cpp
    src/BbModel.cpp
    src/Paths.cpp
    src/Log.cpp
    src/CrashHandler.cpp
    src/Application.cpp
)

target_include_directories(engine PUBLIC include)
# SYSTEM so the vendored single-header libs stay outside the warning ratchet
# on Clang too (MSVC already treats them as external via /external below).
target_include_directories(engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_include_directories(engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/miniaudio)
target_include_directories(engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/json)

# SDL3 + glad + glm are part of the engine's public interface (headers expose them).
target_link_libraries(engine PUBLIC SDL3::SDL3 glad glm::glm)

# miniaudio's CoreAudio backend needs the Apple frameworks spelled out.
if(APPLE)
    target_link_libraries(engine PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

# On Linux miniaudio dlopen()s ALSA/PulseAudio at runtime rather than linking
# them, so there are no audio dev packages to install -- but it does need the
# loader, threads, and libm spelled out. A machine with neither backend present
# simply fails to open a device, which Audio already degrades to silence.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# CrashHandler writes minidumps via DbgHelp on Windows.
if(WIN32)
    target_link_libraries(engine PUBLIC dbghelp)
endif()

# /W4 for first-party code only: everything third-party (SDL3/glm/glad and the
# vendored single-header libs) is angle-included, so /external keeps it quiet.
# Clang gets the same ratchet; there third-party stays quiet via -isystem
# (FetchContent marks SDL3/glm SYSTEM, and glad + the single-headers are
# marked SYSTEM where they're added).
#
# -Wno-missing-field-initializers (GCC's -Wextra turns it on; MSVC and Clang
# stay quiet here) because omitting trailing fields in a designated-initializer
# registry row is this codebase's convention, not an oversight: kBlocks,
# kItems, kMachineTraits and kBlockShapes are read as tables where an absent
# field means "the default", and the omitted members are value-initialized.
# Spelling every field on every row to satisfy the warning would fight the one
# property those tables are built for -- adding content is appending a row.
if(MSVC)
    target_compile_options(engine PRIVATE /W4 /external:anglebrackets /external:W0)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
