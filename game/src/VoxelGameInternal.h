#pragma once

// Shared internals of the VoxelGame implementation files (VoxelGame*.cpp):
// every gameplay tuning constant, plus the few helpers used across more than
// one of them. Helpers used by a single concern live in that file's anonymous
// namespace instead. Not part of the public game headers.

#include "game/Block.h"
#include "game/Collision.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"
#include "game/World.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Product version, injected by CMake from the root project() version.
#ifndef VOXEL_FACTORY_VERSION
#define VOXEL_FACTORY_VERSION "dev"
#endif

namespace vg {

    // Mouse look sensitivity and master volume live in Settings (Settings.h)
    // now — user-tunable, persisted in settings.cfg.

    // Sun direction, shared by the chunk and entity passes.
    inline const glm::vec3 kLightDir = glm::normalize(glm::vec3{-0.4f, -1.0f, -0.3f});

    // ---- Player physics: the feel knobs. Tune freely. ----
    inline constexpr float kWalkSpeed    = 4.5f;   // blocks per second
    inline constexpr float kSprintMult   = 1.6f;   // LCtrl multiplier
    inline constexpr float kAccel        = 40.0f;  // blocks/s^2 spin-up toward wanted speed
    inline constexpr float kDecel        = 30.0f;  // blocks/s^2 friction when no input
    inline constexpr float kGravity      = 28.8f;  // blocks per second^2
    inline constexpr float kJumpSpeed    = 8.5f;   // initial jump velocity (~1.3 block jump)
    inline constexpr float kTerminalVel  = 50.0f;  // max fall speed
    inline constexpr float kPlayerHalfW  = 0.30f;  // half width of the player's box
    inline constexpr float kPlayerHeight = 1.80f;
    inline constexpr float kEyeHeight    = 1.62f;  // camera above the feet
    inline constexpr int   kUnstickMaxLift = 3;    // blocks to pop up out of an embed before giving up
    inline constexpr float kVoidY        = -8.0f;  // fall below this: pack lost, respawn
    // -------------------------------------------------------

    // ---- Health & damage ----
    inline constexpr float kMaxHealth        = 10.0f; // hearts
    inline constexpr float kFallSafeSpeed    = 14.0f; // impact speed that hurts (~3.5 blocks)
    inline constexpr float kFallDamagePerVel = 0.5f;  // hearts per blocks/s beyond safe
    inline constexpr float kDraughtHeal      = 4.0f;  // hearts per Healing Draught

    // ---- Mining & tools (timed breaking + tiered tool gating) ----
    // blockHardness(id) is the by-hand break time; the matching tool at the
    // block's tier divides it by the tool's miningSpeed (see breakSeconds).
    // Gated blocks (toolTier > 0) yield nothing without that tool (yieldsDrop).
    // Early-grind gathering chances (hand tier):
    inline constexpr float kPebbleChance = 0.25f; // pebble per grass/dirt sifted
    inline constexpr float kStickChance  = 0.5f;  // stick per leaf broken

    // ---- Drops (physical ground items) ----
    // Pickup is a cylinder: within kPickupRadius horizontally AND within
    // kPickupVertical of the body mid-point (reaches items resting in the pit a
    // freshly-mined block leaves).
    inline constexpr float kPickupRadius        = 1.6f;  // horizontal auto-collect radius
    inline constexpr float kPickupVertical      = 1.8f;  // vertical half-band for pickup
    inline constexpr float kDropRenderDist      = 26.0f; // billboards cull beyond this
    inline constexpr float kDeathDropPickupDelay = 0.8f; // grace before re-grabbing a death scatter
    // Billboard size in PIXELS: kDropIconScale / distance, clamped. Drawn
    // deliberately larger than belt cargo (150/dist, 10-40 px) -- cargo is
    // scenery you watch flow past, a ground drop is something you're hunting
    // for and must read at a glance across a mined-out area.
    // Keep kDropIconScale near kDropIconMax * 8 or below: much above that and
    // the cap is reached beyond kDropRenderDist, so every visible drop pins to
    // the ceiling and the distance falloff stops reading at all.
    inline constexpr float kDropIconScale = 400.0f; // px * blocks; bigger = bigger
    inline constexpr float kDropIconMin   = 14.0f;  // px floor, far away
    inline constexpr float kDropIconMax   = 100.0f; // px cap, up close

