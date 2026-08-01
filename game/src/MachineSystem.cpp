// The 20 Hz machine + conduit simulation. Kind dispatch comes from the
// kMachineTraits registry (Machine.h): generators burn fuel into power,
// collectors gather from the environment, miners harvest nodes, processors
// run their MachineRecipe lists. World mutations rely on the caller's
// per-frame dirty sweep to update meshes.

#include "game/MachineSystem.h"

#include "VoxelGameInternal.h"
#include "game/AlchemyCircle.h"
#include "game/Recipes.h"
#include "game/World.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <climits>

using namespace vg;

namespace MachineSystem {

namespace {

    // What this machine should light next: the SHORTEST burn it holds, so
    // cheap fuel (sticks) is spent before the good stuff (charcoal) and a
    // stockpile of the latter survives idle chores.
    //
    // A machine never burns an item its own recipes consume. A Furnace fed
    // wood is there to CHAR that wood, not to eat it, and without this rule a
    // fuel-fired machine quietly devours its own feedstock. Generators have no
    // recipes, so nothing is excluded and they burn whatever they are handed.
    ItemId pickFuel(const Machine& mac) {
        const auto recipes = recipesForMachine(mac.type);
        auto isIngredient = [&](ItemId item) {
            for (const MachineRecipe* r : recipes) {
                for (const ItemStack& in : r->inputs) {
                    if (in.id == item) return true;
                }
            }
            return false;
        };

        ItemId best = ItemId::None;
        float  bestSeconds = 0.0f;
        for (const FuelInfo& f : kFuels) {
            if (mac.input.count(f.item) <= 0) continue;
            if (best != ItemId::None && f.seconds >= bestSeconds) continue;
            if (isIngredient(f.item)) continue;
            best = f.item;
            bestSeconds = f.seconds;
        }
        return best;
    }

    // Draw this craft's product. One output is the ordinary case and doesn't
    // touch the counter at all, so only a genuinely random machine (the Sifter)
    // perturbs the world's roll sequence. Randomness rides the same
    // seed + saved counter as node growth and weather, so a sifting line
    // replays identically across a save/load.
    ItemStack rollOutput(const MachineRecipe& r, std::uint32_t seed,
                         std::uint32_t& rngCounter) {
        if (r.outputs.size() <= 1) return primaryOutput(r);

        float total = 0.0f;
        for (const RecipeOutput& o : r.outputs) total += std::max(0.0f, o.weight);
        if (total <= 0.0f) return primaryOutput(r);

        const std::uint32_t h = hash2(static_cast<int>(rngCounter++), 4409, seed);
        float pick = total * static_cast<float>(h % 4096u) / 4096.0f;
        for (const RecipeOutput& o : r.outputs) {
            pick -= std::max(0.0f, o.weight);
            if (pick < 0.0f) return o.stack;
        }
        return r.outputs.back().stack;
    }

