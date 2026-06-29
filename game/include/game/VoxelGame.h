#pragma once

#include "engine/Application.h"
#include "engine/Shader.h"
#include "engine/Mesh.h"
#include "engine/Texture.h"
#include "game/World.h"
#include "game/Block.h"
#include "game/PowerSystem.h"

#include <glm/glm.hpp>
#include <memory>

// The voxel automation game.
//   M0: render a face-culled voxel scene and fly around it.
//   M2: aim (center-screen raycast), break (LMB) / place (RMB) the selected
//       block (number keys 1-7), with a wireframe target outline.
//   M4: power networks light up (generator -> wire -> machine).
//   M1: multi-chunk world, a procedurally textured block atlas, and a crosshair.
class VoxelGame : public engine::Application {
public:
    VoxelGame();

protected:
    void onStart() override;
    void onUpdate(float dt) override;
    void onRender() override;

private:
    void buildAtlas();           // procedurally generate the block texture atlas
    void buildWorld();           // generate terrain + the demo structures
    void rebuildMesh();          // recompute power and rebuild the combined mesh
    void buildHighlightMesh();   // unit wireframe cube for the target outline
    void buildCrosshairMesh();   // screen-space '+' at the center
    void updateTitle();          // show the selected block in the window title

    engine::Shader  m_shader;
    engine::Texture m_atlas;
    engine::Mesh    m_mesh;          // whole world, one combined buffer
    engine::Mesh    m_highlightMesh;
    engine::Mesh    m_crosshairMesh;

    std::unique_ptr<World> m_world;
    PowerState m_power; // energized cells; refreshed on every edit

    BlockId    m_selectedBlock = BlockId::Stone;
    bool       m_hasTarget = false;
    glm::ivec3 m_targetBlock{0};
};
