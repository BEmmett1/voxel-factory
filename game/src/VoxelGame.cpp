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

    // The save lives in the OS-preferred data directory.
    if (char* pref = SDL_GetPrefPath(kOrgName, kAppName)) {
        m_savePath = std::string(pref) + kSaveFile;
        SDL_free(pref);
        migrateLegacySave(m_savePath);
    }

    // Hotbar: every placeable item, in enum order. Keys 1-9 and 0 jump to the
    // first ten slots; the mouse wheel cycles through all of them.
    m_hotbar.clear();
    for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemId id = static_cast<ItemId>(i);
        // The bucket rides in the hotbar as a tool: hold it in the rain to
        // collect water. (The place path guards on `placeable`.)
        if (itemInfo(id).placeable || id == ItemId::Bucket) m_hotbar.push_back(id);
    }

    m_world = std::make_unique<World>();
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
    buildHighlightMesh();
    buildCrosshairMesh();
    m_ui.init();

    updateTitle();
}

bool VoxelGame::saveGame() {
    if (m_savePath.empty() || !m_world) return false;
    int slot = m_selectedSlot;
    SaveData d{*m_world, m_inventory, m_machines, m_belts, m_sources, m_saplings,
               m_weatherRaining, m_weatherTimer, m_bucketFill,
               camera().position, camera().yaw, camera().pitch,
               m_worldSeed, m_sourceRng, slot};
    return SaveSystem::save(m_savePath, d);
}

bool VoxelGame::loadGame() {
    if (m_savePath.empty()) return false;
    // Try the main file, then the .bak generation the save rotation keeps —
    // a save interrupted mid-write costs at most one session, not the island.
    for (const std::string& path : {m_savePath, m_savePath + ".bak"}) {
        int slot = 0;
        SaveData d{*m_world, m_inventory, m_machines, m_belts, m_sources, m_saplings,
                   m_weatherRaining, m_weatherTimer, m_bucketFill,
                   camera().position, camera().yaw, camera().pitch,
                   m_worldSeed, m_sourceRng, slot};
        if (SaveSystem::load(path, d)) {
            m_selectedSlot = std::clamp(slot, 0, static_cast<int>(m_hotbar.size()) - 1);
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
        m_weatherRaining = false;
        m_weatherTimer = 120.0f;
        m_bucketFill = 0.0f;
    }
    return false;
}

void VoxelGame::onExit() {
    saveGame(); // runs on every quit path (Esc, window close)
}

void VoxelGame::onEscape() {
    if (m_machineUiOpen) {
        if (m_drag.active()) {
            cancelDrag(); // first Esc returns the payload; second closes
        } else {
            closeMachineUi();
        }
    } else if (m_helpOpen) {
        m_helpOpen = false;
    } else if (m_menuOpen) {
        m_menuOpen = false;
        window().setRelativeMouse(true);
    } else if (m_pauseOpen) {
        closePauseMenu();
    } else {
        openPauseMenu(); // quitting lives on its SAVE AND QUIT row
    }
}

void VoxelGame::updateTitle() {
    const ItemId held = m_hotbar.empty() ? ItemId::None : m_hotbar[m_selectedSlot];
    window().setTitle(std::string("Voxel Factory v" VOXEL_FACTORY_VERSION
                                  "  —  Holding: ") + itemName(held) +
                      " x" + std::to_string(m_inventory.count(held)) +
                      "   (F1 help / E craft / LMB mine / RMB place)");
}