    // ---- Melee (the sword swings through the aim raycast) ----
    inline constexpr float kSwordDamage    = 2.0f;   // per hit (creature hearts)
    inline constexpr float kSwordCooldown  = 0.35f;  // seconds between swings
    inline constexpr float kKnockback      = 7.0f;   // horizontal shove, blocks/s
    inline constexpr float kKnockUp        = 4.5f;   // vertical pop, blocks/s
    inline constexpr float kKnockDecay     = 0.8f;   // shove multiplier per tick
    inline constexpr float kCreatureHealth = 6.0f;   // test creature (3 hits)
    inline constexpr float kFlashDecay     = 5.0f;   // hurt tint fade, per second

    // ---- Combat gear & potions (the dormant alchemy gets its combat job) ----
    // Mana Vial: a ranged alchemy bolt cast along the aim ray (hitscan). Reaches
    // farther than the sword and has its own cooldown, so it's kite-and-poke.
    inline constexpr float kBoltDamage   = 2.5f;   // per bolt (creature hearts)
    inline constexpr float kBoltReach    = 18.0f;  // blocks the bolt travels
    inline constexpr float kCastCooldown = 0.5f;   // seconds between casts
    // Elixir of Vigor: a timed buff multiplying weapon (sword + bolt) damage.
    inline constexpr float kVigorSeconds    = 20.0f; // buff duration, seconds
    inline constexpr float kVigorDamageMult = 1.5f;  // weapon damage while buffed
    // Armor: equipped pieces sum a flat COMBAT damage reduction (fall damage is
    // deliberately unmitigated — falling is the hardcore-death pressure). Capped
    // so no loadout reaches invulnerability.
    inline constexpr float kArmorMaxReduction = 0.60f;

    // Save location (%APPDATA%\<org>\<app>\). kOrgName is a placeholder until
    // a studio name exists; migrateLegacySave() makes renaming it free.
    inline constexpr const char* kOrgName  = "BennyThompson";
    inline constexpr const char* kAppName  = "voxel-factory";
    inline constexpr const char* kSaveFile = "save.vxf";
    inline constexpr const char* kSettingsFile = "settings.cfg";
    // The main menu offers this many independent save slots (save_0..N-1.vxf).
    inline constexpr int kSaveSlots = 3;

    // Fresh games and pre-v12 saves seed the hotbar with the ten placeables
    // the old auto-built hotbar put on keys 1-0, preserving muscle memory
    // (and previewing the machine tech tree on a fresh island).
    inline constexpr std::array<ItemId, kHotbarSlots> kDefaultHotbar = {
        ItemId::Conduit,      ItemId::WireItem,     ItemId::GeneratorItem,
        ItemId::GrinderItem,  ItemId::CauldronItem, ItemId::InfuserItem,
        ItemId::AlembicItem,  ItemId::DistillerItem, ItemId::TransmuterItem,
        ItemId::MinerItem};

    inline constexpr float kReach = 8.0f;             // how far you can target blocks
    inline constexpr int   kWorldChunks = 6;          // NxN chunks => 96x96 area
    inline constexpr float kIslandRadius = 34.0f;     // base coastline radius (noise-wobbled)
    inline constexpr int   kSurfaceY = 14;            // base island surface height
    inline constexpr float kPlateauRadius = 10.0f;    // flattened spawn/demo area
    inline constexpr int   kPlateauY = kSurfaceY + 4; // plateau (and demo) surface height
    inline constexpr float kTickSeconds = 1.0f / 20.0f; // matches Application's tick rate
    inline constexpr int   kLoadPerAction = 8;        // recipe sets loaded per panel action
    inline constexpr int   kBeltStepTicks = 4;        // ticks between belt advances (~0.2s)

    inline constexpr float kSourceSpawnSeconds = 7.0f; // time between a source's node spawns
    inline constexpr int   kPatchRadius = 4;          // how far a source spreads its nodes
    inline constexpr int   kPatchCap = 5;             // max live nodes per source patch
    inline constexpr float kMineSeconds = 4.0f;       // miner: seconds per harvested node
    inline constexpr int   kMineRadius = 4;           // miner reach (matches patch radius)
    inline constexpr int   kMinerIdleRescanTicks = 5; // ticks between reach scans while idle

