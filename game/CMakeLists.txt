add_executable(voxel-factory
    src/main.cpp
    src/Block.cpp
    src/Chunk.cpp
    src/World.cpp
    src/Atlas.cpp
    src/ChunkMesher.cpp
    src/Raycast.cpp
    src/PowerSystem.cpp
    src/VoxelGame.cpp
)

target_include_directories(voxel-factory PRIVATE include)
target_link_libraries(voxel-factory PRIVATE engine)

# Copy the shaders next to the executable so it can load them at runtime
# (resolved relative to SDL_GetBasePath()).
add_custom_command(TARGET voxel-factory POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:voxel-factory>/shaders
    COMMENT "Copying shaders to output directory"
)

# Copy SDL3.dll next to the executable on Windows so it runs in place.
add_custom_command(TARGET voxel-factory POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:voxel-factory>
    COMMENT "Copying SDL3 runtime library to output directory"
)
