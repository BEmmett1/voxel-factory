// Application lifecycle: startup (shaders, atlas, save-or-fresh-island),
// save/load plumbing, the Esc close-or-quit ladder, and the window title.
// The other concerns live in sibling files: VoxelGameWorldGen / Sim / Player /
// Render / Ui (shared knobs + helpers in VoxelGameInternal.h).

#include "engine/GL.h" // GL calls in onStart

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/SaveSystem.h"
#include "game/PowerSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

using namespace vg;

VoxelGame::VoxelGame()
    : engine::Application("Voxel Factory", 1280, 720) {}

// One-time migration: saves used to live under a developer-named org folder
// ("benny"). If the new location has no save yet and the old one does, copy
// it (and its .bak) over — copy, not move, so older builds keep working.
// The same logic makes any future kOrgName rename free.
static void migrateLegacySave(const std::string& newSavePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path newSave{newSavePath};
    if (fs::exists(newSave, ec)) return;
    // <pref root>/<org>/<app>/save.vxf -> three parents up is the pref root.
    const fs::path oldDir =
        newSave.parent_path().parent_path().parent_path() / "benny" / "voxel-factory";
    for (const char* name : {"save.vxf", "save.vxf.bak"}) {
        const fs::path from = oldDir / name;
        if (fs::exists(from, ec)) {
            fs::copy_file(from, newSave.parent_path() / name,
                          fs::copy_options::skip_existing, ec);
            if (ec) SDL_Log("Save migration failed for %s: %s",
                            from.string().c_str(), ec.message().c_str());
        }
    }
}

void VoxelGame::onStart() {
    glEnable(GL_DEPTH_TEST);
    window().setRelativeMouse(true); // capture the cursor for FPS look

    const char* base = SDL_GetBasePath(); // owned by SDL, do not free
    const std::string dir = base ? base : "";
    if (!m_shader.loadFromFiles(dir + "shaders/voxel.vert", dir + "shaders/voxel.frag")) {
        // Without the shader the game would "run" as a black window; fail
        // loudly instead (main() shows this in a message box).
        throw std::runtime_error("Failed to load voxel shaders from '" + dir +
                                 "shaders/' - the game files may be incomplete.");
    }
    m_shader.use();
    m_shader.setInt("uAtlas", 0); // atlas lives on texture unit 0

    buildAtlas();

    // The save and settings live in the OS-preferred data directory. Resolved
    // before audio setup so the loaded master volume applies from frame one.
    if (char* pref = SDL_GetPrefPath(kOrgName, kAppName)) {
        m_savePath = std::string(pref) + kSaveFile;
        m_settingsPath = std::string(pref) + kSettingsFile;
        SDL_free(pref);
        migrateLegacySave(m_savePath);
    }
    SettingsIO::load(m_settingsPath, m_settings); // missing/invalid = defaults
    applySettings(); // fullscreen/vsync/volume (window + audio exist by now)

    audio().loadDirectory(dir + "assets/sounds");
    // Rain ambience runs for the whole session; the per-frame intensity ease
    // in onUpdate drives its gain (silent while clear).
    m_rainLoop = audio().createLoop("rain_loop", /*spatial=*/false, 0.0f);

    m_creatures.loadAssets(dir); // .bbmodel + entity shader; creatureless on failure

    // Hotbar: ten player-assigned slots, curated in the Tab inventory overlay
    // (keys 1-9 and 0 select; the wheel cycles). Seed the default BEFORE
    // loadGame() so a pre-v12 save keeps it, like the health default below.
    m_hotbar = kDefaultHotbar;

    m_world = std::make_unique<World>();
    m_player.health = kMaxHealth; // pre-v10 saves have no health field; keep this default
    if (!loadGame()) {
        // No (valid) save: fresh island + the starting kit of raw materials.
        // Everything placeable is hand-crafted from these.
        buildWorld();

        camera().position = spawnFeet() + glm::vec3(0.0f, kEyeHeight, 0.0f);
        camera().yaw = -90.0f;   // looking toward -Z (the demo row)
        camera().pitch = -15.0f;

        // Lean kit: exactly enough for the bootstrap pair (Generator +
        // Grinder from ingots) with the starting tree covering wood and fuel.
        // Everything after runs on mined raws and machine-made plates.
        m_inventory.add(ItemId::CopperOre, 12);
        m_inventory.add(ItemId::Stone, 8);
        m_inventory.add(ItemId::Sand, 4);
        m_inventory.add(ItemId::Crystal, 2);
        m_inventory.add(ItemId::Herb, 4);
        m_inventory.add(ItemId::Essence, 1);
    }

    // Chunks are born dirty, so the first remeshDirtyChunks() sweep (top of
    // the first onRender) builds every mesh; power just needs one seed solve.
    m_power = PowerSystem::solve(*m_world, m_machines, &m_hungryGenerators);
    updateHums(); // a loaded save's energized machines hum from frame one
    // Fresh each launch; not part of the save. A few blocks from the player
    // spawn, snapped to ground inside the system.
    m_creatures.spawnTestCreature(*m_world, spawnFeet() + glm::vec3(4.0f, 0.0f, -3.0f));
    buildHighlightMesh();
    buildCrosshairMesh();
    m_ui.init();

    updateTitle();
}