    // Forestry. Chopped leaves are the sapling supply; the pity counter
    // guarantees a drop before a whole canopy can come up empty-handed.
    inline constexpr float kSaplingDropChance = 0.25f; // sapling chance per chopped leaf
    inline constexpr int   kSaplingPityLeaves = 4;     // guaranteed drop after N dry leaves
    // ---- Weather & fuel knobs ----
    inline constexpr float kClearMinSeconds = 90.0f;   // clear-phase duration roll
    inline constexpr float kClearMaxSeconds = 240.0f;
    inline constexpr float kRainMinSeconds  = 40.0f;   // rain-phase duration roll
    inline constexpr float kRainMaxSeconds  = 100.0f;
    inline constexpr float kRainFadeSeconds = 4.0f;    // visual intensity ramp
    inline constexpr float kRainGrowthMult  = 3.0f;    // growth speed-up while raining
    inline constexpr int   kRainStreaks     = 220;     // streak count at full intensity
    inline constexpr float kRainRadius      = 14.0f;   // streak spawn radius (camera)
    inline constexpr float kRainFallSpeed   = 22.0f;   // blocks per second
    inline constexpr float kRainStreakLen   = 0.6f;
    inline constexpr float kRainSpan        = 24.0f;   // vertical wrap span
    inline constexpr int   kSkyTopY         = 64;      // sky-visibility scan ceiling
    inline constexpr float kRainDimMax      = 0.35f;   // max lit-color dimming
    inline constexpr int   kDemoFuelWood    = 8;       // wood preloaded in demo generators
    inline constexpr float kBucketFillSeconds = 8.0f;  // held-bucket fill time in rain
    // (Generator burn time / barrel fill cadence + cap are per-machine data
    // now: see kMachineTraits in Machine.h.)
    inline constexpr float kSourceMinRadius = 22.0f;   // sources scatter beyond this ring

    // ---- The Alchemy Circle (the crafting overhaul) ----
    // How much slower a circle runs without the Greater tier's power. This is
    // the whole price of the unpowered bootstrap path: a Lesser circle WILL
    // build your first Generator, it will just make you wait for it.
    inline constexpr float kLesserCircleSlowdown = 3.0f;
    // A pedestal holds one item TYPE, up to this many. Deep enough that a belt
    // can keep a pattern topped up, shallow enough to stay readable.
    inline constexpr int   kPedestalCap = 16;

    // ---- Entities: the test creature. Tune freely. ----
    inline constexpr const char* kCreatureModel = "assets/models/creature.bbmodel";
    inline constexpr float kCreatureScale        = 1.0f;   // model is authored in blocks
    inline constexpr float kCreatureHalfW        = 0.35f;  // collision box half width
    inline constexpr float kCreatureHeight       = 0.9f;
    inline constexpr float kCreatureWalkSpeed    = 1.6f;   // blocks per second
    inline constexpr float kCreatureWanderRadius = 6.0f;   // around its spawn point
    inline constexpr float kCreatureIdleMin      = 1.5f;   // seconds between wander legs
    inline constexpr float kCreatureIdleMax      = 5.0f;
    inline constexpr float kCreatureTurnRate     = 360.0f; // deg/s yaw ease
    inline constexpr int   kMaxEntityBones       = 32;     // must match uBones[] in entity.vert

