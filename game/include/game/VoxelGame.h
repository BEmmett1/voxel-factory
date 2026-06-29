#pragma once

#include "engine/Application.h"
#include "engine/Shader.h"
#include "engine/Mesh.h"
#include "game/Chunk.h"

#include <memory>

// The voxel automation game. M0: render a small face-culled voxel scene and fly
// around it with WASD + mouse-look.
class VoxelGame : public engine::Application {
public:
    VoxelGame();

protected:
    void onStart() override;
    void onUpdate(float dt) override;
    void onRender() override;

private:
    void buildWorld();   // populate the chunk with the demo scene
    void rebuildMesh();  // (re)generate the GPU mesh from the chunk

    engine::Shader m_shader;
    engine::Mesh   m_mesh;
    std::unique_ptr<Chunk> m_chunk;
};
