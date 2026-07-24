// Application lifecycle: startup (shaders, atlas, save-or-fresh-island),
// save/load plumbing, the Esc close-or-quit ladder, and the window title.
// The other concerns live in sibling files: VoxelGameWorldGen / Sim / Player /
// Render / Ui (shared knobs + helpers in VoxelGameInternal.h).

#include "engine/GL.h" // GL calls in onStart

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/SaveSystem.h"
#include "game/PowerSystem.h"

#include "engine/Paths.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

using namespace vg;

VoxelGame::VoxelGame()
    : engine::Application("Voxel Factory", 1280, 720) {}

// One-time migrations run at startup, both copy-not-move so older builds keep
// working. (1) Saves used to live under a developer-named org folder ("benny");
// this makes any future kOrgName rename free too. (2) Pre-slot builds kept a
// single save.vxf; adopt it as slot 0 so a returning player's world appears
// under Continue.
static void migrateLegacySave(const std::string& prefDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path appDir{prefDir};
    if (appDir.filename().empty()) appDir = appDir.parent_path(); // drop trailing sep
    // appDir == <pref root>/<org>/<app>.

    auto copyIfMissing = [&](const fs::path& from, const fs::path& to) {
        if (fs::exists(to, ec) || !fs::exists(from, ec)) return;
        fs::copy_file(from, to, fs::copy_options::skip_existing, ec);
        if (ec) SDL_Log("Save migration failed for %s: %s",
                        from.string().c_str(), ec.message().c_str());
    };

    // (1) Old org folder -> this pref dir's single save.vxf (+ .bak).
    if (!fs::exists(appDir / "save.vxf", ec)) {
        const fs::path oldDir =
            appDir.parent_path().parent_path() / "benny" / "voxel-factory";
        copyIfMissing(oldDir / "save.vxf", appDir / "save.vxf");
        copyIfMissing(oldDir / "save.vxf.bak", appDir / "save.vxf.bak");
    }
    // (2) Single save.vxf -> slot 0 (save_0.vxf + .bak).
    copyIfMissing(appDir / "save.vxf", appDir / "save_0.vxf");
    copyIfMissing(appDir / "save.vxf.bak", appDir / "save_0.vxf.bak");
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

    // The saves and settings live in the OS-preferred data directory. Resolved
    // before audio setup so the loaded master volume applies from frame one.
    m_prefDir = engine::prefDir(kOrgName, kAppName);
    if (!m_prefDir.empty()) {
        m_settingsPath = m_prefDir + kSettingsFile;
        migrateLegacySave(m_prefDir);
    }
    m_saveSlot = 0;
    m_savePath = saveSlotPath(m_saveSlot);
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

    for (auto& w : m_worlds) w = std::make_unique<World>();
    m_world = m_worlds[static_cast<std::size_t>(DimensionId::Overworld)].get();
    m_player.health = kMaxHealth; // pre-v10 saves have no health field; keep this default

    buildHighlightMesh();
    buildCrosshairMesh();
    m_ui.init();

    // Boot into the main menu over an empty world (bare sky as the backdrop).
    // NEW GAME / CONTINUE build or load a slot and begin play (startPlaying);
    // nothing is simulated until then.
    openMainMenu();
    updateTitle();
}

// Fresh island + the starting kit into a chosen slot, then begin play.
// Everything placeable is hand-crafted from these raws.
void VoxelGame::startNewGame(int slot) {
    m_saveSlot = slot;
    m_savePath = saveSlotPath(slot);

    buildWorld();

    camera().position = spawnFeet() + glm::vec3(0.0f, kEyeHeight, 0.0f);
    camera().yaw = -90.0f;   // looking toward -Z (the demo row)
    camera().pitch = -15.0f;

    // Empty kit: the hard start. You bootstrap from the world by hand — punch
    // leaves for Sticks, sift dirt/grass for Pebbles, craft Wood tools, mine
    // Stone, climb the Stone -> Copper tool ladder. Nothing is handed to you.
    // (F6 stays the dev shortcut for testing the later game.)
    m_armor.fill(ItemId::None); // a fresh start is unarmored

    startPlaying();
}

