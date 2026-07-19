// The fixed 20 Hz simulation: growth, weather, power glue, and the calls into
// MachineSystem — plus the machine/belt registries the edit paths share.
// Everything here mutates world/state and relies on the per-frame dirty sweep
// to update meshes.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/MachineSystem.h"
#include "game/PowerSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace vg;

// Holding a bucket under open sky while it rains slowly collects water.
// Rain (and so the bucket) is an Overworld thing; the arena has no weather.
void VoxelGame::updateBucketFill() {
    const ItemId held = heldItem();
    const glm::vec3 feet = camera().position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    const bool collecting = m_dimension == DimensionId::Overworld &&
        m_weather.raining && held == ItemId::Bucket &&
        m_inventory.has(ItemId::Bucket) &&
        skyVisible(overworld(),
                   static_cast<int>(std::floor(feet.x)),
                   static_cast<int>(std::floor(feet.y + kPlayerHeight)),
                   static_cast<int>(std::floor(feet.z)));
    if (!collecting) {
        m_bucketFill = 0.0f;
        return;
    }
    m_bucketFill += kTickSeconds;
    if (m_bucketFill >= kBucketFillSeconds) {
        m_bucketFill = 0.0f;
        m_inventory.add(ItemId::SpringWater, 1);
    }
}

// Recompute the power network -- only edits that add/remove a power block can
// change it -- and queue a remesh for every chunk whose energized glow flips.
void VoxelGame::solvePowerAndMarkDirty() {
    const std::uint64_t t0 = SDL_GetPerformanceCounter();
    PowerState next = PowerSystem::solve(overworld(), m_machines, &m_hungryGenerators);
    for (const glm::ivec3& c : next.cells()) {
        if (!m_power.energized(c.x, c.y, c.z)) overworld().markDirtyAt(c.x, c.y, c.z);
    }
    for (const glm::ivec3& c : m_power.cells()) {
        if (!next.energized(c.x, c.y, c.z)) overworld().markDirtyAt(c.x, c.y, c.z);
    }
    m_power = std::move(next);
    m_perf.lastSolveMs = msBetween(t0, SDL_GetPerformanceCounter());
    ++m_perf.solveCount;
    updateHums(); // the energized set is the hum set
}

namespace {
    // Slightly detuned per machine so a bank of them beats gently instead of
    // phase-summing into one loud tone.
    float humPitch(const glm::ivec3& p) {
        return 0.97f + (vg::hash2(p.x * 17 + p.y, p.z, 733u) % 7u) * 0.01f;
    }
} // namespace

// Keep one positional hum loop per audibly-running machine: energized, and
// for generators actually burning. Only called when the power state can have
// changed (every solve), never per frame/tick.
void VoxelGame::updateHums() {
    std::vector<glm::ivec3> wanted;
    for (const auto& [pos, m] : m_machines) {
        if (!m_power.energized(pos.x, pos.y, pos.z)) continue;
        if (machineTraits(m.type).kind == MachineKind::Generator && m.progress <= 0.0f) continue;
        wanted.push_back(pos);
    }
    if (static_cast<int>(wanted.size()) > kMaxHums) {
        const glm::vec3 ear = camera().position;
        std::partial_sort(wanted.begin(), wanted.begin() + kMaxHums, wanted.end(),
                          [&](const glm::ivec3& a, const glm::ivec3& b) {
                              const glm::vec3 da = glm::vec3(a) + glm::vec3(0.5f) - ear;
                              const glm::vec3 db = glm::vec3(b) + glm::vec3(0.5f) - ear;
                              return glm::dot(da, da) < glm::dot(db, db);
                          });
        wanted.resize(kMaxHums);
    }
    const std::unordered_set<glm::ivec3, IVec3Hash> want(wanted.begin(), wanted.end());

    for (auto it = m_humLoops.begin(); it != m_humLoops.end();) {
        if (want.count(it->first) == 0) {
            audio().destroyLoop(it->second);
            it = m_humLoops.erase(it);
        } else {
            ++it;
        }
    }
    for (const glm::ivec3& pos : wanted) {
        if (m_humLoops.count(pos) > 0) continue;
        const engine::AudioLoop h = audio().createLoop(
            "hum_loop", /*spatial=*/true, kHumVolume, kHumMaxDistance, humPitch(pos));
        if (h == 0) continue;
        audio().setLoopPosition(h, glm::vec3(pos) + glm::vec3(0.5f));
        // Hums are Overworld ambience: born paused while the player is away
        // (the arena overlaps home coordinates numerically).
        if (m_dimension != DimensionId::Overworld) audio().setLoopPaused(h, true);
        m_humLoops[pos] = h;
    }
}

