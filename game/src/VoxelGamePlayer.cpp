// The per-frame player: mouse look, walking physics (axis-separated
// move-and-slide, gravity, jump, the void-fall penalty), hotbar selection,
// and the aim/mine/place/wrench edit path. Menus intercept input up top.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/WorldEdit.h"
#include "game/Raycast.h"
#include "game/PowerSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

using namespace vg;

namespace {

    // Small per-cell pitch variation (±10%) so repeated mining/placing at
    // different spots doesn't sound machine-gun identical.
    float pitchJitter(const glm::ivec3& p) {
        return 1.0f + (static_cast<int>(hash2(p.x * 31 + p.y, p.z, 517u) % 21u) - 10) * 0.01f;
    }

} // namespace

void VoxelGame::onUpdate(float dt) {
    auto& cam = camera();

    // Perf bookkeeping runs every frame, even while menus own the input.
    m_perf.frameMs[m_perf.frameIdx] = dt * 1000.0f;
    m_perf.frameIdx = (m_perf.frameIdx + 1) % PerfStats::Window;
    float sum = 0.0f, worst = 0.0f;
    for (float ms : m_perf.frameMs) {
        sum += ms;
        worst = std::max(worst, ms);
    }
    m_perf.avgMs = sum / static_cast<float>(PerfStats::Window);
    m_perf.worstMs = worst;
    m_perf.secondTimer += dt;
    if (m_perf.secondTimer >= 1.0f) {
        m_perf.secondTimer = 0.0f;
        m_perf.remeshesPerSec = m_perf.remeshCount;
        m_perf.solvesPerSec = m_perf.solveCount;
        m_perf.remeshCount = 0;
        m_perf.solveCount = 0;
    }
    if (input().wasKeyPressed(SDL_SCANCODE_F3)) {
        m_debugOpen = !m_debugOpen;
    }

    // Entity animation clocks tick at render rate (menus keep animating, just
    // like onTick keeps simulating); a true pause freezes them.
    if (!paused()) {
        m_creatures.frameAdvance(dt);
        m_attackCooldown = std::max(0.0f, m_attackCooldown - dt);

        // Victory linger: soak in the win, then ride home automatically.
        if (m_victoryTimer > 0.0f) {
            m_victoryTimer -= dt;
            if (m_victoryTimer <= 0.0f) returnHome();
        }
    }

    // Pause menu: simulated time is frozen (the engine skips onTick while
    // paused); the menu owns all input until it resumes or quits. The
    // settings panel lives inside the pause (sim stays frozen there).
    if (m_pauseOpen) {
        if (m_settingsOpen) {
            updateSettingsUi();
        } else {
            updatePauseMenu();
        }
        return;
    }

    // F6 is a dev key (the F4 precedent): the boss-testing kit — a Teleport
    // Key + Copper Sword, assigned onto the hotbar so they are usable at once
    // (the default hotbar is full; the last two slots are sacrificed).
    if (input().wasKeyPressed(SDL_SCANCODE_F6)) {
        auto give = [&](ItemId id, int fallbackSlot) {
            m_inventory.add(id, 1);
            for (ItemId s : m_hotbar) {
                if (s == id) return; // already assigned
            }
            for (ItemId& s : m_hotbar) {
                if (s == ItemId::None) { s = id; return; }
            }
            m_hotbar[fallbackSlot] = id;
        };
        give(ItemId::TeleportKey, kHotbarSlots - 2);
        give(ItemId::CopperSword, kHotbarSlots - 1);
        updateTitle();
        audio().play("craft", kCraftVolume);
    }

    // Weather visuals ease in and out; F4 is a dev key to summon/clear rain.
    // The rain loop is Overworld ambience — silent in the arena.
    m_weather.frameEase(dt);
    audio().setLoopGain(m_rainLoop,
                        m_dimension == DimensionId::Overworld
                            ? m_weather.intensity * kRainVolume : 0.0f);
    if (input().wasKeyPressed(SDL_SCANCODE_F4)) {
        m_weather.forceToggle();
    }

    // Machine panel: owns all input while open.
    if (m_machineUiOpen) {
        updateMachineUi();
        return;
    }

    // Quick-save (default F5; also happens automatically on quit).
    if (input().wasKeyPressed(key(Action::QuickSave))) {
        if (saveGame()) {
            window().setTitle("Voxel Factory  —  SAVED");
        }
    }

    // Help overlay (default F1). While open it freezes the world.
    if (input().wasKeyPressed(key(Action::Help))) {
        m_helpOpen = !m_helpOpen;
        audio().play(m_helpOpen ? "open" : "close", kUiVolume);
        if (m_menuOpen) {
            m_menuOpen = false;
            window().setRelativeMouse(true);
        }
        if (m_invOpen) {
            m_invOpen = false;
            m_invDrag = ItemId::None;
            window().setRelativeMouse(true);
        }
    }
    if (m_helpOpen) {
        return;
    }

    // Crafting menu (default E). While open it owns the input and freezes
    // the world; the cursor is released for hover/click.
    if (input().wasKeyPressed(key(Action::CraftMenu))) {
        m_menuOpen = !m_menuOpen;
        window().setRelativeMouse(!m_menuOpen);
        audio().play(m_menuOpen ? "open" : "close", kUiVolume);
        if (m_invOpen) { // the overlays are mutually exclusive
            m_invOpen = false;
            m_invDrag = ItemId::None;
        }
    }
    if (m_menuOpen) {
        updateMenu();
        return;
    }

    // Inventory overlay (default Tab). While open it owns the input;
    // drag items onto the hotbar strip to assign them.
    if (input().wasKeyPressed(key(Action::Inventory))) {
        if (m_invOpen) {
            closeInventoryUi();
        } else {
            openInventoryUi();
        }
    }
    if (m_invOpen) {
        updateInventoryUi();
        return;
    }

    // The body: look, walk, gravity, fall damage, and the hardcore death
    // rule. On death the controller respawns the body; the pack-loss penalty
    // is applied here. Hotbar ASSIGNMENTS deliberately survive (they're
    // references, not items): the slots grey out at count 0 and re-enable as
    // the pack is rebuilt.
    const PlayerController::MoveResult mv =
        m_player.move(dt, input(), cam, *m_world, m_settings, audio());
    if (mv.died) {
        m_inventory = Inventory{};
        // Death in the arena ends the fight: the respawn plateau is an
        // Overworld place, so the dimension follows the body home.
        if (m_dimension != DimensionId::Overworld) {
            switchDimension(DimensionId::Overworld);
            m_creatures.clearDimension(DimensionId::BossArena);
            m_victoryTimer = -1.0f;
        }
        window().setTitle(mv.fellOff
            ? "Voxel Factory  —  YOU FELL. YOUR PACK IS LOST."
            : "Voxel Factory  —  YOU DIED. YOUR PACK IS LOST.");
    }

    // Hotbar selection: keys 1-9 and 0 map to the ten slots; the mouse wheel
    // cycles through all of them, empty slots included (scroll up = previous).
    for (int n = 0; n < kHotbarSlots; ++n) {
        const SDL_Scancode sc = (n < 9)
            ? static_cast<SDL_Scancode>(SDL_SCANCODE_1 + n)
            : SDL_SCANCODE_0;
        if (input().wasKeyPressed(sc)) {
            m_selectedSlot = n;
            updateTitle();
            audio().play("click", kUiVolume * 0.5f);
        }
    }
    const int wheel = input().wheelSteps();
    if (wheel != 0) {
        m_selectedSlot =
            ((m_selectedSlot - wheel) % kHotbarSlots + kHotbarSlots) % kHotbarSlots;
        updateTitle();
        audio().play("click", kUiVolume * 0.5f);
    }

    const ItemId held = heldItem();

    // Tools that consume the right click outright: a sip of the Healing
    // Draught, or the Teleport Key discharging into a trip to the arena.
    bool drank = false;
    if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
        if (held == ItemId::HealingDraught && m_player.health < kMaxHealth &&
            m_inventory.has(held)) {
            m_inventory.remove(held, 1);
            m_player.health = std::min(kMaxHealth, m_player.health + kDraughtHeal);
            audio().play("heal", kHurtVolume);
            updateTitle();
            drank = true;
        } else if (held == ItemId::TeleportKey && m_inventory.has(held) &&
                   m_dimension == DimensionId::Overworld) {
            // The expensive ticket, consumed on use; return trips are free.
            m_inventory.remove(held, 1);
            enterArena();
            drank = true;
        }
    }

    // Sword: LMB swings along the aim ray, creatures first (needs no block
    // under the crosshair). A connected swing consumes the click; a miss
    // whooshes and falls through to mining.
    bool swordHit = false;
    if (input().wasMousePressed(SDL_BUTTON_LEFT) && m_attackCooldown <= 0.0f &&
        held == ItemId::CopperSword && m_inventory.has(held)) {
        m_attackCooldown = kSwordCooldown;
        audio().play("swing", kUiVolume);
        const CreatureSystem::MeleeResult mr = m_creatures.tryMeleeAttack(
            *m_world, audio(), cam.position, cam.front(), m_dimension);
        swordHit = mr.hit;
        if (mr.bossDied) {
            // VICTORY: the unique drop lands in the pack, the progression
            // flag sticks (saved), and the linger timer starts the ride home.
            m_inventory.add(mr.drop, 1);
            m_bossDefeated = true;
            m_victoryTimer = kVictorySeconds;
            window().setTitle("Voxel Factory  —  THE VOID WARDEN FALLS. VICTORY!");
            audio().play("craft", kCraftVolume);
        }
    }

    // Aim and edit.
    const RaycastHit aim = raycastVoxel(*m_world, cam.position, cam.front(), kReach);
    m_hasTarget = aim.hit;
    m_targetBlock = aim.block;

    // Editing (and the machine panel) is Overworld-only: the arena is
    // transient, and the registries are Overworld-semantic — a machine
    // "found" at arena coordinates would be a home machine at overlapping
    // numbers. Attempted edits deny audibly; the sword still works.
    if (aim.hit && m_dimension != DimensionId::Overworld) {
        if ((!swordHit && input().wasMousePressed(SDL_BUTTON_LEFT)) ||
            (!drank && input().wasMousePressed(SDL_BUTTON_RIGHT))) {
            audio().play("deny", kCraftVolume);
        }
    } else if (aim.hit) {
        const glm::ivec3 tb = aim.block;

        // Mine: break the block (WorldEdit keeps the registries + power in
        // sync) and collect its drop plus any handed-back buffered items.
        if (!swordHit && input().wasMousePressed(SDL_BUTTON_LEFT)) {
            const WorldEdit::BreakResult r =
                WorldEdit::breakBlock(*m_world, editRegistries(), tb);
            m_inventory.add(r.drop.id, r.drop.count);
            for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                m_inventory.add(id, r.returned.count(id));
            }
            if (r.brokeLeaves) {
                rollLeafSapling(tb);
            }
            audio().playAt("mine", glm::vec3(tb) + glm::vec3(0.5f), kMineVolume,
                           pitchJitter(tb));
            if (r.powerChanged) solvePowerAndMarkDirty();
            updateTitle();
        }
        // RMB: on a machine, open its panel (Shift+RMB to place against it
        // instead); otherwise place the held item into the empty target cell.
        if (!drank && input().wasMousePressed(SDL_BUTTON_RIGHT)) {
            const bool aimedMachine = m_machines.find(tb) != m_machines.end();
            if (aimedMachine && !input().isKeyDown(SDL_SCANCODE_LSHIFT)) {
                openMachineUi(tb);
            } else {
                const glm::ivec3 p = aim.block + aim.normal;
                if (itemInfo(held).placeable && !m_inventory.has(held)) {
                    // Assigned but out of stock: make the restock need audible.
                    audio().play("deny", kCraftVolume);
                } else if (itemInfo(held).placeable && m_inventory.has(held) &&
                           !cellOverlapsPlayer(p)) {
                    // A conduit carries items the way the player is facing --
                    // straight up/down when looking steeply. (Player policy,
                    // so decided here; WorldEdit just stores it.)
                    const glm::vec3 f = camera().front();
                    glm::ivec3 facing;
                    if (std::abs(f.y) > 0.7f) {
                        facing = {0, f.y > 0 ? 1 : -1, 0};
                    } else if (std::abs(f.x) > std::abs(f.z)) {
                        facing = {f.x > 0 ? 1 : -1, 0, 0};
                    } else {
                        facing = {0, 0, f.z > 0 ? 1 : -1};
                    }

                    // WorldEdit refuses world-side (cell taken, saplings need
                    // soil) as a silent no-op, matching the old guards.
                    const WorldEdit::PlaceResult r = WorldEdit::placeBlock(
                        *m_world, editRegistries(), p, itemInfo(held).placesBlock, facing);
                    if (r.placed) {
                        audio().playAt("place", glm::vec3(p) + glm::vec3(0.5f),
                                       kPlaceVolume, pitchJitter(p));
                        m_inventory.remove(held, 1);
                        if (r.powerChanged) solvePowerAndMarkDirty();
                        updateTitle();
                    }
                }
            }
        }

        // Wrench (default R): re-aims the targeted conduit, cycling six ways.
        if (input().wasKeyPressed(key(Action::WrenchRotate)) && m_inventory.has(ItemId::Wrench)) {
            WorldEdit::rotateBelt(*m_world, m_belts, tb);
        }
    }
}
