// The per-frame player: mouse look, walking physics (axis-separated
// move-and-slide, gravity, jump, the void-fall penalty), hotbar selection,
// and the aim/mine/place/wrench edit path. Menus intercept input up top.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/WorldEdit.h"
#include "game/DropSystem.h"
#include "game/Raycast.h"
#include "game/PowerSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using namespace vg;

namespace {

    // Small per-cell pitch variation (±10%) so repeated mining/placing at
    // different spots doesn't sound machine-gun identical.
    float pitchJitter(const glm::ivec3& p) {
        return 1.0f + (static_cast<int>(hash2(p.x * 31 + p.y, p.z, 517u) % 21u) - 10) * 0.01f;
    }

    // Timed breaking + tiered tool gating. `hardness` is the by-hand break
    // time; the right tool CLASS at the block's TIER divides it by the tool's
    // miningSpeed. A gated block (toolTier > 0) drops nothing without that tool.
    bool hasHarvestTool(ItemId held, BlockId block) {
        return blockTool(block) != ToolType::None &&
               itemTool(held) == blockTool(block) &&
               itemTier(held) >= blockToolTier(block);
    }
    float breakSeconds(BlockId block, ItemId held) {
        const float base = blockHardness(block);
        if (base <= 0.0f) return 0.0f; // effectively instant (unset hardness)
        return hasHarvestTool(held, block)
                   ? base / std::max(0.01f, itemMiningSpeed(held))
                   : base; // wrong/no tool: full by-hand time (and no drop if gated)
    }
    bool yieldsDrop(BlockId block, ItemId held) {
        return blockToolTier(block) == 0 || hasHarvestTool(held, block);
    }
    // What a gated block wants, in words, for the moment it breaks into
    // nothing. Tiers are numbers in the registry but a MATERIAL to the player,
    // and naming the material is the only form of this sentence that tells them
    // what to go and make.
    std::string toolWanted(BlockId block) {
        const char* cls = "TOOL";
        switch (blockTool(block)) {
            case ToolType::Pickaxe: cls = "PICKAXE"; break;
            case ToolType::Axe:     cls = "AXE";     break;
            case ToolType::Shovel:  cls = "SHOVEL";  break;
            case ToolType::None:    break;
        }
        const int tier = blockToolTier(block);
        const char* mat = tier <= kTierWood     ? "WOODEN"
                          : tier == kTierStone  ? "STONE"
                          : tier == kTierCopper ? "COPPER"
                                                : "IRON";
        return std::string(mat) + " " + cls;
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
    // Screenshot works over every screen, so it is read before any overlay
    // takes the input -- except an armed keybind capture, which owns the key.
    if (m_bindCapture < 0 && input().wasKeyPressed(key(Action::Screenshot))) {
        m_screenshotPending = true;
    }

    // A refusal's reason fades on REAL frame time, not the pause-aware clock
    // below: several of the sites that call deny() are inside panels, where the
    // sim is frozen and a message that never expired would hang there.
    m_denyTimer = std::max(0.0f, m_denyTimer - dt);

    // Entity animation clocks tick at render rate (menus keep animating, just
    // like onTick keeps simulating); a true pause freezes them.
    if (!paused()) {
        m_playtime += dt; // active-play seconds for the slot cards (excludes menus)
        m_creatures.frameAdvance(dt);
        // Animated block textures run off the same pause-aware clock, so a
        // paused cauldron stops bubbling along with the sim that fills it.
        m_animClock = std::fmod(m_animClock + dt, kAnimClockWrap);
        // Cargo slides across its cell between belt steps. Clamped rather than
        // wrapped: if the sim stalls, an item parks at its destination instead
        // of running past it.
        m_beltLerp = std::min(m_beltLerp + dt, kBeltStepTicks * kTickSeconds);
        m_attackCooldown = std::max(0.0f, m_attackCooldown - dt);
        m_castCooldown = std::max(0.0f, m_castCooldown - dt);
        m_vigorTimer = std::max(0.0f, m_vigorTimer - dt);
        m_placeCooldown = std::max(0.0f, m_placeCooldown - dt);
        m_rmbHeld = input().isMouseDown(SDL_BUTTON_RIGHT) ? m_rmbHeld + dt : 0.0f;

        // Victory linger: soak in the win, then ride home automatically.
        if (m_victoryTimer > 0.0f) {
            m_victoryTimer -= dt;
            if (m_victoryTimer <= 0.0f) returnHome();
        }
    }

    // Main menu shell (launch): owns all input over an unbuilt world until a
    // slot is chosen. Settings and the slot picker ride on top of it.
    if (m_shellOpen) {
        if (m_settingsOpen) updateSettingsUi();
        else if (m_slotPickerOpen) updateSlotPicker();
        else updateMainMenu();
        return;
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
    // Key + Iron Sword + Fusion Catalyst, plus two different source blocks so
    // fusion is testable straight away (place them adjacent, RMB the catalyst).
    // All assigned onto the hotbar so they are usable at once (the default
    // hotbar is full; the last slots are sacrificed).
    if (input().wasKeyPressed(SDL_SCANCODE_F6)) {
        // The kit wants more items than there are hotbar slots, so the
        // fallback index is CLAMPED: an out-of-range slot would otherwise be
        // an out-of-bounds write the moment the kit grew past kHotbarSlots.
        // Items that miss out still land in the pack, assignable via Tab.
        auto give = [&](ItemId id, int count, int fallbackSlot) {
            m_inventory.add(id, count);
            for (ItemId s : m_hotbar) {
                if (s == id) return; // already assigned
            }
            for (ItemId& s : m_hotbar) {
                if (s == ItemId::None) { s = id; return; }
            }
            if (fallbackSlot >= 0 && fallbackSlot < kHotbarSlots) {
                m_hotbar[static_cast<std::size_t>(fallbackSlot)] = id;
            }
        };
        // The fusion source blocks stay in the pack only -- the Alchemy Circle
        // parts below want those two slots more, and fusion is a two-click
        // test you can set up from the Tab overlay.
        m_inventory.add(ItemId::CrystalSourceItem, 4);
        m_inventory.add(ItemId::EssenceSourceItem, 4);
        give(ItemId::TeleportKey, 1, kHotbarSlots - 4);
        give(ItemId::StormKey, 1, kHotbarSlots - 3);
        give(ItemId::IronSword, 1, kHotbarSlots - 2);
        give(ItemId::FusionCatalyst, 1, kHotbarSlots - 1);
        // Combat pillar: usable gear on the hotbar (Forge to place, Mana Vials
        // to cast, Elixirs to drink) plus the mats + boss drops to forge armor
        // and a ready Aegis set to equip via Tab straight away.
        give(ItemId::ForgeItem, 1, kHotbarSlots - 7);
        give(ItemId::ManaVial, 16, kHotbarSlots - 8);
        give(ItemId::ElixirOfVigor, 8, kHotbarSlots - 9);
        m_inventory.add(ItemId::CopperPlate, 24);
        m_inventory.add(ItemId::MachineFrame, 6);
        m_inventory.add(ItemId::VoidCatalyst, 3);
        m_inventory.add(ItemId::StormCore, 3);
        m_inventory.add(ItemId::AegisHelm, 1);
        m_inventory.add(ItemId::AegisChest, 1);
        m_inventory.add(ItemId::AegisBoots, 1);
        // Parts tier: a Press plus stock at every stage, so the chain
        // (ingot -> rod -> gear, plate -> casing, plate + dust -> etched
        // plate, all three -> frame) is testable from any link without
        // grinding the whole ladder first.
        give(ItemId::PressItem, 1, kHotbarSlots - 10);
        // The Alchemy Circle: a core plus the full eight pedestals, so both
        // tiers (4 cardinals = Lesser, all 8 = Greater) are testable at once.
        give(ItemId::RuneCoreItem, 1, kHotbarSlots - 6);
        give(ItemId::PedestalItem, 8, kHotbarSlots - 5);
        m_inventory.add(ItemId::CopperIngot, 16);
        m_inventory.add(ItemId::CrystalDust, 12);
        m_inventory.add(ItemId::IronRod, 8);
        m_inventory.add(ItemId::Gear, 4);
        m_inventory.add(ItemId::MachineCasing, 2);
        m_inventory.add(ItemId::EtchedPlate, 2);
        // The smelting/sifting tier: both halves of each pair, so the manual
        // twin can be watched running the SAME recipe three times slower
        // right next to its powered counterpart.
        give(ItemId::BloomeryItem, 1, kHotbarSlots - 1);
        give(ItemId::FurnaceItem, 1, kHotbarSlots - 2);
        give(ItemId::SieveItem, 1, kHotbarSlots - 3);
        m_inventory.add(ItemId::SifterItem, 1);
        m_inventory.add(ItemId::GlassblowerItem, 1);
        m_inventory.add(ItemId::CompactorItem, 1);
        m_inventory.add(ItemId::MortarItem, 1);
        m_inventory.add(ItemId::HandPressItem, 1);
        // The four MODELLED machines, which the kit could not reach at all --
        // every other way to get one is a full Alchemy Circle grind or a walk
        // to the ruin. They are the only blocks in the game with sub-cube
        // geometry and moving parts, so they are the only ones where a
        // rendering change is visible, and a dev key that cannot put one in
        // front of you makes that change unverifiable by hand.
        m_inventory.add(ItemId::CauldronItem, 1);
        m_inventory.add(ItemId::InfuserItem, 1);
        m_inventory.add(ItemId::AlembicItem, 1);
        give(ItemId::MinerItem, 1, kHotbarSlots - 9);
        // A generator too, or none of the above can be switched on: the shape
        // animations and the energized glow are both gated on power.
        give(ItemId::GeneratorItem, 1, kHotbarSlots - 10);

        // Logistics: crates and a spool of conduit, so a sorting line (machine
        // -> crate -> filtered belts) can be laid without first chopping the
        // wood for it. This is the tier F6 most needs to reach, because a jam
        // is the one thing you cannot set up by hand in a fresh world.
        give(ItemId::StorageCrateItem, 4, kHotbarSlots - 4);
        give(ItemId::Conduit, 32, kHotbarSlots - 5);
        // Wire is only craftable on the Circle, so without this the kit could
        // build a factory it could not WIRE -- and, since wire became a shaped
        // block that grows arms toward its network, could not look at either.
        m_inventory.add(ItemId::WireItem, 32);
        // The Wrench gates BOTH conduit verbs (re-aim and set filter), so a kit
        // without one leaves half the logistics tier untestable.
        m_inventory.add(ItemId::Wrench, 1);
        // Iron: stock at every link of the sand -> nugget -> ingot -> plate
        // chain, plus the fuel to run it.
        m_inventory.add(ItemId::Charcoal, 32);
        m_inventory.add(ItemId::IronNugget, 32);
        m_inventory.add(ItemId::IronIngot, 16);
        m_inventory.add(ItemId::IronPlate, 24);
        // Raw stock so the Circle's blueprint rows actually light up -- the
        // panel only lists patterns the pack can lay.
        m_inventory.add(ItemId::Wood, 32);
        m_inventory.add(ItemId::Stone, 32);
        m_inventory.add(ItemId::Stick, 16);
        m_inventory.add(ItemId::Crystal, 12);
        m_inventory.add(ItemId::Glass, 12);
        m_inventory.add(ItemId::Essence, 12);
        m_inventory.add(ItemId::Sand, 32);
        m_inventory.add(ItemId::DirtItem, 32);
        // Farming: the hoe on the bar, since a field starts with tilling and
        // there is nothing else in the kit that can make Tilled Soil.
        give(ItemId::CopperHoe, 1, kHotbarSlots - 6);
        give(ItemId::HerbSeed, 32, kHotbarSlots - 7);
        give(ItemId::HarvesterItem, 2, kHotbarSlots - 8);
        m_inventory.add(ItemId::IrrigatorItem, 2);
        m_inventory.add(ItemId::SpringWater, 32);
        m_inventory.add(ItemId::Herb, 32);
        updateTitle();
        audio().play("craft", kCraftVolume);
    }

    // Weather visuals ease in and out; F4 is a dev key to summon/clear rain.
    // The rain loop is Overworld ambience — silent in the arena.
    m_weather.frameEase(dt);
    // Home rain follows the weather; the Tempest's arena storm never breaks.
    const float rainAmbience = m_dimension == DimensionId::Overworld
        ? m_weather.intensity : (m_arenaStorm ? 1.0f : 0.0f);
    audio().setLoopGain(m_rainLoop, rainAmbience * kRainVolume);
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
        m_invScroll = 0; // never open a panel already scrolled somewhere
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
    const glm::vec3 preMoveEye = cam.position; // death spot, before the respawn
    const PlayerController::MoveResult mv =
        m_player.move(dt, input(), cam, *m_world, m_settings, audio());
    if (mv.died) {
        // Worn armor rides the pack's fate: fold it back into the inventory so
        // the scatter/wipe below treats it uniformly (scattered on a normal
        // death, lost to the void/arena).
        for (ItemId& a : m_armor) {
            if (a != ItemId::None) { m_inventory.add(a, 1); a = ItemId::None; }
        }
        recomputeArmor();
        // Non-void death scatters the whole pack at the spot (recoverable);
        // the void, and any death in the transient arena, still fully wipes.
        if (!mv.fellOff && m_dimension == DimensionId::Overworld) {
            const glm::vec3 feet = preMoveEye - glm::vec3(0.0f, kEyeHeight, 0.0f);
            for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                const int c = m_inventory.count(id);
                if (c > 0) {
                    spawnDrop(feet + glm::vec3(0.0f, 0.4f, 0.0f), id, c,
                              kDeathDropPickupDelay);
                }
            }
        }
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
        } else if (held == ItemId::HealingDraught && m_inventory.has(held)) {
            // Holding a draught you cannot use yet. Silent before, which reads
            // as a broken item rather than as a full health bar.
            deny("ALREADY AT FULL HEALTH");
            drank = true; // the click is spent on the explanation
        } else if (held == ItemId::ElixirOfVigor && m_inventory.has(held)) {
            // A draught of vigor: refresh the timed weapon-damage buff.
            m_inventory.remove(held, 1);
            m_vigorTimer = kVigorSeconds;
            audio().play("heal", kHurtVolume);
            updateTitle();
            drank = true;
        } else if (held == ItemId::TeleportKey && m_inventory.has(held) &&
                   m_dimension == DimensionId::Overworld) {
            // The expensive tickets, consumed on use; return trips are free.
            m_inventory.remove(held, 1);
            enterArena(SpeciesId::VoidWarden);
            drank = true;
        } else if (held == ItemId::StormKey && m_inventory.has(held) &&
                   m_dimension == DimensionId::Overworld) {
            m_inventory.remove(held, 1);
            enterArena(SpeciesId::Tempest);
            drank = true;
        }
    }

