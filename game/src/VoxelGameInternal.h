#pragma once

// Shared internals of the VoxelGame implementation files (VoxelGame*.cpp):
// every gameplay tuning constant, plus the few helpers used across more than
// one of them. Helpers used by a single concern live in that file's anonymous
// namespace instead. Not part of the public game headers.

#include "game/Block.h"
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
    inline constexpr float kVoidY        = -8.0f;  // fall below this: pack lost, respawn
    // -------------------------------------------------------

    // ---- Health & damage ----
    inline constexpr float kMaxHealth        = 10.0f; // hearts
    inline constexpr float kFallSafeSpeed    = 14.0f; // impact speed that hurts (~3.5 blocks)
    inline constexpr float kFallDamagePerVel = 0.5f;  // hearts per blocks/s beyond safe
    inline constexpr float kDraughtHeal      = 4.0f;  // hearts per Healing Draught

    // ---- Melee (the sword swings through the aim raycast) ----
    inline constexpr float kSwordDamage    = 2.0f;   // per hit (creature hearts)
    inline constexpr float kSwordCooldown  = 0.35f;  // seconds between swings
    inline constexpr float kKnockback      = 7.0f;   // horizontal shove, blocks/s
    inline constexpr float kKnockUp        = 4.5f;   // vertical pop, blocks/s
    inline constexpr float kKnockDecay     = 0.8f;   // shove multiplier per tick
    inline constexpr float kCreatureHealth = 6.0f;   // test creature (3 hits)
    inline constexpr float kFlashDecay     = 5.0f;   // hurt tint fade, per second

    // Save location (%APPDATA%\<org>\<app>\). kOrgName is a placeholder until
    // a studio name exists; migrateLegacySave() makes renaming it free.
    inline constexpr const char* kOrgName  = "BennyThompson";
    inline constexpr const char* kAppName  = "voxel-factory";
    inline constexpr const char* kSaveFile = "save.vxf";
    inline constexpr const char* kSettingsFile = "settings.cfg";

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

    // Can this cell see the sky? (No solid block between it and the world
    // top.) Shared by the rain collector, the held bucket, and the rain mesh.
    inline bool skyVisible(const World& w, int wx, int wy, int wz) {
        for (int y = wy + 1; y <= kSkyTopY; ++y) {
            if (isSolid(w.getBlock(wx, y, wz))) return false;
        }
        return true;
    }

    // Does an AABB (feet at `feet`, half width halfW, height h) overlap any
    // solid block? Shared by player and entity move-and-slide.
    inline bool boxCollides(const World& w, const glm::vec3& feet, float halfW,
                            float height) {
        const int x0 = static_cast<int>(std::floor(feet.x - halfW));
        const int x1 = static_cast<int>(std::floor(feet.x + halfW));
        const int y0 = static_cast<int>(std::floor(feet.y));
        const int y1 = static_cast<int>(std::floor(feet.y + height));
        const int z0 = static_cast<int>(std::floor(feet.z - halfW));
        const int z1 = static_cast<int>(std::floor(feet.z + halfW));
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    if (isSolid(w.getBlock(x, y, z))) return true;
                }
            }
        }
        return false;
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
