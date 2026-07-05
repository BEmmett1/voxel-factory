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
    src/Application.cpp
)

target_include_directories(engine PUBLIC include)
target_include_directories(engine PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)

# SDL3 + glad + glm are part of the engine's public interface (headers expose them).
target_link_libraries(engine PUBLIC SDL3::SDL3 glad glm::glm)
