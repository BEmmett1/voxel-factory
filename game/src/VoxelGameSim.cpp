// The fixed 20 Hz simulation: machines, belts, power, growth, weather — plus
// the machine/belt registries the edit paths share. Everything here mutates
// world/state and relies on the per-frame dirty sweep to update meshes.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/PowerSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace vg;

// Generators burn fuel; a new Wood is lit only when the network wants power
// (the hungry set from the last solve), but a lit one burns out fully.
// Machine::progress holds the burn seconds LEFT. When any generator's burn
// state flips, the power network is re-solved once.
void VoxelGame::updateGeneratorsAndBarrels() {
    bool powerChanged = false;
    for (auto& [pos, m] : m_machines) {
        if (m.type == BlockId::Generator) {
            const bool wasBurning = m.progress > 0.0f;
            if (m.progress > 0.0f) {
                m.progress = std::max(0.0f, m.progress - kTickSeconds);
            }
            if (m.progress <= 0.0f && m_hungryGenerators.count(pos) > 0 &&
                m.input.count(ItemId::Wood) > 0) {
                m.input.remove(ItemId::Wood, 1);
                m.progress = kWoodBurnSeconds;
            }
            m.crafting = m.progress > 0.0f;
            m.craftTime = kWoodBurnSeconds;
            if ((m.progress > 0.0f) != wasBurning) powerChanged = true;
        } else if (m.type == BlockId::RainBarrel) {
            // Barrels need no power -- just rain and open sky above.
            const bool filling = m_weatherRaining &&
                m.output.count(ItemId::SpringWater) < kBarrelCap &&
                skyVisible(pos.x, pos.y, pos.z);
            m.crafting = filling;
            m.craftTime = kBarrelFillSeconds;
            if (filling) {
                m.progress += kTickSeconds;
                if (m.progress >= kBarrelFillSeconds) {
                    m.progress = 0.0f;
                    m.output.add(ItemId::SpringWater, 1);
                }
            }
        }
    }
    if (powerChanged) {
        solvePowerAndMarkDirty();
    }
}

// Holding a bucket under open sky while it rains slowly collects water.
void VoxelGame::updateBucketFill() {
    const ItemId held = m_hotbar.empty() ? ItemId::None : m_hotbar[m_selectedSlot];
    const glm::vec3 feet = camera().position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    const bool collecting = m_weatherRaining && held == ItemId::Bucket &&
        m_inventory.has(ItemId::Bucket) &&
        skyVisible(static_cast<int>(std::floor(feet.x)),
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
    PowerState next = PowerSystem::solve(*m_world, m_machines, &m_hungryGenerators);
    for (const glm::ivec3& c : next.cells()) {
        if (!m_power.energized(c.x, c.y, c.z)) m_world->markDirtyAt(c.x, c.y, c.z);
    }
    for (const glm::ivec3& c : m_power.cells()) {
        if (!next.energized(c.x, c.y, c.z)) m_world->markDirtyAt(c.x, c.y, c.z);
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
        if (m.type == BlockId::Generator && m.progress <= 0.0f) continue;
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
        m_humLoops[pos] = h;
    }
}

void VoxelGame::registerMachine(const glm::ivec3& pos, BlockId type) {
    Machine m;
    m.type = type;
    m_machines[pos] = m;
}

void VoxelGame::unregisterMachine(const glm::ivec3& pos) {
    const auto it = m_machines.find(pos);
    if (it == m_machines.end()) return;
    // Return any buffered items to the player so nothing is lost.
    for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemId id = static_cast<ItemId>(i);
        m_inventory.add(id, it->second.input.count(id));
        m_inventory.add(id, it->second.output.count(id));
    }
    m_machines.erase(it);
}

void VoxelGame::registerBelt(const glm::ivec3& pos, const glm::ivec3& facing) {
    Belt b;
    b.facing = facing;
    m_belts[pos] = b;
}

void VoxelGame::unregisterBelt(const glm::ivec3& pos) {
    const auto it = m_belts.find(pos);
    if (it == m_belts.end()) return;
    if (it->second.item != ItemId::None) m_inventory.add(it->second.item, 1);
    m_belts.erase(it);
}