void VoxelGame::registerMachine(const glm::ivec3& pos, BlockId type) {
    Machine m;
    m.type = type;
    m_machines[pos] = m;
}

void VoxelGame::registerBelt(const glm::ivec3& pos, const glm::ivec3& facing) {
    Belt b;
    b.facing = facing;
    m_belts[pos] = b;
}

void VoxelGame::updateSources() {
    for (auto& [pos, timer] : m_sources) {
        timer += kTickSeconds * (m_weather.raining ? kRainGrowthMult : 1.0f);
        if (timer < kSourceSpawnSeconds) continue;
        timer = 0.0f;

        const BlockId node = sourceSpawnsNode(overworld().getBlock(pos.x, pos.y, pos.z));
        if (node == BlockId::Air) continue; // source block was removed under us

        // Patch is capped: count this source's live nodes nearby.
        int liveNodes = 0;
        for (int dz = -kPatchRadius; dz <= kPatchRadius; ++dz) {
            for (int dx = -kPatchRadius; dx <= kPatchRadius; ++dx) {
                for (int dy = -3; dy <= 3; ++dy) {
                    if (overworld().getBlock(pos.x + dx, pos.y + dy, pos.z + dz) == node) {
                        ++liveNodes;
                    }
                }
            }
        }
        if (liveNodes >= kPatchCap) continue;

        // Try a few random nearby columns for grass with air above.
        bool placedNode = false;
        for (int attempt = 0; attempt < 8 && !placedNode; ++attempt) {
            const std::uint32_t h = hash2(pos.x * 31 + attempt, pos.z * 17, m_worldSeed + m_sourceRng++);
            const int dx = static_cast<int>(h % (2 * kPatchRadius + 1)) - kPatchRadius;
            const int dz = static_cast<int>((h >> 8) % (2 * kPatchRadius + 1)) - kPatchRadius;
            if (dx == 0 && dz == 0) continue;

            const int x = pos.x + dx;
            const int z = pos.z + dz;
            for (int y = pos.y + 2; y >= pos.y - 3; --y) {
                if (overworld().getBlock(x, y, z) == BlockId::Grass &&
                    overworld().getBlock(x, y + 1, z) == BlockId::Air) {
                    overworld().setBlock(x, y + 1, z, node);
                    placedNode = true;
                    break;
                }
            }
        }
    }
}

// Would a solid block in this cell intersect the player's box?
bool VoxelGame::cellOverlapsPlayer(const glm::ivec3& p) {
    const glm::vec3 feet = camera().position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    return static_cast<float>(p.x + 1) > feet.x - kPlayerHalfW &&
           static_cast<float>(p.x) < feet.x + kPlayerHalfW &&
           static_cast<float>(p.z + 1) > feet.z - kPlayerHalfW &&
           static_cast<float>(p.z) < feet.z + kPlayerHalfW &&
           static_cast<float>(p.y + 1) > feet.y &&
           static_cast<float>(p.y) < feet.y + kPlayerHeight;
}

void VoxelGame::updateSaplings() {
    std::vector<glm::ivec3> done;

    for (auto& [pos, timer] : m_saplings) {
        if (timer < kTreeGrowSeconds) {
            timer += kTickSeconds * (m_weather.raining ? kRainGrowthMult : 1.0f);
            continue;
        }
        if (overworld().getBlock(pos.x, pos.y, pos.z) != BlockId::Sapling) {
            done.push_back(pos); // the block went away; drop the stale timer
            continue;
        }

        // Grow only into open space -- and never onto the player, who must
        // not wake up entombed in a canopy.
        bool clear = true;
        for (const TreeCell& c : treeCells()) {
            const glm::ivec3 cell = pos + c.offset;
            if (cell != pos && overworld().getBlock(cell.x, cell.y, cell.z) != BlockId::Air) {
                clear = false;
                break;
            }
            if (m_dimension == DimensionId::Overworld && cellOverlapsPlayer(cell)) {
                clear = false;
                break;
            }
        }
        if (!clear) continue; // blocked: stay ripe and retry next tick

        placeTree(overworld(), pos);
        done.push_back(pos);
    }

    for (const glm::ivec3& p : done) m_saplings.erase(p);
}

