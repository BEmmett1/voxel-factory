#pragma once

#include "engine/Application.h"
#include "engine/Shader.h"
#include "engine/Mesh.h"
#include "engine/Texture.h"
#include "engine/UiRenderer.h"
#include "game/World.h"
#include "game/Block.h"
#include "game/Item.h"
#include "game/Inventory.h"
#include "game/Recipes.h"
#include "game/Machine.h"
#include "game/Belt.h"
#include "game/HashIVec3.h"
#include "game/PowerSystem.h"

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

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
    void onTick() override;      // fixed 20 Hz machine processing
    void onRender() override;
    void onEscape() override;    // closes the crafting menu, else quits

private:
    void buildAtlas();           // procedurally generate the block texture atlas
    void buildWorld();           // generate terrain + the demo structures
    void rebuildMesh();          // recompute power and rebuild the combined mesh
    void buildHighlightMesh();   // unit wireframe cube for the target outline
    void buildCrosshairMesh();   // screen-space '+' at the center
    void drawHud();              // hotbar + machine overlays
    void drawCraftMenu();        // crafting menu overlay
    void drawHelp();             // F1 how-to-play overlay
    void updateMenu();           // crafting menu navigation + crafting
    bool canCraft(const Recipe& r) const;
    void tryCraft(const Recipe& r);
    void updateTitle();          // show the selected item in the window title

    void registerMachine(const glm::ivec3& pos, BlockId type);
    void unregisterMachine(const glm::ivec3& pos); // returns buffered items
    void registerBelt(const glm::ivec3& pos, const glm::ivec3& facing);
    void unregisterBelt(const glm::ivec3& pos);    // returns carried item
    void beltStep();                                // advance items along conduits
    bool updateSources();                           // grow patches; true if a node spawned
    bool projectToScreen(const glm::vec3& world, glm::vec2& outPx);

    engine::Shader     m_shader;
    engine::Texture    m_atlas;
    engine::Mesh       m_mesh;          // whole world, one combined buffer
    engine::Mesh       m_highlightMesh;
    engine::Mesh       m_crosshairMesh;
    engine::UiRenderer m_ui;

    std::unique_ptr<World> m_world;
    PowerState m_power; // energized cells; refreshed on every edit

    Inventory           m_inventory;
    std::vector<ItemId> m_hotbar;        // placeable items, selected by number keys
    int                 m_selectedSlot = 0;

    std::unordered_map<glm::ivec3, Machine, IVec3Hash> m_machines;
    std::unordered_map<glm::ivec3, Belt, IVec3Hash>    m_belts;
    std::unordered_map<glm::ivec3, float, IVec3Hash>   m_sources; // pos -> spawn timer
    int m_beltTimer = 0;           // ticks since the last belt step
    std::uint32_t m_worldSeed = 0; // per-launch seed for island + source layout
    std::uint32_t m_sourceRng = 0; // decorrelates node-spawn placement rolls

    bool m_menuOpen = false;             // crafting menu visible?
    bool m_helpOpen = false;             // F1 help overlay visible?
    int  m_menuSelection = 0;

    bool       m_hasTarget = false;
    glm::ivec3 m_targetBlock{0};
};