// Load a slot and begin play. Returns false (menu stays up) if the save is
// missing or unreadable — loadGame() leaves state clean on failure.
bool VoxelGame::continueGame(int slot) {
    m_saveSlot = slot;
    m_savePath = saveSlotPath(slot);
    if (!loadGame()) return false;
    startPlaying();
    return true;
}

// The tail shared by New and Continue: seed power/audio, spawn the transient
// creature, and drop out of the menu into live play.
void VoxelGame::startPlaying() {
    // Chunks are born dirty, so the first remeshDirtyChunks() sweep (top of the
    // first onRender) builds every mesh; power just needs one seed solve.
    m_power = PowerSystem::solve(*m_world, m_machines, &m_hungryGenerators);
    recomputeArmor(); // seed mitigation from a loaded save's equipped armor
    updateHums(); // a loaded save's energized machines hum from frame one
    // Fresh each launch; not part of the save. A few blocks from spawn, snapped
    // to ground inside the system.
    m_creatures.spawn(SpeciesId::TestCreature, DimensionId::Overworld, overworld(),
                      spawnFeet() + glm::vec3(4.0f, 0.0f, -3.0f));

    m_shellOpen = false;
    m_slotPickerOpen = false;
    m_slotConfirm = -1;
    setPaused(false);
    window().setRelativeMouse(true); // recapture the cursor for FPS look
    updateTitle();
    audio().play("close", kUiVolume);
}

std::string VoxelGame::saveSlotPath(int slot) const {
    return m_prefDir + "save_" + std::to_string(slot) + ".vxf";
}

bool VoxelGame::saveGame() {
    if (m_savePath.empty() || !m_world) return false;
    int slot = m_selectedSlot;
    // The arena is transient: only the Overworld is ever saved, and a save
    // taken mid-fight records the remembered home pose so loading always
    // wakes the player at home (quitting abandons the fight).
    glm::vec3 pos = camera().position;
    float yaw = camera().yaw, pitch = camera().pitch;
    if (m_dimension != DimensionId::Overworld) {
        pos = m_homePose.position;
        yaw = m_homePose.yaw;
        pitch = m_homePose.pitch;
    }
    SaveData d{overworld(), m_inventory, editRegistries(), m_weather, m_player,
               m_bucketFill, pos, yaw, pitch,
               m_worldSeed, m_sourceRng, slot, m_hotbar, m_bossDefeated,
               m_tempestDefeated, m_playtime, m_drops, m_armor};
    return SaveSystem::save(m_savePath, d);
}

bool VoxelGame::loadGame() {
    if (m_savePath.empty()) return false;
    // Try the main file, then the .bak generation the save rotation keeps —
    // a save interrupted mid-write costs at most one session, not the island.
    for (const std::string& path : {m_savePath, m_savePath + ".bak"}) {
        int slot = 0;
        SaveData d{overworld(), m_inventory, editRegistries(), m_weather, m_player,
                   m_bucketFill, camera().position, camera().yaw, camera().pitch,
                   m_worldSeed, m_sourceRng, slot, m_hotbar, m_bossDefeated,
                   m_tempestDefeated, m_playtime, m_drops, m_armor};
        if (SaveSystem::load(path, d)) {
            // Pre-v12 saves carry slot indices up to the old ~20-entry hotbar.
            m_selectedSlot = std::clamp(slot, 0, kHotbarSlots - 1);
            return true;
        }
        // A partial read may have dirtied state; start clean before the next
        // candidate (or the fresh island the caller builds).
        m_worlds[static_cast<std::size_t>(DimensionId::Overworld)] = std::make_unique<World>();
        m_world = m_worlds[static_cast<std::size_t>(DimensionId::Overworld)].get();
        m_inventory = Inventory{};
        m_machines.clear();
        m_belts.clear();
        m_sources.clear();
        m_saplings.clear();
        m_drops.clear();
        m_armor.fill(ItemId::None);
        m_weather = Weather{};
        m_bucketFill = 0.0f;
        m_player.health = kMaxHealth;
        m_hotbar = kDefaultHotbar;
        m_bossDefeated = false;
        m_tempestDefeated = false;
        m_playtime = 0.0;
        m_sourceRng = 0;
        // Camera pose, seed, and slot need no reset: a .bak success or the
        // caller's fresh island overwrites them all.
    }
    return false;
}