// Every leaf that dies -- chopped by hand or decayed off a felled trunk --
// rolls the same sapling drop into the player's pack. The shared pity counter
// guarantees the supply across dry streaks either way.
void VoxelGame::rollLeafSapling(const glm::ivec3& p) {
    const std::uint32_t h = hash2(p.x * 31 + p.y, p.z * 17,
                                  m_worldSeed + m_sourceRng++);
    const bool lucky = (h % 100u) <
        static_cast<std::uint32_t>(kSaplingDropChance * 100.0f + 0.5f);
    if (lucky || ++m_leafPity >= kSaplingPityLeaves) {
        m_leafPity = 0;
        m_inventory.add(ItemId::SaplingItem, 1);
    }
}

void VoxelGame::updateLeafDecay() {
    m_leafDecayTimer += kTickSeconds;
    if (m_leafDecayTimer < kLeafDecaySeconds) return;
    m_leafDecayTimer = 0.0f;

    // Leaves with no log in reach wither, a random fraction per pass so a
    // felled canopy crumbles away rather than popping. Collect first: the
    // chunk map must not grow mid-iteration.
    std::vector<glm::ivec3> dying;
    for (const auto& [coord, chunk] : overworld().chunks()) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    if (chunk->get(x, y, z) != BlockId::Leaves) continue;
                    const glm::ivec3 p = coord * CHUNK_SIZE + glm::ivec3(x, y, z);

                    bool nearLog = false;
                    for (int dy = -kLeafReach; dy <= kLeafReach && !nearLog; ++dy) {
                        for (int dz = -kLeafReach; dz <= kLeafReach && !nearLog; ++dz) {
                            for (int dx = -kLeafReach; dx <= kLeafReach && !nearLog; ++dx) {
                                if (overworld().getBlock(p.x + dx, p.y + dy, p.z + dz) ==
                                    BlockId::Log) {
                                    nearLog = true;
                                }
                            }
                        }
                    }
                    if (nearLog) continue;

                    const std::uint32_t h =
                        hash2(p.x * 31 + p.y, p.z * 17, m_worldSeed + m_sourceRng++);
                    if (h % 100u <
                        static_cast<std::uint32_t>(kLeafDecayChance * 100.0f + 0.5f)) {
                        dying.push_back(p);
                    }
                }
            }
        }
    }

    for (const glm::ivec3& p : dying) {
        overworld().setBlock(p.x, p.y, p.z, BlockId::Air);
        rollLeafSapling(p); // a felled canopy still seeds the next forest
    }
}

void VoxelGame::onTick() {
    m_weather.tick(m_worldSeed);

    // Grow resource patches, pop ripe saplings, wither orphaned leaves. Any
    // change marks its chunk dirty; the per-frame sweep picks it up.
    updateSources();
    updateSaplings();
    updateLeafDecay();

    // Generators and collectors first: a burn flip re-solves the network so
    // the powered machines below see fresh power in this same tick.
    if (MachineSystem::tickSelfPowered(overworld(), m_machines, m_hungryGenerators,
                                       m_weather.raining)) {
        solvePowerAndMarkDirty();
    }
    updateBucketFill();

    // Powered machines process their input buffers into outputs over time.
    MachineSystem::tickPowered(overworld(), m_machines, m_power);

    // Advance conduits on a slower cadence so items visibly travel.
    if (++m_beltTimer >= kBeltStepTicks) {
        m_beltTimer = 0;
        MachineSystem::beltStep(m_belts, m_machines);
    }

    // Creatures step in the ACTIVE dimension (the arena's warden hunts; the
    // home wanderer freezes while the player is away). Boss strikes come
    // back as events — the first enemy damage in the game.
    const glm::vec3 playerFeet = camera().position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    const CreatureSystem::Events ev = m_creatures.update(*m_world, m_dimension, playerFeet);
    if (ev.damageToPlayer > 0.0f) {
        m_player.damage(ev.damageToPlayer, audio());
        m_player.shove(ev.playerKnock);
    }
}
