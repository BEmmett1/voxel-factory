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
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    void onEscape() override;    // closes the topmost overlay, else pause menu
    void onExit() override;      // save the game on any quit path

private:
    void buildAtlas();           // load assets/atlas.png or generate a fallback
    void buildWorld();           // generate terrain + the demo structures
    void remeshDirtyChunks();    // rebuild only changed chunks (once per frame)
    void solvePowerAndMarkDirty(); // recompute power; queue glow-changed chunks
    void buildHighlightMesh();   // unit wireframe cube for the target outline
    void buildCrosshairMesh();   // screen-space '+' at the center
    void drawHud();              // hotbar + machine overlays
    void drawCraftMenu();        // crafting menu overlay
    void drawHelp();             // F1 how-to-play overlay
    void drawDebugOverlay();     // F3 perf readout
    void updateMenu();           // crafting menu navigation + crafting
    void openPauseMenu();        // freezes the simulation (engine setPaused)
    void closePauseMenu();       // resume
    void updatePauseMenu();      // keyboard + mouse interaction
    void drawPauseMenu();
    void openMachineUi(const glm::ivec3& pos);
    void closeMachineUi();
    void updateMachineUi();      // keyboard + mouse interaction with the panel
    void drawMachineUi();
    bool canCraft(const Recipe& r) const;
    void tryCraft(const Recipe& r);
    void updateTitle();          // show the selected item in the window title

    bool saveGame();             // write the full game state to m_savePath
    bool loadGame();             // restore it; false = no/invalid save

    void registerMachine(const glm::ivec3& pos, BlockId type);
    void unregisterMachine(const glm::ivec3& pos); // returns buffered items
    void registerBelt(const glm::ivec3& pos, const glm::ivec3& facing);
    void unregisterBelt(const glm::ivec3& pos);    // returns carried item
    void beltStep();                                // advance items along conduits
    void updateSources();                           // grow patches around sources
    void updateSaplings();                          // grow planted saplings into trees
    void updateLeafDecay();                         // wither leaves cut off from logs
    void rollLeafSapling(const glm::ivec3& p);      // sapling chance per lost leaf
    void updateWeather();                           // advance the rain/clear phases
    bool skyVisible(int wx, int wy, int wz) const;  // nothing solid above this cell?
    void buildRainMesh();                           // per-frame falling streaks
    void updateGeneratorsAndBarrels();              // burn fuel / collect rain
    void updateBucketFill();                        // held bucket catches rain
    bool cellOverlapsPlayer(const glm::ivec3& p);   // would a block here clip the player?
    bool projectToScreen(const glm::vec3& world, glm::vec2& outPx);

    engine::Shader     m_shader;
    engine::Texture    m_atlas;
    std::unordered_map<glm::ivec3, engine::Mesh, IVec3Hash> m_chunkMeshes;
    std::vector<float> m_meshScratch;   // reused vertex staging buffer
    engine::Mesh       m_rainMesh;      // falling streaks, rebuilt per frame
    std::vector<float> m_rainScratch;
    engine::Mesh       m_highlightMesh;
    engine::Mesh       m_crosshairMesh;
    engine::UiRenderer m_ui;

    std::unique_ptr<World> m_world;
    PowerState m_power; // energized cells; refreshed on every edit

    Inventory           m_inventory;
    std::vector<ItemId> m_hotbar;        // placeable items, selected by number keys
    int                 m_selectedSlot = 0;

    std::unordered_map<glm::ivec3, Machine, IVec3Hash> m_machines;
    std::unordered_set<glm::ivec3, IVec3Hash> m_hungryGenerators; // networks wanting power
    std::unordered_map<glm::ivec3, Belt, IVec3Hash>    m_belts;
    std::unordered_map<glm::ivec3, float, IVec3Hash>   m_sources;  // pos -> spawn timer
    std::unordered_map<glm::ivec3, float, IVec3Hash>   m_saplings; // pos -> growth timer
    int m_beltTimer = 0;           // ticks since the last belt step
    int m_leafPity = 0;            // chopped leaves since the last sapling drop
    float m_leafDecayTimer = 0.0f; // seconds since the last leaf-decay pass

    bool  m_weatherRaining = false;
    float m_weatherTimer = 120.0f; // seconds left in the current weather phase
    float m_rainIntensity = 0.0f;  // smoothed 0..1; gameplay uses the bool
    float m_bucketFill = 0.0f;     // held-bucket rain-collection progress
    std::uint32_t m_worldSeed = 0; // per-launch seed for island + source layout
    std::uint32_t m_sourceRng = 0; // decorrelates node-spawn placement rolls
    std::string m_savePath;        // save.vxf in the SDL pref dir

    bool m_menuOpen = false;             // crafting menu visible?
    bool m_helpOpen = false;             // F1 help overlay visible?
    bool m_debugOpen = false;            // F3 perf overlay visible?
    int  m_menuSelection = 0;
    bool m_pauseOpen = false;            // pause menu (Esc); sim time frozen
    int  m_pauseSel = 0;

    // Rolling frame times + costs of the heavy passes, shown by F3.
    struct PerfStats {
        static constexpr int Window = 120;   // ~2 s of frames
        float frameMs[Window] = {};
        int   frameIdx = 0;
        float avgMs = 0.0f, worstMs = 0.0f;  // over the window
        float lastRemeshMs = 0.0f;
        int   chunksRemeshed = 0;            // chunks rebuilt by the last remesh
        float lastSolveMs = 0.0f;
        int   remeshCount = 0, solveCount = 0;      // accumulating this second
        int   remeshesPerSec = 0, solvesPerSec = 0; // latched once per second
        float secondTimer = 0.0f;
    };
    PerfStats m_perf;

    bool       m_machineUiOpen = false;  // machine panel (RMB on a machine)
    glm::ivec3 m_machineUiPos{0};
    int        m_machineUiSel = 0;

    // Item being dragged inside the machine panel. The payload is removed from
    // its source at pickup and returned there on cancel/close (or to the
    // player if the source machine vanished), so items can't duplicate.
    struct Drag {
        enum class Source { None, PlayerInv, MachineIn, MachineOut };
        Source source = Source::None;
        ItemId id     = ItemId::None;
        int    count  = 0;
        bool active() const { return source != Source::None && count > 0; }
    };
    Drag m_drag;
    void cancelDrag(); // return the payload to its source

    bool       m_hasTarget = false;
    glm::ivec3 m_targetBlock{0};

    float     m_velY = 0.0f;      // vertical velocity (gravity/jump)
    glm::vec3 m_velXZ{0.0f};      // horizontal velocity (accel/friction; y unused)
    bool      m_grounded = false; // standing on something this frame?
};
