#pragma once

#include "engine/Application.h"
#include "engine/Shader.h"
#include "engine/Mesh.h"
#include "game/Chunk.h"
#include "game/Block.h"

#include <glm/glm.hpp>
#include <memory>

// The voxel automation game.
//   M0: render a face-culled voxel scene and fly around it.
//   M2: aim at blocks (center-screen raycast), break (LMB) and place (RMB) the
//       selected block type (number keys 1-7), with a wireframe target outline.
class VoxelGame : public engine::Application {
public:
    VoxelGame();

protected:
    void onStart() override;
    void onUpdate(float dt) override;
    void onRender() override;

private:
    void buildWorld();           // populate the chunk with the demo scene
    void rebuildMesh();          // (re)generate the GPU mesh from the chunk
    void buildHighlightMesh();   // unit wireframe cube used for the target outline
    void updateTitle();          // show the selected block in the window title

    engine::Shader m_shader;
    engine::Mesh   m_mesh;
    engine::Mesh   m_highlightMesh;
    std::unique_ptr<Chunk> m_chunk;

    BlockId    m_selectedBlock = BlockId::Stone;
    bool       m_hasTarget = false;   // is the center ray pointing at a block?
    glm::ivec3 m_targetBlock{0};
};
