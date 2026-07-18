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

    // The player's box vs. the world (shared helper, player-sized).
    bool boxCollides(const World& w, const glm::vec3& feet) {
        return vg::boxCollides(w, feet, kPlayerHalfW, kPlayerHeight);
    }

    // Small per-cell pitch variation (±10%) so repeated mining/placing at
    // different spots doesn't sound machine-gun identical.
    float pitchJitter(const glm::ivec3& p) {
        return 1.0f + (static_cast<int>(hash2(p.x * 31 + p.y, p.z, 517u) % 21u) - 10) * 0.01f;
    }

} // namespace

void VoxelGame::damagePlayer(float amount) {
    if (amount <= 0.0f) return;
    m_health = std::max(0.0f, m_health - amount);
    audio().play("hurt", kHurtVolume);
}

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

    // Weather visuals ease in and out; F4 is a dev key to summon/clear rain.
    m_weather.frameEase(dt);
    audio().setLoopGain(m_rainLoop, m_weather.intensity * kRainVolume);
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

    // Mouse look.
    cam.addLook(input().mouseRelX() * m_settings.sensitivity,
                -input().mouseRelY() * m_settings.sensitivity);

    // Walking physics: WASD on the ground plane, gravity, Space to jump.
    // There is no flight -- verticality is scaffolds, hills, and falling.
    glm::vec3 flatFront(cam.front().x, 0.0f, cam.front().z);
    if (glm::dot(flatFront, flatFront) > 1e-6f) flatFront = glm::normalize(flatFront);

    glm::vec3 wish(0.0f);
    if (input().isKeyDown(key(Action::MoveForward))) wish += flatFront;
    if (input().isKeyDown(key(Action::MoveBack))) wish -= flatFront;
    if (input().isKeyDown(key(Action::MoveRight))) wish += cam.right();
    if (input().isKeyDown(key(Action::MoveLeft))) wish -= cam.right();
    float targetSpeed = 0.0f;
    if (glm::dot(wish, wish) > 0.0f) {
        targetSpeed = kWalkSpeed;
        if (input().isKeyDown(key(Action::Sprint))) targetSpeed *= kSprintMult;
        wish = glm::normalize(wish);
    }

    // Momentum: accelerate toward the wanted velocity; friction to a stop
    // when there's no input.
    const glm::vec3 targetVel = wish * targetSpeed;
    const glm::vec3 delta = targetVel - m_velXZ;
    const float deltaLen = glm::length(delta);
    if (deltaLen > 0.0001f) {
        const float rate = (targetSpeed > 0.0f) ? kAccel : kDecel;
        m_velXZ += delta * (std::min(rate * dt, deltaLen) / deltaLen);
    }

    if (m_grounded && input().isKeyDown(key(Action::Jump))) {
        m_velY = kJumpSpeed;
    }
    m_velY = std::max(m_velY - kGravity * dt, -kTerminalVel);

    // Axis-separated move-and-slide against the voxel grid.
    glm::vec3 feet = cam.position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    glm::vec3 next = feet;
    next.x += m_velXZ.x * dt;
    if (!boxCollides(*m_world, next)) feet.x = next.x;
    else m_velXZ.x = 0.0f; // ran into a wall
    next = feet;
    next.z += m_velXZ.z * dt;
    if (!boxCollides(*m_world, next)) feet.z = next.z;
    else m_velXZ.z = 0.0f;

    m_grounded = false;
    next = feet;
    next.y += m_velY * dt;
    if (!boxCollides(*m_world, next)) {
        feet.y = next.y;
    } else if (m_velY <= 0.0f) {
        feet.y = std::floor(next.y) + 1.0f; // land: snap feet onto the block top
        // Hard landings hurt: damage scales with impact speed beyond the
        // safe threshold (~a 3-block drop).
        const float impact = -m_velY;
        if (impact > kFallSafeSpeed) {
            damagePlayer((impact - kFallSafeSpeed) * kFallDamagePerVel);
        }
        m_velY = 0.0f;
        m_grounded = true;
    } else {
        m_velY = 0.0f; // bumped our head
    }

    // Death — by damage or by falling off the island — costs the whole pack
    // and respawns on the plateau. One hardcore penalty everywhere. Hotbar
    // ASSIGNMENTS deliberately survive (they're references, not items): the
    // slots grey out at count 0 and re-enable as the pack is rebuilt.
    const bool fellOff = feet.y < kVoidY;
    if (fellOff || m_health <= 0.0f) {
        m_inventory = Inventory{};
        feet = spawnFeet();
        m_velY = 0.0f;
        m_velXZ = glm::vec3(0.0f);
        m_health = kMaxHealth;
        window().setTitle(fellOff
            ? "Voxel Factory  —  YOU FELL. YOUR PACK IS LOST."
            : "Voxel Factory  —  YOU DIED. YOUR PACK IS LOST.");
    }

    cam.position = feet + glm::vec3(0.0f, kEyeHeight, 0.0f);

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

    // Drink: RMB with the Healing Draught held restores health (no aim
    // needed); the sip consumes the click so nothing places or opens.
    bool drank = false;
    if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
        if (held == ItemId::HealingDraught && m_health < kMaxHealth &&
            m_inventory.has(held)) {
            m_inventory.remove(held, 1);
            m_health = std::min(kMaxHealth, m_health + kDraughtHeal);
            audio().play("heal", kHurtVolume);
            updateTitle();
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
        swordHit = m_creatures.tryMeleeAttack(*m_world, audio(), cam.position, cam.front());
    }

    // Aim and edit.
    const RaycastHit aim = raycastVoxel(*m_world, cam.position, cam.front(), kReach);
    m_hasTarget = aim.hit;
    m_targetBlock = aim.block;

    if (aim.hit) {
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