    // The Elixir of Vigor buff scales both weapons while it lasts.
    const float vigorMult = m_vigorTimer > 0.0f ? kVigorDamageMult : 1.0f;

    // VICTORY: the unique drop lands in the pack, the species' progression flag
    // sticks (saved), and the linger timer starts the ride home. Shared by the
    // sword and the Mana Vial bolt.
    auto awardBossKill = [&](const CreatureSystem::MeleeResult& mr) {
        m_inventory.add(mr.drop, 1);
        if (mr.bossSpecies == SpeciesId::VoidWarden) m_bossDefeated = true;
        if (mr.bossSpecies == SpeciesId::Tempest) m_tempestDefeated = true;
        m_victoryTimer = kVictorySeconds;
        window().setTitle(std::string("Voxel Factory  —  ") + mr.bossName +
                          " FALLS. VICTORY!");
        audio().play("craft", kCraftVolume);
    };

    // A weapon: LMB swings along the aim ray, creatures first (needs no block
    // under the crosshair). A connected swing consumes the click; a miss
    // whooshes and falls through to mining. Which items are weapons, and how
    // hard they hit, is registry data (ItemInfo::weaponDamage).
    bool swordHit = false;
    if (input().wasMousePressed(SDL_BUTTON_LEFT) && m_attackCooldown <= 0.0f &&
        isWeapon(held) && m_inventory.has(held)) {
        m_attackCooldown = kSwordCooldown;
        audio().play("swing", kUiVolume);
        const CreatureSystem::MeleeResult mr = m_creatures.tryMeleeAttack(
            *m_world, audio(), cam.position, cam.front(), m_dimension,
            itemWeaponDamage(held) * vigorMult);
        swordHit = mr.hit;
        if (mr.bossDied) awardBossKill(mr);
    }

