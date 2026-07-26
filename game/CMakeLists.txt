add_executable(voxel-factory
    src/main.cpp
    src/Block.cpp
    src/Item.cpp
    src/Recipes.cpp
    src/Chunk.cpp
    src/World.cpp
    src/Atlas.cpp
    src/ChunkMesher.cpp
    src/Raycast.cpp
    src/PowerSystem.cpp
    src/SaveSystem.cpp
    src/Settings.cpp
    src/Weather.cpp
    src/WorldEdit.cpp
    src/PlayerController.cpp
    src/CreatureSystem.cpp
    src/MachineSystem.cpp
    src/AlchemyCircle.cpp
    src/DropSystem.cpp
    src/VoxelGame.cpp
    src/VoxelGameWorldGen.cpp
    src/VoxelGameSim.cpp
    src/VoxelGamePlayer.cpp
    src/VoxelGameRender.cpp
    src/VoxelGameUi.cpp
)

target_include_directories(voxel-factory PRIVATE include)
target_link_libraries(voxel-factory PRIVATE engine)

# Same warning stance as the engine (see engine/CMakeLists.txt).
if(MSVC)
    target_compile_options(voxel-factory PRIVATE /W4 /external:anglebrackets /external:W0)
else()
    target_compile_options(voxel-factory PRIVATE -Wall -Wextra)
endif()

# Product version (from the root project()) — shown in the title bar and F3 overlay.
target_compile_definitions(voxel-factory PRIVATE
    VOXEL_FACTORY_VERSION="${PROJECT_VERSION}")

# Copy the shaders next to the executable so it can load them at runtime
# (resolved relative to SDL_GetBasePath()).
add_custom_command(TARGET voxel-factory POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:voxel-factory>/shaders
    COMMENT "Copying shaders to output directory"
)

# Copy assets (the paintable texture atlas) next to the executable. A
# standalone always-run target (not POST_BUILD) so a repainted atlas.png is
# picked up by `cmake --build` even when no code changed.
add_custom_target(copy-assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:voxel-factory>/assets
    COMMENT "Copying assets to output directory"
)
add_dependencies(copy-assets voxel-factory)

# Copy SDL3.dll next to the executable on Windows so it runs in place.
if(WIN32)
    add_custom_command(TARGET voxel-factory POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:voxel-factory>
        COMMENT "Copying SDL3 runtime library to output directory"
    )
endif()

# On macOS libSDL3.dylib already lands in bin/ (the root output dirs apply to
# the SDL3 subproject too); @loader_path makes the exe find it there wherever
# the bin folder is moved — the same relocatable-folder story as Windows.
if(APPLE)
    set_target_properties(voxel-factory PROPERTIES BUILD_RPATH "@loader_path")
endif()
