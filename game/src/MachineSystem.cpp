// The 20 Hz machine + conduit simulation. Kind dispatch comes from the
// kMachineTraits registry (Machine.h): generators burn fuel into power,
// collectors gather from the environment, miners harvest nodes, processors
// run their MachineRecipe lists. World mutations rely on the caller's
// per-frame dirty sweep to update meshes.

#include "game/MachineSystem.h"

#include "VoxelGameInternal.h"
#include "game/Recipes.h"
#include "game/World.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <climits>

using namespace vg;

namespace MachineSystem {

namespace {

    // A generator burns fuel; a new unit is lit only when the network wants
    // power (`hungry`, from the last solve), but a lit one burns out fully.
    // Machine::progress holds the burn seconds LEFT. Returns true when the
    // burn state flipped (the caller re-solves power once per tick).
    bool tickGenerator(Machine& m, const MachineTraits& t, bool hungry) {
        const bool wasBurning = m.progress > 0.0f;
        if (m.progress > 0.0f) {
            m.progress = std::max(0.0f, m.progress - kTickSeconds);
        }
        if (m.progress <= 0.0f && hungry && m.input.count(t.fuel) > 0) {
            m.input.remove(t.fuel, 1);
            m.progress = t.burnSeconds;
        }
        m.crafting = m.progress > 0.0f;
        m.craftTime = t.burnSeconds;
        return (m.progress > 0.0f) != wasBurning;
    }

    // A collector gathers its item from the environment while conditions
    // hold (for the Rain Barrel: raining + open sky). Needs no power.
    void tickCollector(Machine& m, const MachineTraits& t, bool gathering) {
        const bool filling = gathering && m.output.count(t.collects) < t.collectCap;
        m.crafting = filling;
        m.craftTime = t.collectSeconds;
        if (!filling) return;
        m.progress += kTickSeconds;
        if (m.progress >= t.collectSeconds) {
            m.progress = 0.0f;
            m.output.add(t.collects, 1);
        }
    }

