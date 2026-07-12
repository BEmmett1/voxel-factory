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

# /W4 for first-party code only: everything third-party (SDL3/glm/glad and the
# vendored single-header libs) is angle-included, so /external keeps it quiet.
# Clang gets the same ratchet; there third-party stays quiet via -isystem
# (FetchContent marks SDL3/glm SYSTEM, and glad + the single-headers are
# marked SYSTEM where they're added).
if(MSVC)
    target_compile_options(engine PRIVATE /W4 /external:anglebrackets /external:W0)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra)
endif()