    // A generator burns fuel; a new unit is lit only when the network wants
    // power (`hungry`, from the last solve), but a lit one burns out fully.
    // Machine::progress holds the burn seconds LEFT. Returns true when the
    // burn state flipped (the caller re-solves power once per tick).
    bool tickGenerator(Machine& m, const MachineTraits& t, bool hungry) {
        const bool wasBurning = m.progress > 0.0f;
        if (m.progress > 0.0f) {
            m.progress = std::max(0.0f, m.progress - kTickSeconds);
        }
        if (m.progress <= 0.0f && hungry) {
            if (const ItemId fuel = pickFuel(m); fuel != ItemId::None) {
                m.input.remove(fuel, 1);
                m.progress = fuelSeconds(fuel) * t.fuelMult;
                m.craftTime = m.progress; // the gauge's full scale is THIS fuel
            }
        }
        m.crafting = m.progress > 0.0f;
        // craftTime isn't saved, so after a load widen it to whatever is still
        // burning -- otherwise the gauge would read past full until it goes out.
        if (m.progress > m.craftTime) m.craftTime = m.progress;
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

    // The Rune Core reads the ring of Pedestals around it and runs whichever
    // CircleRecipe the necklace spells. A Lesser (4-pedestal) circle ignores
    // power entirely and runs slowly -- that unpowered path is what lets a
    // circle build your first Generator. Power only buys speed and the
    // eight-slot patterns.
    void tickRuneCore(World& world, MachineMap& machines, const glm::ivec3& pos,
                      Machine& core, bool energized) {
        const AlchemyCircle::Tier tier = AlchemyCircle::tierAt(world, machines, pos);
        if (tier == AlchemyCircle::Tier::None) {
            core.progress = 0.0f;
            return;
        }
        const auto ring = AlchemyCircle::ringContents(world, machines, pos);
        const AlchemyCircle::Match match =
            AlchemyCircle::findMatch(ring, core.input, tier, energized, core.selectedRecipe);
        if (!match) {
            core.progress = 0.0f;
            return;
        }

        core.crafting = true;
        core.craftTime = AlchemyCircle::craftSeconds(*match.recipe, tier, energized);
        core.progress += kTickSeconds;
        if (core.progress >= core.craftTime) {
            AlchemyCircle::consume(world, machines, pos, match, core.input);
            core.output.add(match.recipe->output.id, match.recipe->output.count);
            core.progress = 0.0f;
        }
    }

} // namespace

bool machineAccepts(const Machine& mac, ItemId item) {
    const MachineTraits& t = machineTraits(mac.type);
    switch (t.kind) {
        case MachineKind::Generator: return fuelSeconds(item) > 0.0f;
        case MachineKind::Collector: return false; // the environment fills it
        case MachineKind::Miner:     return nodeForRaw(item) != BlockId::Air;
                                     // raws are filters (not consumed)
        case MachineKind::RuneCore: {
            // The core's own buffer holds the CENTRE catalyst only; ring
            // ingredients belong on the pedestals.
            for (const CircleRecipe& r : circleRecipes()) {
                if (r.center.id == item) return true;
            }
            return false;
        }
        case MachineKind::Pedestal: {
            // A one-item-TYPE holder: it takes anything while empty, then only
            // more of the same, up to the cap. Belts can therefore keep a
            // pattern topped up but can never contaminate a laid slot.
            if (mac.input.count(item) > 0) return mac.input.count(item) < kPedestalCap;
            for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
                if (mac.input.count(static_cast<ItemId>(i)) > 0) return false;
            }
            return true;
        }
        case MachineKind::Processor: break;
    }
    // A fuel-fired processor takes fuel as well as ingredients, so a belt can
    // keep the fire going. (An item that is BOTH is still just an ingredient —
    // see pickFuel.)
    if (t.burnsFuel && fuelSeconds(item) > 0.0f) return true;
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

void tickPowered(World& world, MachineMap& machines, const PowerState& power,
                 std::uint32_t seed, std::uint32_t& rngCounter) {
    for (auto& [pos, m] : machines) {
        const MachineTraits& traits = machineTraits(m.type);
        // Generators and collectors ran in tickSelfPowered (their state does
        // not gate on power, and the recipe fallthrough would zero their
        // progress).
        if (traits.kind == MachineKind::Generator ||
            traits.kind == MachineKind::Collector) continue;
        m.crafting = false;

        // The Rune Core runs BEFORE the power gate: a Lesser circle is
        // deliberately allowed to work on a dead network (slowly), so the
        // Circle can bootstrap the Generator that would power it.
        if (traits.kind == MachineKind::RuneCore) {
            tickRuneCore(world, machines, pos, m, power.energized(pos.x, pos.y, pos.z));
            continue;
        }
        if (traits.kind == MachineKind::Pedestal) continue; // a passive holder

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

        // Fuel-fired machines: heat is their power. Fuel is lit only once there
        // is something to make, so a loaded Furnace doesn't burn its stock down
        // while idle -- the generator's "hungry" rule applied to a recipe. Out
        // of fuel HOLDS progress rather than losing it, exactly like the
        // unpowered case above.
        if (traits.burnsFuel) {
            if (m.burnLeft <= 0.0f) {
                const ItemId fuel = pickFuel(m);
                if (fuel == ItemId::None) continue;
                m.input.remove(fuel, 1);
                m.burnLeft = fuelSeconds(fuel) * traits.fuelMult;
            }
            m.burnLeft = std::max(0.0f, m.burnLeft - kTickSeconds);
        }

        // The manual tier's entire cost: the same recipe, times longer.
        const float seconds = active->seconds * traits.speedMult;
        m.crafting = true;
        m.craftTime = seconds;
        m.progress += kTickSeconds;
        if (m.progress >= seconds) {
            for (const ItemStack& in : active->inputs) m.input.remove(in.id, in.count);
            const ItemStack won = rollOutput(*active, seed, rngCounter);
            if (won.id != ItemId::None && won.count > 0) m.output.add(won.id, won.count);
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