    // A miner harvests the nearest grown resource node in reach instead of
    // running recipes; the patch regrows from its source, bounding the rate.
    void tickMiner(World& world, const glm::ivec3& pos, Machine& m) {
        // A raw item in the input buffer acts as a filter: mine only that
        // node type. Empty input = mine anything nearby.
        const BlockId filterNode = nodeForRaw(minerFilter(m));

        // The miner commits to one node per harvest. One cheap read per
        // tick validates it (it may be mined away or the filter changed);
        // the full reach scan runs only to acquire, every few ticks.
        if (m.hasTarget) {
            const BlockId t = world.getBlock(m.target.x, m.target.y, m.target.z);
            if (!isResourceNode(t) ||
                (filterNode != BlockId::Air && t != filterNode)) {
                m.hasTarget = false;
            }
        }
        if (!m.hasTarget) {
            if (--m.rescanCooldown > 0) {
                m.progress = 0.0f;
                return;
            }
            m.rescanCooldown = kMinerIdleRescanTicks;

            glm::ivec3 best{0};
            int bestDist2 = INT_MAX;
            for (int dz = -kMineRadius; dz <= kMineRadius; ++dz) {
                for (int dx = -kMineRadius; dx <= kMineRadius; ++dx) {
                    for (int dy = -3; dy <= 3; ++dy) {
                        const glm::ivec3 c = pos + glm::ivec3(dx, dy, dz);
                        const BlockId node = world.getBlock(c.x, c.y, c.z);
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
                return;
            }
            m.target = best;
            m.hasTarget = true;
        }

        m.crafting = true;
        m.craftTime = kMineSeconds;
        m.progress += kTickSeconds;
        if (m.progress >= kMineSeconds) {
            m.progress = 0.0f;
            const ItemStack drop = blockDrop(world.getBlock(m.target.x, m.target.y, m.target.z));
            m.output.add(drop.id, drop.count);
            world.setBlock(m.target.x, m.target.y, m.target.z, BlockId::Air);
            m.hasTarget = false;
        }
    }

} // namespace

bool machineAccepts(const Machine& mac, ItemId item) {
    const MachineTraits& t = machineTraits(mac.type);
    switch (t.kind) {
        case MachineKind::Generator: return item == t.fuel;
        case MachineKind::Collector: return false; // the environment fills it
        case MachineKind::Miner:     return nodeForRaw(item) != BlockId::Air;
                                     // raws are filters (not consumed)
        case MachineKind::Processor: break;
    }
    const auto recipes = recipesForMachine(mac.type);
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        if (mac.selectedRecipe >= 0 && static_cast<int>(i) != mac.selectedRecipe) continue;
        for (const ItemStack& in : recipes[i]->inputs) {
            if (in.id == item) return true;
        }
    }
    return false;
}

ItemId minerFilter(const Machine& mac) {
    for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemId id = static_cast<ItemId>(i);
        if (mac.input.count(id) > 0 && nodeForRaw(id) != BlockId::Air) return id;
    }
    return ItemId::None;
}

bool tickSelfPowered(const World& world, MachineMap& machines,
                     const std::unordered_set<glm::ivec3, IVec3Hash>& hungryGenerators,
                     bool raining) {
    bool powerChanged = false;
    for (auto& [pos, m] : machines) {
        const MachineTraits& t = machineTraits(m.type);
        switch (t.kind) {
            case MachineKind::Generator:
                powerChanged |= tickGenerator(m, t, hungryGenerators.count(pos) > 0);
                break;
            case MachineKind::Collector:
                tickCollector(m, t, raining && skyVisible(world, pos.x, pos.y, pos.z));
                break;
            default:
                break;
        }
    }
    return powerChanged;
}

void tickPowered(World& world, MachineMap& machines, const PowerState& power) {
    for (auto& [pos, m] : machines) {
        const MachineTraits& traits = machineTraits(m.type);
        // Generators and collectors ran in tickSelfPowered (their state does
        // not gate on power, and the recipe fallthrough would zero their
        // progress).
        if (traits.kind == MachineKind::Generator ||
            traits.kind == MachineKind::Collector) continue;
        m.crafting = false;
        if (traits.demand > 0 && !power.energized(pos.x, pos.y, pos.z)) continue;

        if (traits.kind == MachineKind::Miner) {
            tickMiner(world, pos, m);
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
}

void beltStep(BeltMap& belts, MachineMap& machines) {
    // 1. Belts deliver their item into a machine directly ahead (if it accepts).
    for (auto& [pos, b] : belts) {
        if (b.item == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto mit = machines.find(front);
        if (mit != machines.end() && machineAccepts(mit->second, b.item)) {
            mit->second.input.add(b.item, 1);
            b.item = ItemId::None;
        }
    }

    // 2. Hop items belt -> belt. Use a snapshot of pre-step contents so an item
    //    advances at most one belt, and claim targets so two items never merge.
    std::unordered_map<glm::ivec3, ItemId, IVec3Hash> before;
    before.reserve(belts.size());
    for (const auto& [pos, b] : belts) before[pos] = b.item;

    std::unordered_set<glm::ivec3, IVec3Hash> claimed;
    for (auto& [pos, b] : belts) {
        const ItemId carried = before[pos];
        if (carried == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto tb = belts.find(front);
        if (tb == belts.end()) continue;          // ahead is not a belt
        if (before[front] != ItemId::None) continue; // target was occupied
        if (claimed.count(front)) continue;       // already filled this step
        tb->second.item = carried;
        b.item = ItemId::None;
        claimed.insert(front);
    }

    // 3. Empty belts pull one item from a machine's output directly behind them.
    for (auto& [pos, b] : belts) {
        if (b.item != ItemId::None) continue;
        const glm::ivec3 back = pos - b.facing;
        const auto mit = machines.find(back);
        if (mit == machines.end()) continue;
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

} // namespace MachineSystem