    // ---- Boss & arena (the BossArena dimension). Tune freely. ----
    inline constexpr const char* kBossModel = "assets/models/boss.bbmodel";
    inline constexpr int   kArenaRadius        = 12;    // voidstone disc radius (blocks)
    inline constexpr int   kArenaY             = 20;    // arena ground height
    inline constexpr float kBossHealth         = 30.0f; // ~15 sword hits
    // Deliberately just UNDER kWalkSpeed: backing away still works, which is
    // what makes the lunge below the warden's real weapon rather than a
    // flourish. Raising this past 4.5 would make the lunge redundant.
    inline constexpr float kBossWalkSpeed      = 4.2f;  // blocks/s; a walk barely escapes
    inline constexpr float kBossAggroRadius    = 22.0f; // notices the player from here
    inline constexpr float kBossStrikeRange    = 2.1f;  // center distance for a hit
    inline constexpr float kBossDamage         = 1.5f;  // hearts per hit
    inline constexpr float kBossStrikeCooldown = 1.5f;  // seconds between hits
    inline constexpr float kBossKnockback      = 9.0f;  // player shove per hit, blocks/s
    // Vertical pop per hit, rolled per strike so no two hits feel alike.
    // Expressed as HEIGHT IN BLOCKS and converted to a launch velocity at the
    // strike site (v = sqrt(2*g*h)) -- the feel is "how high does it throw
    // me", not a velocity, and tuning in blocks keeps it that way.
    // Note the top of this range lands ABOVE kFallSafeSpeed: anything past
    // ~3.4 blocks hurts on the way down (and armor never mitigates fall
    // damage), so a high roll costs the strike plus up to ~1.5 more hearts.
    inline constexpr float kBossKnockUpMinH    = 1.0f;  // blocks of height
    inline constexpr float kBossKnockUpMaxH    = 5.0f;
    inline constexpr float kBossScale          = 2.0f;  // render + reach scale
    inline constexpr float kBossHalfW          = 0.85f; // collision half width
    inline constexpr float kBossHeight         = 2.3f;
    inline constexpr float kVictorySeconds     = 3.0f;  // linger before the ride home
    // The warden's kit. Kiting used to be free (it walks slower than you), so
    // the LUNGE is the answer to distance: a visible freeze, then a dash
    // faster than a sprint. The windup is what keeps it fair -- the tell is
    // the boss standing still, which needs no new art to read.
    inline constexpr float kBossLungeCooldown   = 5.0f;  // seconds between lunges
    inline constexpr float kBossLungeWindup     = 0.55f; // frozen telegraph
    inline constexpr float kBossLungeSpeed      = 12.0f; // > sprint (kWalkSpeed*kSprintMult)
    inline constexpr float kBossLungeDuration   = 0.45f; // ~5 blocks of travel
    inline constexpr float kBossLungeMinRange   = 3.0f;  // already on you: just swing
    inline constexpr float kBossLungeMaxRange   = 14.0f; // beyond this it walks instead
    inline constexpr float kBossLungeDamageMult = 1.6f;  // a connecting dash hurts more
    // Enrage: the last 40% is the dangerous part, not a formality.
    inline constexpr float kBossEnrageAt        = 0.40f; // hp fraction
    inline constexpr float kBossEnrageSpeedMult = 1.30f;
    inline constexpr float kBossEnrageRateMult  = 0.60f; // cooldowns shrink

    // THE TEMPEST (boss #2): faster than a walking player (sprint or die),
    // harder hits, more health. Its storm arena is a tighter ring.
    inline constexpr const char* kTempestModel = "assets/models/tempest.bbmodel";
    inline constexpr float kTempestHealth         = 24.0f; // ~12 sword hits
    inline constexpr float kTempestWalkSpeed      = 4.6f;  // > kWalkSpeed; < sprint
    inline constexpr float kTempestAggroRadius    = 26.0f;
    inline constexpr float kTempestStrikeRange    = 2.0f;
    inline constexpr float kTempestDamage         = 2.0f;  // hearts per hit
    inline constexpr float kTempestStrikeCooldown = 1.2f;
    inline constexpr float kTempestScale          = 1.6f;
    inline constexpr float kTempestHalfW          = 0.6f;
    inline constexpr float kTempestHeight         = 2.4f;
    inline constexpr int   kTempestArenaRadius    = 10;    // tighter than the warden's

    // The arena lives at its own origin (a different World; overlap with home
    // coordinates is fine). Player spawns south, the boss lurks north.
    inline glm::vec3 arenaSpawnFeet() { return {0.5f, static_cast<float>(kArenaY) + 1.0f, 8.5f}; }
    inline glm::vec3 bossSpawnFeet()  { return {0.5f, static_cast<float>(kArenaY) + 1.0f, -5.5f}; }

