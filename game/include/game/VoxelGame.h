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
#include "game/Settings.h"
#include "game/Recipes.h"
#include "game/Machine.h"
#include "game/Belt.h"
#include "game/CreatureSystem.h"
#include "game/Drop.h"
#include "game/Dimension.h"
#include "game/HashIVec3.h"
#include "game/PlayerController.h"
#include "game/Weather.h"
#include "game/PowerSystem.h"
#include "game/WorldEdit.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
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
    void buildShapeSheet();      // load assets/shapes.png (optional; loud if absent)
    void buildWorld();           // generate terrain + the demo structures
    void buildArena(World& w, SpeciesId boss); // the boss's arena variant
    void enterArena(SpeciesId boss); // consume-key travel: regen arena + boss, go
    void returnHome();               // back to m_homePose in the Overworld
    void remeshDirtyChunks();    // rebuild only changed chunks (once per frame)
    void updateShapeAnim();      // pick each shape's animation frame (a uniform, not a remesh)
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
    // The main menu shell shown on launch (NEW GAME / CONTINUE / SETTINGS /
    // QUIT), plus the save-slot picker it opens. The world is unbuilt behind
    // it (bare sky); choosing a slot builds or loads and begins play.
    void openMainMenu();
    void updateMainMenu();
    void drawMainMenu();
    void openSlotPicker(bool newGame); // newGame: create; else continue (load)
    void closeSlotPicker();            // back to the main menu
    void updateSlotPicker();
    void drawSlotPicker();
    void startNewGame(int slot);       // fresh island into a slot, begin play
    bool continueGame(int slot);       // load a slot, begin play; false on failure
    void startPlaying();               // shared post-load tail; leave the menu
    bool anySaveExists() const;        // is CONTINUE meaningful?
    void openSettingsUi();       // from the pause menu's SETTINGS row
    void closeSettingsUi();      // writes settings.cfg; back to the pause menu
    void updateSettingsUi();     // both panels + key capture
    void drawSettingsUi();
    void applySettings();        // push fullscreen/vsync/volume to the engine
    // The scancode bound to a gameplay action (UNKNOWN = unbound, inert).
    SDL_Scancode key(Action a) const { return m_settings.key(a); }
    void openMachineUi(const glm::ivec3& pos);
    void closeMachineUi();
    void updateMachineUi();      // keyboard + mouse interaction with the panel
    void drawMachineUi();
    // The Alchemy Circle's own panel: the eight pedestals drawn in their true
    // compass positions around the core. Every pattern is laid BY HAND, one
    // drag per pedestal -- the panel deliberately offers no blueprint list.
    // Reached through the machine panel's open/close plumbing (a Rune Core
    // dispatches here) so Esc, the cursor grab, and the drag all behave.
    void updateCircleUi();
    void drawCircleUi();
    void openInventoryUi();      // Tab: full inventory + hotbar assignment
    void closeInventoryUi();
    void updateInventoryUi();    // drag items onto hotbar / armor slots
    void drawInventoryUi();
    void recomputeArmor();       // refresh m_armorMitigation from m_armor
    bool canCraft(const Recipe& r) const;
    void tryCraft(const Recipe& r);
    void updateTitle();          // show the selected item in the window title
    // The selected hotbar item (None for an empty slot). m_selectedSlot stays
    // in [0, kHotbarSlots) — enforced at load, number keys, and wheel.
    ItemId heldItem() const { return m_hotbar[m_selectedSlot]; }

    bool saveGame();             // write the full game state to m_savePath
    bool loadGame();             // restore it; false = no/invalid save
    // The save file for a slot ("save_<n>.vxf" under the pref dir).
    std::string saveSlotPath(int slot) const;

    void registerMachine(const glm::ivec3& pos, BlockId type); // world-gen seeding
    void registerBelt(const glm::ivec3& pos, const glm::ivec3& facing);
    // The registry bundle WorldEdit keeps in sync with the block grid
    // (player edits go through WorldEdit::breakBlock / placeBlock).
    WorldEdit::Registries editRegistries() {
        return {m_machines, m_belts, m_sources, m_saplings};
    }
    void updateSources();                           // grow patches around sources
    void updateSaplings();                          // grow planted saplings into trees
    void updateGrassSpread();                       // grass creeps onto adjacent dirt (renewable)
    void updateLeafDecay();                         // wither leaves cut off from logs
    void rollLeafSapling(const glm::ivec3& p);      // sapling chance per lost leaf
    void buildRainMesh();                           // per-frame falling streaks
    void updateHums();                              // sync hum loops to power state
    void updateBucketFill();                        // held bucket catches rain
    // Spawn a physical item into the active dimension (mining yields + the
    // death-scattered pack); the player auto-collects it in updateDrops().
    void spawnDrop(const glm::vec3& pos, ItemId id, int count, float pickupDelay = 0.0f);
    void updateDrops();                             // fall/settle + proximity pickup (onTick)
    bool cellOverlapsPlayer(const glm::ivec3& p);   // would a block here clip the player?
    bool projectToScreen(const glm::vec3& world, glm::vec2& outPx);

    engine::Shader     m_shader;
    engine::Texture    m_atlas;
    std::unordered_map<glm::ivec3, engine::Mesh, IVec3Hash> m_chunkMeshes;
    std::vector<float> m_meshScratch;   // reused vertex staging buffer
    // Sub-cube block geometry (BlockShape.h) is a second pass per chunk: same
    // vertex layout, but UVs address shapes.png instead of the atlas. A world
    // with no shaped blocks uploads nothing here and draws nothing extra.
    engine::Texture    m_shapes;
    bool               m_shapesReady = false;
    std::unordered_map<glm::ivec3, engine::Mesh, IVec3Hash> m_chunkShapeMeshes;
    std::vector<float> m_shapeScratch;
    // Animated shape textures: one v-offset per ShapeId, recomputed each frame
    // from m_animClock and uploaded as a uniform array. Advancing a frame never
    // touches a mesh, so this is the whole per-frame cost of a bubbling
    // cauldron. The clock is pause-aware — machines freeze with the sim.
    // Sized to vg::kMaxShapeBanks on first use, like CreatureSystem's bone
    // scratch — which keeps the generated shape tables out of this header.
    float              m_animClock = 0.0f;
    std::vector<float> m_shapeAnimV;
    engine::Mesh       m_rainMesh;      // falling streaks, rebuilt per frame
    std::vector<float> m_rainScratch;
    engine::Mesh       m_highlightMesh;
    engine::Mesh       m_crosshairMesh;
    engine::UiRenderer m_ui;

    // Dimensions: one World per DimensionId; m_world points at the ACTIVE
    // one (player physics, raycast, rendering). The factory simulation and
    // every registry are Overworld-semantic and always operate on
    // overworld(), regardless of where the player stands.
    std::array<std::unique_ptr<World>, static_cast<std::size_t>(DimensionId::Count)> m_worlds;
    World* m_world = nullptr;                       // = m_worlds[m_dimension]
    DimensionId m_dimension = DimensionId::Overworld;
    World& overworld() { return *m_worlds[static_cast<std::size_t>(DimensionId::Overworld)]; }
    void switchDimension(DimensionId dim);          // swap world + meshes + ambience

    // Where the player left the Overworld; every return trip (victory, death,
    // save-and-load) lands here. Saved as the camera pose while in the arena.
    struct CameraPose {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
    };
    CameraPose m_homePose;

    PowerState m_power; // energized OVERWORLD cells; refreshed on every edit

    Inventory m_inventory;
    // Player-assigned hotbar slots (None = empty). Assignments are references
    // into m_inventory — counts never live here — so they survive hitting 0
    // (drawn greyed) and even the death wipe; restocking re-enables them.
    std::array<ItemId, kHotbarSlots> m_hotbar{};
    int m_selectedSlot = 0;

    // Equipped armor (head/body/feet; None = empty). Unlike hotbar slots these
    // HOLD the piece — it leaves the pack while worn, and rides the pack's fate
    // on death. m_armorMitigation caches the summed reduction (recomputeArmor).
    std::array<ItemId, kArmorSlots> m_armor{ItemId::None, ItemId::None, ItemId::None};
    float m_armorMitigation = 0.0f;

    std::unordered_map<glm::ivec3, Machine, IVec3Hash> m_machines;
    std::unordered_set<glm::ivec3, IVec3Hash> m_hungryGenerators; // networks wanting power
    std::unordered_map<glm::ivec3, Belt, IVec3Hash>    m_belts;
    std::unordered_map<glm::ivec3, float, IVec3Hash>   m_sources;  // pos -> spawn timer
    std::unordered_map<glm::ivec3, float, IVec3Hash>   m_saplings; // pos -> growth timer
    int m_beltTimer = 0;           // ticks since the last belt step
    int m_leafPity = 0;            // chopped leaves since the last sapling drop
    float m_leafDecayTimer = 0.0f; // seconds since the last leaf-decay pass
    std::uint32_t m_lootRng = 0x9E3779B9u; // rolls sift-pebble / leaf-stick drops
    std::uint32_t m_growthRng = 0xC2B2AE35u; // grass-spread cell sampling

    // The entity layer (test creature). Owns its model/GPU assets and
    // instances; spawned fresh each launch, deliberately NOT saved.
    CreatureSystem m_creatures;

    // Physical items on the ground (mining yields + a death-scattered pack).
    // Overworld drops are saved; the physics/pickup run in onTick.
    std::vector<DroppedItem> m_drops;

    engine::AudioLoop m_rainLoop = 0; // rain ambience; gain follows the intensity
    std::unordered_map<glm::ivec3, engine::AudioLoop, IVec3Hash> m_humLoops;

    Weather m_weather;          // rain/clear phases + eased visual intensity
    float m_bucketFill = 0.0f;  // held-bucket rain-collection progress

    bool  m_bossDefeated = false;    // ever beaten the Void Warden (saved, v13)
    bool  m_tempestDefeated = false; // ever beaten the Tempest (saved, v14)
    bool  m_arenaStorm = false;      // this arena visit rages (Tempest fights)
    float m_victoryTimer = -1.0f; // >0: victory linger, counting down to the ride home
    std::uint32_t m_worldSeed = 0; // per-launch seed for island + source layout
    std::uint32_t m_sourceRng = 0; // decorrelates node-spawn placement rolls
    std::string m_prefDir;         // the SDL pref dir root (saves + logs live here)
    std::string m_savePath;        // the active slot's save_<n>.vxf
    int    m_saveSlot = 0;         // which save slot is loaded (NOT the hotbar slot)
    double m_playtime = 0.0;       // total active-play seconds (drives slot cards)

    bool m_menuOpen = false;             // crafting menu visible?
    bool m_invOpen = false;              // inventory overlay (Tab) visible?
    bool m_helpOpen = false;             // F1 help overlay visible?
    bool m_debugOpen = false;            // F3 perf overlay visible?
    int  m_menuSelection = 0;
    bool m_pauseOpen = false;            // pause menu (Esc); sim time frozen
    int  m_pauseSel = 0;

    // Main-menu shell (launch): the world is unbuilt behind it until a slot is
    // chosen. m_shellOpen gates the boot menu; the slot picker rides on top.
    bool m_shellOpen = false;
    int  m_shellSel = 0;
    bool m_slotPickerOpen = false;
    bool m_slotPickerNew = false;        // true = create/overwrite; false = load
    int  m_slotSel = 0;
    int  m_slotConfirm = -1;             // slot index awaiting a confirm; -1 = none
    bool m_slotConfirmDelete = false;    // armed confirm is a delete, else overwrite

    // Settings panel (opened from the pause menu; m_pauseOpen stays true so
    // the sim stays frozen). m_bindCapture = the Action index awaiting a key.
    Settings    m_settings;
    std::string m_settingsPath;          // settings.cfg in the SDL pref dir
    bool m_settingsOpen = false;
    int  m_settingsSel  = 0;
    bool m_bindsOpen    = false;         // keybinds subpanel
    int  m_bindsSel     = 0;
    int  m_bindCapture  = -1;

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
    // Scroll offset (in grid ROWS) of the panel inventory grid, which is
    // windowed rather than drawn whole -- see InvWindow in VoxelGameUi.cpp.
    // Transient and shared by every panel that shows the grid; reset whenever
    // one opens so a panel never comes up scrolled somewhere unexpected.
    int        m_invScroll = 0;

    // Item being dragged inside the machine panel. The payload is removed from
    // its source at pickup and returned there on cancel/close (or to the
    // player if the source machine vanished), so items can't duplicate.
    struct Drag {
        enum class Source { None, PlayerInv, MachineIn, MachineOut, PedestalIn };
        Source source = Source::None;
        ItemId id     = ItemId::None;
        int    count  = 0;
        // PedestalIn only: which ring slot (0..7) of the open Rune Core the
        // payload came from, so a cancel puts it back on the right pedestal.
        int    slot   = -1;
        bool active() const { return source != Source::None && count > 0; }
    };
    Drag m_drag;
    void cancelDrag(); // return the payload to its source

    // Item being dragged inside the inventory overlay. Unlike m_drag this is
    // a reference — nothing leaves the inventory — so cancel is just a reset.
    ItemId m_invDrag = ItemId::None;

    bool       m_hasTarget = false;
    glm::ivec3 m_targetBlock{0};

    // Timed breaking: LMB-held progress against the aimed cell's hardness. Reset
    // when the aim leaves the cell or the button releases.
    bool       m_breaking = false;
    glm::ivec3 m_breakTarget{0};
    float      m_breakProgress = 0.0f; // seconds accumulated
    float      m_breakNeeded   = 0.0f; // seconds required (for the HUD bar)

    // The player's body: velocities + health + move/damage (health is public
    // on the controller so SaveData binds to it). Reaching 0 hp triggers the
    // same penalty as falling off the island: pack lost, respawn.
    PlayerController m_player;
    float m_attackCooldown = 0.0f; // seconds until the sword can swing again
    float m_castCooldown = 0.0f;   // seconds until the Mana Vial can cast again
    float m_vigorTimer = 0.0f;     // seconds of Elixir of Vigor buff remaining
};