void VoxelGame::beltStep() {
    // 1. Belts deliver their item into a machine directly ahead (if it accepts).
    for (auto& [pos, b] : m_belts) {
        if (b.item == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto mit = m_machines.find(front);
        if (mit != m_machines.end() && machineAccepts(mit->second, b.item)) {
            mit->second.input.add(b.item, 1);
            b.item = ItemId::None;
        }
    }

    // 2. Hop items belt -> belt. Use a snapshot of pre-step contents so an item
    //    advances at most one belt, and claim targets so two items never merge.
    std::unordered_map<glm::ivec3, ItemId, IVec3Hash> before;
    before.reserve(m_belts.size());
    for (const auto& [pos, b] : m_belts) before[pos] = b.item;

    std::unordered_set<glm::ivec3, IVec3Hash> claimed;
    for (auto& [pos, b] : m_belts) {
        const ItemId carried = before[pos];
        if (carried == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto tb = m_belts.find(front);
        if (tb == m_belts.end()) continue;       // ahead is not a belt
        if (before[front] != ItemId::None) continue; // target was occupied
        if (claimed.count(front)) continue;       // already filled this step
        tb->second.item = carried;
        b.item = ItemId::None;
        claimed.insert(front);
    }

    // 3. Empty belts pull one item from a machine's output directly behind them.
    for (auto& [pos, b] : m_belts) {
        if (b.item != ItemId::None) continue;
        const glm::ivec3 back = pos - b.facing;
        const auto mit = m_machines.find(back);
        if (mit == m_machines.end()) continue;
        Inventory& out = mit->second.output;
        for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            if (out.count(id) > 0) {
                out.remove(id, 1);
                b.item = id;
                break;
            }
        }
    }
}

// Rain comes and goes on seeded random phases. Gameplay gates on the boolean;
// visuals ease through m_rainIntensity (updated per frame in onUpdate).
void VoxelGame::updateWeather() {
    m_weatherTimer -= kTickSeconds;
    if (m_weatherTimer > 0.0f) return;
    m_weatherRaining = !m_weatherRaining;
    const float lo = m_weatherRaining ? kRainMinSeconds : kClearMinSeconds;
    const float hi = m_weatherRaining ? kRainMaxSeconds : kClearMaxSeconds;
    const std::uint32_t h = hash2(311, 977, m_worldSeed + m_sourceRng++);
    m_weatherTimer = lo + (hi - lo) * static_cast<float>(h % 1024u) / 1023.0f;
}

// Can this cell see the sky? (No solid block between it and the world top.)
bool VoxelGame::skyVisible(int wx, int wy, int wz) const {
    for (int y = wy + 1; y <= kSkyTopY; ++y) {
        if (isSolid(m_world->getBlock(wx, y, wz))) return false;
    }
    return true;
}

void VoxelGame::updateSources() {
    for (auto& [pos, timer] : m_sources) {
        timer += kTickSeconds * (m_weatherRaining ? kRainGrowthMult : 1.0f);
        if (timer < kSourceSpawnSeconds) continue;
        timer = 0.0f;

        const BlockId node = sourceSpawnsNode(m_world->getBlock(pos.x, pos.y, pos.z));
        if (node == BlockId::Air) continue; // source block was removed under us

        // Patch is capped: count this source's live nodes nearby.
        int liveNodes = 0;
        for (int dz = -kPatchRadius; dz <= kPatchRadius; ++dz) {
            for (int dx = -kPatchRadius; dx <= kPatchRadius; ++dx) {
                for (int dy = -3; dy <= 3; ++dy) {
                    if (m_world->getBlock(pos.x + dx, pos.y + dy, pos.z + dz) == node) {
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
                if (m_world->getBlock(x, y, z) == BlockId::Grass &&
                    m_world->getBlock(x, y + 1, z) == BlockId::Air) {
                    m_world->setBlock(x, y + 1, z, node);
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
            timer += kTickSeconds * (m_weatherRaining ? kRainGrowthMult : 1.0f);
            continue;
        }
        if (m_world->getBlock(pos.x, pos.y, pos.z) != BlockId::Sapling) {
            done.push_back(pos); // the block went away; drop the stale timer
            continue;
        }

        // Grow only into open space -- and never onto the player, who must
        // not wake up entombed in a canopy.
        bool clear = true;
        for (const TreeCell& c : treeCells()) {
            const glm::ivec3 cell = pos + c.offset;
            if (cell != pos && m_world->getBlock(cell.x, cell.y, cell.z) != BlockId::Air) {
                clear = false;
                break;
            }
            if (cellOverlapsPlayer(cell)) {
                clear = false;
                break;
            }
        }
        if (!clear) continue; // blocked: stay ripe and retry next tick

        placeTree(*m_world, pos);
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
    for (const auto& [coord, chunk] : m_world->chunks()) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    if (chunk->get(x, y, z) != BlockId::Leaves) continue;
                    const glm::ivec3 p = coord * CHUNK_SIZE + glm::ivec3(x, y, z);

                    bool nearLog = false;
                    for (int dy = -kLeafReach; dy <= kLeafReach && !nearLog; ++dy) {
                        for (int dz = -kLeafReach; dz <= kLeafReach && !nearLog; ++dz) {
                            for (int dx = -kLeafReach; dx <= kLeafReach && !nearLog; ++dx) {
                                if (m_world->getBlock(p.x + dx, p.y + dy, p.z + dz) ==
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
        m_world->setBlock(p.x, p.y, p.z, BlockId::Air);
        rollLeafSapling(p); // a felled canopy still seeds the next forest
    }
}

void VoxelGame::onTick() {
    updateWeather();

    // Grow resource patches, pop ripe saplings, wither orphaned leaves. Any
    // change marks its chunk dirty; the per-frame sweep picks it up.
    updateSources();
    updateSaplings();
    updateLeafDecay();
    updateGeneratorsAndBarrels();
    updateBucketFill();

    // Powered machines process their input buffer into outputs over time.
    for (auto& [pos, m] : m_machines) {
        // Generators and barrels run in the pre-pass above (their state does
        // not gate on power, and the recipe fallthrough would zero progress).
        if (m.type == BlockId::Generator || m.type == BlockId::RainBarrel) continue;
        m.crafting = false;
        if (!m_power.energized(pos.x, pos.y, pos.z)) continue;

        // Miners harvest the nearest grown resource node in reach instead of
        // running recipes; the patch regrows from its source, bounding the rate.
        if (m.type == BlockId::Miner) {
            // A raw item in the input buffer acts as a filter: mine only that
            // node type. Empty input = mine anything nearby.
            const BlockId filterNode = nodeForRaw(minerFilter(m));

            // The miner commits to one node per harvest. One cheap read per
            // tick validates it (it may be mined away or the filter changed);
            // the full reach scan runs only to acquire, every few ticks.
            if (m.hasTarget) {
                const BlockId t = m_world->getBlock(m.target.x, m.target.y, m.target.z);
                if (!isResourceNode(t) ||
                    (filterNode != BlockId::Air && t != filterNode)) {
                    m.hasTarget = false;
                }
            }
            if (!m.hasTarget) {
                if (--m.rescanCooldown > 0) {
                    m.progress = 0.0f;
                    continue;
                }
                m.rescanCooldown = kMinerIdleRescanTicks;

                glm::ivec3 best{0};
                int bestDist2 = INT_MAX;
                for (int dz = -kMineRadius; dz <= kMineRadius; ++dz) {
                    for (int dx = -kMineRadius; dx <= kMineRadius; ++dx) {
                        for (int dy = -3; dy <= 3; ++dy) {
                            const glm::ivec3 c = pos + glm::ivec3(dx, dy, dz);
                            const BlockId node = m_world->getBlock(c.x, c.y, c.z);
                            if (!isResourceNode(node)) continue;
                            if (filterNode != BlockId::Air && node != filterNode) continue;
                            const int d2 = dx * dx + dy * dy + dz * dz;
                            if (d2 < bestDist2) {
                                bestDist2 = d2;
                                best = c;
                            }
                        }
                    }
                }
                if (bestDist2 == INT_MAX) {
                    m.progress = 0.0f; // nothing in reach; idle until the patch regrows
                    continue;
                }
                m.target = best;
                m.hasTarget = true;
            }

            m.crafting = true;
            m.craftTime = kMineSeconds;
            m.progress += kTickSeconds;
            if (m.progress >= kMineSeconds) {
                m.progress = 0.0f;
                const ItemStack drop = blockDrop(m_world->getBlock(m.target.x, m.target.y, m.target.z));
                m.output.add(drop.id, drop.count);
                m_world->setBlock(m.target.x, m.target.y, m.target.z, BlockId::Air);
                m.hasTarget = false;
            }
            continue;
        }

        const MachineRecipe* active = nullptr;
        const auto candidates = recipesForMachine(m.type);
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            // A selected recipe locks the machine to it; -1 = first ready one.
            if (m.selectedRecipe >= 0 && static_cast<int>(i) != m.selectedRecipe) continue;
            bool ok = true;
            for (const ItemStack& in : candidates[i]->inputs) {
                if (!m.input.has(in.id, in.count)) { ok = false; break; }
            }
            if (ok) { active = candidates[i]; break; }
        }
        if (!active) { m.progress = 0.0f; continue; }

        m.crafting = true;
        m.craftTime = active->seconds;
        m.progress += kTickSeconds;
        if (m.progress >= active->seconds) {
            for (const ItemStack& in : active->inputs) m.input.remove(in.id, in.count);
            m.output.add(active->output.id, active->output.count);
            m.progress = 0.0f;
        }
    }

    // Advance conduits on a slower cadence so items visibly travel.
    if (++m_beltTimer >= kBeltStepTicks) {
        m_beltTimer = 0;
        beltStep();
    }

    updateCreatures(); // wander + physics (VoxelGameEntities.cpp)
}