    // ---- Audio: mix levels + hum behavior (master volume is a Setting) ----
    inline constexpr float kMineVolume     = 0.9f;  // block broken (positional)
    inline constexpr float kPlaceVolume    = 0.8f;  // block placed (positional)
    inline constexpr float kUiVolume       = 0.5f;  // clicks / open / close
    inline constexpr float kCraftVolume    = 0.6f;  // craft success / deny
    inline constexpr float kHurtVolume     = 0.7f;  // player damage / heal
    inline constexpr float kHumVolume      = 0.55f; // energized-machine loop
    inline constexpr float kHumMaxDistance = 14.0f; // hum audible radius (blocks)
    inline constexpr int   kMaxHums        = 12;    // loop cap; nearest machines win
    inline constexpr float kRainVolume     = 0.5f;  // rain loop gain at intensity 1

    inline constexpr float kTreeGrowSeconds  = 45.0f;  // sapling -> tree (space permitting)
    inline constexpr float kLeafDecaySeconds = 0.6f;   // cadence of orphaned-leaf decay passes
    inline constexpr float kLeafDecayChance  = 0.5f;   // per orphaned leaf per pass (staggers)
    inline constexpr int   kLeafReach        = 2;      // leaves survive within this of a log

    // The tree shape as offsets from the sapling cell: a 3-log trunk, a 3x3
    // leaf ring around the top log, a full 3x3 layer, and a plus-shaped cap.
    // Single source of truth for world-gen, sapling growth, and space checks.
    struct TreeCell {
        glm::ivec3 offset;
        BlockId    block;
    };

    inline const std::vector<TreeCell>& treeCells() {
        static const std::vector<TreeCell> cells = [] {
            std::vector<TreeCell> c;
            for (int y = 0; y <= 2; ++y) c.push_back({{0, y, 0}, BlockId::Log});
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dz != 0) c.push_back({{dx, 2, dz}, BlockId::Leaves});
                    c.push_back({{dx, 3, dz}, BlockId::Leaves});
                }
            }
            const glm::ivec3 cap[5] = {{0, 4, 0}, {1, 4, 0}, {-1, 4, 0}, {0, 4, 1}, {0, 4, -1}};
            for (const glm::ivec3& o : cap) c.push_back({o, BlockId::Leaves});
            return c;
        }();
        return cells;
    }

    // Stamp a grown tree whose trunk base is at `base` (the sapling cell).
    inline void placeTree(World& world, const glm::ivec3& base) {
        for (const TreeCell& c : treeCells()) {
            world.setBlock(base.x + c.offset.x, base.y + c.offset.y,
                           base.z + c.offset.z, c.block);
        }
    }

    // (Machine input policy — machineAccepts / minerFilter — lives with the
    // machine simulation now: MachineSystem.h.)

    // Can this cell see the sky? (No roof between it and the world top.)
    // Shared by the rain collector, the held bucket, and the rain mesh. It
    // takes a FULL cube to keep rain out — rain falls past a tube or a slab.
    inline bool skyVisible(const World& w, int wx, int wy, int wz) {
        for (int y = wy + 1; y <= kSkyTopY; ++y) {
            if (isFullCube(w.getBlock(wx, y, wz))) return false;
        }
        return true;
    }

    // Does an AABB (feet at `feet`, half width halfW, height h) overlap any
    // block geometry? Shared by player and entity move-and-slide. The cell
    // walk lives in Collision now, so this tests a belt's actual slab rather
    // than the whole cell it sits in.
    inline bool boxCollides(const World& w, const glm::vec3& feet, float halfW,
                            float height) {
        return Collision::boxOverlapsWorld(
            w, {feet.x - halfW, feet.y, feet.z - halfW},
            {feet.x + halfW, feet.y + height, feet.z + halfW});
    }

    // Milliseconds between two SDL performance-counter readings.
    inline float msBetween(std::uint64_t t0, std::uint64_t t1) {
        return static_cast<float>(t1 - t0) * 1000.0f /
               static_cast<float>(SDL_GetPerformanceFrequency());
    }

    // Deterministic hash of a 2D lattice point and a seed.
    inline std::uint32_t hash2(int x, int z, std::uint32_t seed = 0) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                          static_cast<std::uint32_t>(z) * 668265263u + seed * 2654435761u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    // Where the player stands after spawning or falling off the island.
    inline glm::vec3 spawnFeet() {
        const float c = kWorldChunks * CHUNK_SIZE * 0.5f;
        return {c, static_cast<float>(kPlateauY) + 1.0f, c + 6.0f};
    }

} // namespace vg