// Travel to a boss arena: the key was just consumed. The arena world is
// regenerated from scratch (transient fights — no state survives between
// visits) and the chosen boss spawns fresh in its lair. The one BossArena
// dimension hosts whichever fight the key opened.
void VoxelGame::enterArena(SpeciesId boss) {
    m_homePose = {camera().position, camera().yaw, camera().pitch};
    m_arenaStorm = (boss == SpeciesId::Tempest); // its storm never breaks

    auto& arena = m_worlds[static_cast<std::size_t>(DimensionId::BossArena)];
    arena = std::make_unique<World>();
    buildArena(*arena, boss);
    m_creatures.clearDimension(DimensionId::BossArena);
    m_creatures.spawn(boss, DimensionId::BossArena, *arena, bossSpawnFeet());

    switchDimension(DimensionId::BossArena);
    camera().position = arenaSpawnFeet() + glm::vec3(0.0f, kEyeHeight, 0.0f);
    camera().yaw = -90.0f;  // facing -Z: straight at the warden's lair
    camera().pitch = -5.0f;
    m_victoryTimer = -1.0f;
    audio().play("craft", kCraftVolume); // the shimmer of the key discharging
    updateTitle();
}

// Every road home — victory, death, or a save-and-quit-and-load — lands at
// the pose the player left from (death overrides with the plateau respawn).
void VoxelGame::returnHome() {
    switchDimension(DimensionId::Overworld);
    camera().position = m_homePose.position;
    camera().yaw = m_homePose.yaw;
    camera().pitch = m_homePose.pitch;
    m_creatures.clearDimension(DimensionId::BossArena);
    m_arenaStorm = false;
    m_victoryTimer = -1.0f;
    audio().play("craft", kCraftVolume);
    updateTitle();
}

// Swap the active world. The mesh cache belongs to the active dimension:
// drop it and re-dirty every chunk of the target so the per-frame sweep
// rebuilds the scene (a one-time hitch on travel, ~a frame). Overworld
// ambience — machine hums — pauses while away; the rain loop's gain is
// gated per frame where it is driven.
void VoxelGame::switchDimension(DimensionId dim) {
    if (dim == m_dimension) return;
    m_dimension = dim;
    m_world = m_worlds[static_cast<std::size_t>(dim)].get();

    m_chunkMeshes.clear();
    for (const auto& [coord, chunk] : m_world->chunks()) {
        chunk->markDirty();
    }

    const bool home = (dim == DimensionId::Overworld);
    for (const auto& [pos, h] : m_humLoops) {
        audio().setLoopPaused(h, !home);
    }
}

void VoxelGame::onExit() {
    // Settings first: covers quitting with the settings panel still open.
    if (!m_settingsPath.empty()) SettingsIO::save(m_settingsPath, m_settings);
    // Only persist an actual game. Quitting from the main menu (no slot chosen)
    // must never overwrite a slot with the empty backdrop world.
    if (!m_shellOpen) saveGame(); // runs on every in-game quit path (Esc, window close)
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
    } else if (m_slotPickerOpen) {
        // A confirm disarms first; otherwise back out to the main menu.
        if (m_slotConfirm >= 0) {
            m_slotConfirm = -1;
            audio().play("close", kUiVolume);
        } else {
            closeSlotPicker();
        }
    } else if (m_shellOpen) {
        // Top-level main menu: Esc is inert (QUIT exits deliberately).
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