bool VoxelGame::saveGame() {
    if (m_savePath.empty() || !m_world) return false;
    int slot = m_selectedSlot;
    SaveData d{*m_world, m_inventory, m_machines, m_belts, m_sources, m_saplings,
               m_weather.raining, m_weather.timer, m_bucketFill,
               camera().position, camera().yaw, camera().pitch,
               m_worldSeed, m_sourceRng, slot, m_player.health, m_hotbar};
    return SaveSystem::save(m_savePath, d);
}

bool VoxelGame::loadGame() {
    if (m_savePath.empty()) return false;
    // Try the main file, then the .bak generation the save rotation keeps —
    // a save interrupted mid-write costs at most one session, not the island.
    for (const std::string& path : {m_savePath, m_savePath + ".bak"}) {
        int slot = 0;
        SaveData d{*m_world, m_inventory, m_machines, m_belts, m_sources, m_saplings,
                   m_weather.raining, m_weather.timer, m_bucketFill,
                   camera().position, camera().yaw, camera().pitch,
                   m_worldSeed, m_sourceRng, slot, m_player.health, m_hotbar};
        if (SaveSystem::load(path, d)) {
            // Pre-v12 saves carry slot indices up to the old ~20-entry hotbar.
            m_selectedSlot = std::clamp(slot, 0, kHotbarSlots - 1);
            return true;
        }
        // A partial read may have dirtied state; start clean before the next
        // candidate (or the fresh island the caller builds).
        m_world = std::make_unique<World>();
        m_inventory = Inventory{};
        m_machines.clear();
        m_belts.clear();
        m_sources.clear();
        m_saplings.clear();
        m_weather = Weather{};
        m_bucketFill = 0.0f;
        m_player.health = kMaxHealth;
        m_hotbar = kDefaultHotbar;
        m_sourceRng = 0;
        // Camera pose, seed, and slot need no reset: a .bak success or the
        // caller's fresh island overwrites them all.
    }
    return false;
}

void VoxelGame::onExit() {
    // Settings first: covers quitting with the settings panel still open.
    if (!m_settingsPath.empty()) SettingsIO::save(m_settingsPath, m_settings);
    saveGame(); // runs on every quit path (Esc, window close)
}

void VoxelGame::onEscape() {
    if (m_machineUiOpen) {
        if (m_drag.active()) {
            cancelDrag(); // first Esc returns the payload; second closes
        } else {
            closeMachineUi();
        }
    } else if (m_invOpen) {
        if (m_invDrag != ItemId::None) {
            m_invDrag = ItemId::None; // first Esc drops the drag; second closes
        } else {
            closeInventoryUi();
        }
    } else if (m_helpOpen) {
        m_helpOpen = false;
        audio().play("close", kUiVolume);
    } else if (m_menuOpen) {
        m_menuOpen = false;
        window().setRelativeMouse(true);
        audio().play("close", kUiVolume);
    } else if (m_settingsOpen) {
        // One level per press: key capture -> keybinds -> settings -> pause.
        // (processEvents routes Esc here BEFORE onUpdate, so the capture scan
        // in updateSettingsUi never sees this press.)
        if (m_bindCapture >= 0) {
            m_bindCapture = -1;
            audio().play("close", kUiVolume);
        } else if (m_bindsOpen) {
            m_bindsOpen = false;
            audio().play("close", kUiVolume);
        } else {
            closeSettingsUi();
        }
    } else if (m_pauseOpen) {
        closePauseMenu();
    } else {
        openPauseMenu(); // quitting lives on its SAVE AND QUIT row
    }
}

void VoxelGame::updateTitle() {
    const ItemId held = heldItem();
    const std::string holding =
        held == ItemId::None
            ? std::string("Nothing")
            : itemName(held) + (" x" + std::to_string(m_inventory.count(held)));
    window().setTitle(std::string("Voxel Factory v" VOXEL_FACTORY_VERSION
                                  "  —  Holding: ") + holding +
                      "   (F1 help / E craft / LMB mine / RMB place)");
}