    // Mana Vial: LMB casts a ranged alchemy bolt (hitscan) and spends the vial.
    // A cast always consumes the click, so it never falls through to mining.
    if (input().wasMousePressed(SDL_BUTTON_LEFT) && m_castCooldown <= 0.0f &&
        held == ItemId::ManaVial && m_inventory.has(held)) {
        m_castCooldown = kCastCooldown;
        m_inventory.remove(held, 1);
        audio().play("swing", kUiVolume); // the whoosh doubles as a cast sound
        const CreatureSystem::MeleeResult mr = m_creatures.tryRangedAttack(
            *m_world, audio(), cam.position, cam.front(), m_dimension,
            kBoltDamage * vigorMult, kBoltReach);
        if (mr.bossDied) awardBossKill(mr);
        swordHit = true; // the click was spent on the cast: no mining, no arena deny
        updateTitle();
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
            deny("NO BUILDING IN THE ARENA - FIGHT OR GO HOME");
        }
    } else if (aim.hit) {
        const glm::ivec3 tb = aim.block;

        // (Mining is timed and runs off the HELD button; see the break block
        // below, after the RMB/wrench edits.)
        // RMB: on a machine, open its panel (Shift+RMB to place against it
        // instead); otherwise place the held item into the empty target cell.
        // Placing repeats while RMB is HELD, but only the place path: every
        // other RMB verb below (open a panel, drink, spend a key, fuse) stays
        // edge-triggered, because repeating those would be a disaster. Gated on
        // a cooldown AND on the cell changing, so holding the button down a
        // line of belts lays one per cell instead of racing the raycast.
        const bool placeRepeat =
            input().isMouseDown(SDL_BUTTON_RIGHT) && m_rmbHeld >= kPlaceRepeatDelay &&
            m_placeCooldown <= 0.0f && m_placedLastCell != aim.block + aim.normal;
        if (!drank && (input().wasMousePressed(SDL_BUTTON_RIGHT) || placeRepeat)) {
            const bool aimedMachine = m_machines.find(tb) != m_machines.end();
            // Only the final `else` (placing) may run on a repeat -- so both
            // one-shot verbs below re-test the EDGE. A repeat aimed at a
            // machine therefore places against it rather than re-opening its
            // panel every frame, which is what you want mid-line anyway.
            const bool pressed = input().wasMousePressed(SDL_BUTTON_RIGHT);
            if (pressed && held == ItemId::FusionCatalyst && m_inventory.has(held)) {
                // Fuse the aimed source with a different adjacent source; the
                // catalyst is spent only on a successful pairing.
                if (WorldEdit::fuseSources(*m_world, editRegistries(), tb)) {
                    audio().playAt("craft", glm::vec3(tb) + glm::vec3(0.5f),
                                   kCraftVolume, pitchJitter(tb));
                    m_inventory.remove(held, 1);
                    updateTitle();
                } else {
                    deny("FUSION NEEDS TWO DIFFERENT SOURCES SIDE BY SIDE");
                }
            } else if (pressed && held == ItemId::CopperHoe) {
                // Till the aimed cell into a bed a crop will take. A tool, so
                // nothing is spent and nothing wears out -- the cost of a field
                // is the walking, not the hoe.
                if (WorldEdit::tillSoil(*m_world, tb)) {
                    audio().playAt("place", glm::vec3(tb) + glm::vec3(0.5f),
                                   kPlaceVolume, pitchJitter(tb));
                } else if (isSolid(m_world->getBlock(tb.x, tb.y + 1, tb.z))) {
                    // The two refusals tillSoil folds into one `false` are
                    // different mistakes, so they get different answers.
                    deny("SOMETHING IS SITTING ON THIS GROUND");
                } else {
                    deny("THE HOE ONLY WORKS ON GRASS OR DIRT");
                }
            } else if (pressed && held == ItemId::Compost && m_inventory.has(held)) {
                // One rung above the hoe: worked ground fed compost grows
                // faster. Unlike the hoe and the catalyst this is a plain
                // MATERIAL, so it is spent -- and only on a true return, or a
                // misclick at a wall would eat it.
                if (WorldEdit::enrichSoil(*m_world, tb)) {
                    audio().playAt("place", glm::vec3(tb) + glm::vec3(0.5f),
                                   kPlaceVolume, pitchJitter(tb));
                    m_inventory.remove(held, 1);
                    updateTitle();
                } else if (m_world->getBlock(tb.x, tb.y, tb.z) == BlockId::RichSoil) {
                    // Re-enriching is a no-op in WorldEdit, but silence here
                    // would read as compost vanishing into nothing.
                    deny("THIS SOIL IS ALREADY RICH");
                } else if (isSolid(m_world->getBlock(tb.x, tb.y + 1, tb.z))) {
                    deny("SOMETHING IS SITTING ON THIS GROUND");
                } else {
                    deny("COMPOST GOES ON TILLED SOIL - USE THE HOE FIRST");
                }
            } else if (pressed && aimedMachine && !input().isKeyDown(SDL_SCANCODE_LSHIFT)) {
                openMachineUi(tb);
            } else {
                const glm::ivec3 p = aim.block + aim.normal;
                if (itemInfo(held).placeable && !m_inventory.has(held)) {
                    // Assigned but out of stock: make the restock need audible.
                    // The slot stays assigned on purpose (it greys out), so the
                    // reason has to distinguish "none left" from "not a block".
                    deny(std::string("OUT OF ") + itemName(held));
                } else if (itemInfo(held).placeable && m_inventory.has(held) &&
                           cellOverlapsPlayer(p)) {
                    // Was silent before. Nothing looks more broken than a click
                    // that does nothing while you are standing in the cell.
                    deny("YOU ARE STANDING THERE");
                } else if (pressed && held == ItemId::Bucket) {
                    // Silent before, and the bucket is the one tool whose verb
                    // is not a click at all -- so the click is exactly when to
                    // say so.
                    deny("HOLD THE BUCKET OUT IN THE RAIN TO FILL IT");
                } else if (pressed && held == ItemId::Wrench) {
                    deny(std::string("THE WRENCH TURNS BELTS - PRESS ") +
                         SDL_GetScancodeName(key(Action::WrenchRotate)));
                } else if (itemInfo(held).placeable && m_inventory.has(held) &&
                           !cellOverlapsPlayer(p)) {
                    // A conduit carries items the way the player is facing --
                    // straight up/down only when looking STEEPLY. (Player
                    // policy, so decided here; WorldEdit just stores it.)
                    //
                    // kVerticalLook used to be 0.7, which is a 44-degree
                    // glance -- shallower than the angle you naturally hold to
                    // put a block at your own feet (about 55-60). So laying a
                    // line along the ground silently gave every segment a
                    // DOWNWARD facing, and a line of conduits that all point
                    // into the dirt moves nothing. The bug was always there;
                    // it was invisible while a conduit was a cube, because a
                    // cube abuts its neighbour whichever way it faces. A tube
                    // draws an arm only where something connects, so the same
                    // mis-facing now reads as a row of disconnected stubs --
                    // the art telling the truth about a wrong the arrow hid.
                    const glm::vec3 f = camera().front();
                    glm::ivec3 facing;
                    if (std::abs(f.y) > kVerticalLook) {
                        facing = {0, f.y > 0 ? 1 : -1, 0};
                    } else if (std::abs(f.x) > std::abs(f.z)) {
                        facing = {f.x > 0 ? 1 : -1, 0, 0};
                    } else {
                        facing = {0, 0, f.z > 0 ? 1 : -1};
                    }
                    // ...unless you clicked against a MACHINE, in which case
                    // aim away from it. beltStep only pulls from the machine
                    // directly BEHIND a belt, so a belt built onto a machine
                    // face and pointing any other way is silently useless --
                    // by far the most common mis-facing, and the camera guess
                    // gets it wrong precisely when you are standing at the
                    // machine looking at it.
                    if (itemInfo(held).placesBlock == BlockId::Belt &&
                        m_machines.find(aim.block) != m_machines.end()) {
                        facing = aim.normal;
                    }
                    // ...and a conduit added to the END of a run continues it,
                    // rather than asking the camera again. Only when the new
                    // cell lies ON that conduit's axis, so clicking a run's
                    // SIDE still branches the way you are looking -- inheriting
                    // there would make a branch impossible to aim.
                    if (itemInfo(held).placesBlock == BlockId::Belt) {
                        const auto ab = m_belts.find(aim.block);
                        if (ab != m_belts.end() &&
                            glm::abs(aim.normal) == glm::abs(ab->second.facing)) {
                            facing = ab->second.facing;
                        }
                    }

                    // WorldEdit refuses world-side (cell taken, plants need the
                    // right ground) as a no-op. It reports only `placed`, but
                    // both refusals are cheap to re-derive here, and a plant
                    // that will not go down is the single most confusing one --
                    // there is nothing on screen to tell you tilled ground is a
                    // different thing from dirt.
                    const BlockId want = itemInfo(held).placesBlock;
                    const WorldEdit::PlaceResult r = WorldEdit::placeBlock(
                        *m_world, editRegistries(), p, want, facing);
                    if (!r.placed) {
                        const SoilKind needs = blockInfo(want).needsSoil;
                        if (isSolid(m_world->getBlock(p.x, p.y, p.z))) {
                            deny("THAT CELL IS ALREADY FULL");
                        } else if (needs == SoilKind::Tilled) {
                            deny("PLANT THIS ON TILLED SOIL - USE THE HOE");
                        } else if (needs == SoilKind::Soil) {
                            deny("THIS ONLY TAKES ROOT ON GRASS OR DIRT");
                        }
                    }
                    if (r.placed) {
                        audio().playAt("place", glm::vec3(p) + glm::vec3(0.5f),
                                       kPlaceVolume, pitchJitter(p));
                        m_inventory.remove(held, 1);
                        if (r.powerChanged) solvePowerAndMarkDirty();
                        updateTitle();
                        m_placedLastCell = p;
                        m_placeCooldown = kPlaceRepeatSeconds;
                    }
                }
            }
        }

        // Wrench (default R): re-aims the targeted conduit, cycling six ways.
        if (input().wasKeyPressed(key(Action::WrenchRotate)) && m_inventory.has(ItemId::Wrench)) {
            WorldEdit::rotateBelt(*m_world, m_belts, tb,
                                  input().isKeyDown(SDL_SCANCODE_LSHIFT));
        }

        // Middle-click picks the aimed block onto the hotbar, if you own one.
        // Costs nothing, saves a Tab round trip every time you extend a line
        // with a block you are already standing next to.
        if (input().wasMousePressed(SDL_BUTTON_MIDDLE)) {
            const ItemId want = blockDrop(m_world->getBlock(tb.x, tb.y, tb.z)).id;
            if (want != ItemId::None && itemInfo(want).placeable && m_inventory.has(want)) {
                // Already on the hotbar: just select it. Otherwise take the
                // current slot, which is the whole point -- you aimed at the
                // thing you want in your hand.
                int slot = -1;
                for (int i = 0; i < kHotbarSlots; ++i) {
                    if (m_hotbar[static_cast<std::size_t>(i)] == want) { slot = i; break; }
                }
                if (slot < 0) {
                    slot = m_selectedSlot;
                    m_hotbar[static_cast<std::size_t>(slot)] = want;
                }
                m_selectedSlot = slot;
                audio().play("click", kCraftVolume);
                updateTitle();
            } else {
                deny(want == ItemId::None || !itemInfo(want).placeable
                         ? "NOTHING TO PICK UP HERE"
                         : std::string("YOU DO NOT OWN A ") + itemName(want));
            }
        }

        // Belt filter (default F): the aimed conduit carries only the item on
        // the hotbar; pressing it again with that same item clears the filter.
        // A binding of its own rather than a modifier on the wrench, because
        // both the item and the belt already say what they are -- the player
        // should not also have to hold a mode.
        if (input().wasKeyPressed(key(Action::BeltFilter)) &&
            m_inventory.has(ItemId::Wrench)) {
            const auto bit = m_belts.find(tb);
            if (bit == m_belts.end()) {
                deny("FILTERS GO ON A CONDUIT");
            } else {
                // The selected item is a REFERENCE here, never consumed, so an
                // out-of-stock hotbar assignment still names a filter -- which
                // is the normal case when you are laying out a line before the
                // factory has made any of what will run down it.
                const ItemId want = (held == ItemId::Wrench) ? ItemId::None : held;
                bit->second.filter = (bit->second.filter == want) ? ItemId::None : want;
                audio().playAt("click", glm::vec3(tb) + glm::vec3(0.5f), kCraftVolume);
                m_world->markDirtyAround(tb);
            }
        }
    }

    // Timed breaking: holding LMB while aiming at a breakable Overworld cell
    // accrues progress against its hardness; the matching tool is faster (and,
    // for a gated block, the only way to keep the drop). A pure weapon (the
    // sword) swings on press instead — it never mines. When the aim leaves the
    // cell or the button releases, progress resets.
    const bool canMine = aim.hit && m_dimension == DimensionId::Overworld &&
                         !swordHit && !isWeapon(held) &&
                         held != ItemId::ManaVial &&
                         input().isMouseDown(SDL_BUTTON_LEFT);
    if (canMine) {
        const glm::ivec3 tb = aim.block;
        const BlockId bid = m_world->getBlock(tb.x, tb.y, tb.z);
        if (!m_breaking || m_breakTarget != tb) {
            m_breaking = true;
            m_breakTarget = tb;
            m_breakProgress = 0.0f;
        }
        m_breakNeeded = breakSeconds(bid, held);
        m_breakProgress += dt;
        if (m_breakProgress >= m_breakNeeded) {
            const bool keepDrop = yieldsDrop(bid, held);
            const WorldEdit::BreakResult r =
                WorldEdit::breakBlock(*m_world, editRegistries(), tb);
            const glm::vec3 dropPos = glm::vec3(tb) + glm::vec3(0.5f);
            if (keepDrop && r.drop.id != ItemId::None) {
                spawnDrop(dropPos, r.drop.id, r.drop.count);
            }
            // Machine buffers / belt cargo handed back are the player's own,
            // gated or not — they always drop.
            for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                const int c = r.returned.count(id);
                if (c > 0) spawnDrop(dropPos, id, c);
            }
            // The three things bare hands get out of the island, BESIDES each
            // block's own drop: turf pulls apart into fiber, topsoil turns up a
            // pebble, and a dying leaf sheds a stick and sometimes a sapling.
            //
            // Grass and Dirt yield theirs every time. They shared one 25% roll
            // until Aug 2026, which made them the same resource — no reason to
            // dig one over the other — and put a coin flip on the first thing a
            // fresh game asks you to collect. Now turf is where binding comes
            // from and topsoil is where stone starts, which is two verbs.
            //
            // None of this can be a BlockDrop row: Grass and Dirt already have
            // one (GrassItem / DirtItem) and BlockDrop is a single stack. That
            // is exactly why content::validate()'s closures have to name these
            // three items by hand — they are code, not data.
            if (keepDrop && bid == BlockId::Grass) {
                spawnDrop(dropPos, ItemId::PlantFiber, 1);
            }
            if (keepDrop && bid == BlockId::Dirt) {
                spawnDrop(dropPos, ItemId::Pebble, 1);
            }
            if (r.brokeLeaves) rollLeafDrops(tb, /*chopped=*/true);
            audio().playAt("mine", dropPos, kMineVolume, pitchJitter(tb));
            // The block is gone and you got nothing for it. This was entirely
            // silent, and it is the rule new players lose the most time to --
            // the pickaxe tier is invisible, so a stone that yields no stone
            // reads as the game being broken rather than as a missing tool.
            // Said AFTER the break, when the loss is what needs explaining.
            if (!keepDrop && r.drop.id != ItemId::None) {
                deny(std::string("NO DROP - ") + itemName(r.drop.id) + " NEEDS A " +
                     toolWanted(bid));
            }
            if (r.powerChanged) solvePowerAndMarkDirty();
            updateTitle();
            m_breaking = false;
            m_breakProgress = 0.0f;
        }
    } else {
        m_breaking = false;
        m_breakProgress = 0.0f;
    }
}

// Spawn a physical item into the active dimension (mining yields, handed-back
// machine buffers, the death-scattered pack). DropSystem merges it into a
// nearby like drop so repeated mining doesn't flood the world with entities.
void VoxelGame::spawnDrop(const glm::vec3& pos, ItemId id, int count,
                          float pickupDelay) {
    DropSystem::spawn(m_drops, pos, id, count, m_dimension, pickupDelay);
}
