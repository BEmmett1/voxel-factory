// The 20 Hz machine + conduit simulation. Kind dispatch comes from the
// kMachineTraitSeed registry (Machine.h): generators burn fuel into power,
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
#include <utility>

using namespace vg;

namespace MachineSystem {

const Inventory& fuelBuffer(const Machine& mac) {
    return usesFuelSlot(mac.type) ? mac.fuel : mac.input;
}

Inventory& fuelBuffer(Machine& mac) {
    return usesFuelSlot(mac.type) ? mac.fuel : mac.input;
}

const Inventory& bufferFor(const Machine& mac, ItemId item) {
    if (!usesFuelSlot(mac.type) || fuelSeconds(item) <= 0.0f) return mac.input;
    // Fuel that is ALSO an ingredient here is feedstock: a belt feeding wood to
    // a Furnace is stocking the charcoal recipe, not the fire. Anything the
    // recipes never ask for is unambiguously fuel.
    for (const MachineRecipe* r : recipesForMachine(mac.type)) {
        for (const ItemStack& in : r->inputs) {
            if (in.id == item) return mac.input;
        }
    }
    return mac.fuel;
}

Inventory& bufferFor(Machine& mac, ItemId item) {
    return const_cast<Inventory&>(bufferFor(std::as_const(mac), item));
}

int inputCap(const Machine& mac) {
    switch (machineTraits(mac.type).kind) {
        // A pedestal's shallow cap is what keeps a laid pattern readable, and
        // predates all of this.
        case MachineKind::Pedestal: return kPedestalCap;
        // A crate is the ANSWER to a full output, so it has to be deep enough
        // to be worth walking to.
        case MachineKind::Storage:  return kChestCap;
        default:                    return kMachineInputCap;
    }
}

int outputCap(const Machine& mac) {
    switch (machineTraits(mac.type).kind) {
        // A Collector's own collectCap already governs its output, and it is
        // the tighter of the two.
        case MachineKind::Collector: return std::max(machineTraits(mac.type).collectCap,
                                                     kMachineOutputCap);
        // Its output IS the stockpile -- everything a crate holds has already
        // been migrated there for belts to drain.
        case MachineKind::Storage:   return kChestCap;
        default:                     return kMachineOutputCap;
    }
}

namespace {

    // What this machine should light next: the SHORTEST burn it holds, so
    // cheap fuel (sticks) is spent before the good stuff (charcoal) and a
    // stockpile of the latter survives idle chores.
    //
    // There used to be a second rule here -- a machine never burns an item its
    // own recipes consume -- which existed only because fuel and ingredients
    // shared one buffer and a Furnace fed wood could not tell what the wood was
    // FOR. Machines with recipes now have a fuel buffer of their own
    // (usesFuelSlot), so what you load is what you meant, and the rule is gone
    // rather than moved: a Furnace can finally char wood while burning wood.
    // The inference survives in exactly one place, bufferFor(), because a belt
    // has no hands and must guess which pile an arriving item belongs to.
    ItemId pickFuel(const Machine& mac) {
        const Inventory& from = fuelBuffer(mac);
        ItemId best = ItemId::None;
        float  bestSeconds = 0.0f;
        for (const FuelInfo& f : fuelRows()) {
            if (from.count(f.item) <= 0) continue;
            if (best != ItemId::None && f.seconds >= bestSeconds) continue;
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
                fuelBuffer(m).remove(fuel, 1);
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

    // A reaping machine takes the nearest block it wants from the cells around
    // it, banks that block's drop, and leaves something behind. Two machines
    // are exactly this and differ only in those two answers: a Miner wants
    // resource nodes and leaves Air, a Harvester wants ripe crops and leaves a
    // fresh seedling so the field replants itself.
    //
    // `wants(BlockId)` is the target test and `leaves(BlockId)` says what the
    // cell becomes; `onReap` is the registry sync for whatever `leaves` put
    // there. Shared because the interesting parts -- committing to one target
    // and revalidating it cheaply per tick instead of re-scanning, and checking
    // the output cap BEFORE the take rather than after -- are the parts worth
    // getting right once.
    template <typename Wants, typename Leaves, typename OnReap>
    void tickReaper(World& world, const glm::ivec3& pos, Machine& m,
                    float seconds, int radius,
                    Wants wants, Leaves leaves, OnReap onReap) {
        if (m.hasTarget && !wants(world.getBlock(m.target.x, m.target.y, m.target.z))) {
            m.hasTarget = false;
        }
        if (!m.hasTarget) {
            if (--m.rescanCooldown > 0) {
                m.progress = 0.0f;
                return;
            }
            m.rescanCooldown = kMinerIdleRescanTicks;

            glm::ivec3 best{0};
            int bestDist2 = INT_MAX;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    for (int dy = -3; dy <= 3; ++dy) {
                        const glm::ivec3 c = pos + glm::ivec3(dx, dy, dz);
                        if (!wants(world.getBlock(c.x, c.y, c.z))) continue;
                        const int d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 < bestDist2) {
                            bestDist2 = d2;
                            best = c;
                        }
                    }
                }
            }
            if (bestDist2 == INT_MAX) {
                m.progress = 0.0f; // nothing in reach; idle until it regrows
                return;
            }
            m.target = best;
            m.hasTarget = true;
        }

        // A full output stops the machine instead of reaping into a bottomless
        // bucket. The target is already chosen, so its yield is known before
        // the take rather than after.
        const ItemStack yield =
            blockDrop(world.getBlock(m.target.x, m.target.y, m.target.z));
        if (yield.id != ItemId::None &&
            m.output.count(yield.id) + yield.count > outputCap(m)) {
            m.jammed = true;
            m.crafting = true;
            m.craftTime = seconds;
            return;
        }

        m.crafting = true;
        m.craftTime = seconds;
        m.progress += kTickSeconds;
        if (m.progress >= seconds) {
            m.progress = 0.0f;
            const BlockId took = world.getBlock(m.target.x, m.target.y, m.target.z);
            const ItemStack drop = blockDrop(took);
            m.output.add(drop.id, drop.count);
            const BlockId left = leaves(took);
            world.setBlock(m.target.x, m.target.y, m.target.z, left);
            onReap(m.target, left);
            m.hasTarget = false;
        }
    }

    // A miner harvests the nearest grown resource node in reach instead of
    // running recipes; the patch regrows from its source, bounding the rate.
    void tickMiner(World& world, const glm::ivec3& pos, Machine& m) {
        // A raw item in the input buffer acts as a filter: mine only that
        // node type. Empty input = mine anything nearby.
        const BlockId filterNode = nodeForRaw(minerFilter(m));
        tickReaper(
            world, pos, m, kMineSeconds, kMineRadius,
            [&](BlockId b) {
                return isResourceNode(b) &&
                       (filterNode == BlockId::Air || b == filterNode);
            },
            [](BlockId) { return BlockId::Air; },
            [](const glm::ivec3&, BlockId) {});
    }

    // A harvester takes RIPE crops only and resets the cell to stage 0, so the
    // field replants itself and the tilled soil is never disturbed -- untilling
    // on harvest would mean re-tilling every automated field by hand forever.
    void tickHarvester(World& world, const glm::ivec3& pos, Machine& m,
                       CropSystem::CropMap& crops) {
        tickReaper(
            world, pos, m, kHarvestSeconds, kHarvestRadius,
            [](BlockId b) { return CropSystem::isRipe(b); },
            [](BlockId) { return CropSystem::cropAtStage(0); },
            // The replanted seedling needs its growth timer, exactly as a
            // hand-placed one gets from WorldEdit -- without this the field
            // reaps once and then stands still.
            [&](const glm::ivec3& at, BlockId) { crops[at] = 0.0f; });
    }

    // An irrigator spends one Rain Water per kIrrigateSeconds of WETNESS, which
    // it banks in `progress` exactly as a generator banks its burn. The bank is
    // what CropSystem reads (via activeIrrigators), so a field keeps growing
    // through the gap between one bucket and the next instead of stuttering.
    void tickIrrigator(Machine& m) {
        if (m.progress > 0.0f) m.progress = std::max(0.0f, m.progress - kTickSeconds);
        if (m.progress <= 0.0f && m.input.count(ItemId::SpringWater) > 0) {
            m.input.remove(ItemId::SpringWater, 1);
            m.progress = kIrrigateSeconds;
        }
        m.crafting = m.progress > 0.0f;
        m.craftTime = kIrrigateSeconds;
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

        // Nowhere to put it: hold the ritual rather than consume the necklace.
        const ItemStack made = match.recipe->output;
        if (made.id != ItemId::None &&
            core.output.count(made.id) + made.count > outputCap(core)) {
            core.jammed = true;
            core.crafting = true;
            core.craftTime = AlchemyCircle::craftSeconds(*match.recipe, tier, energized);
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

    // Does this machine WANT `item` at all, ignoring how full it is? Split out
    // so the capacity rule lives in ONE place rather than being repeated down
    // every branch -- and so a kind that grows its own cap (a crate) changes
    // inputCap and nothing here.
    bool wantsItem(const Machine& mac, ItemId item) {
        const MachineTraits& t = machineTraits(mac.type);
        switch (t.kind) {
            case MachineKind::Generator: return fuelSeconds(item) > 0.0f;
            case MachineKind::Collector: return false; // the environment fills it
            case MachineKind::Miner:     return nodeForRaw(item) != BlockId::Air;
                                         // raws are filters (not consumed)
            // A harvester reads the field, not its input: there is nothing to
            // deliver to it, and taking deliveries would let a belt silently
            // fill a buffer that never empties.
            case MachineKind::Harvester: return false;
            // Water and nothing else: a belt from a Rain Barrel is the whole
            // supply chain, and letting anything else in would just let a
            // mixed line silently fill a buffer that never drains.
            case MachineKind::Irrigator: return item == ItemId::SpringWater;
            case MachineKind::RuneCore: {
                // The core's own buffer holds the CENTRE catalyst only; ring
                // ingredients belong on the pedestals.
                for (const CircleRecipe& r : circleRecipes()) {
                    if (r.center.id == item) return true;
                }
                return false;
            }
            case MachineKind::Pedestal: {
                // A one-item-TYPE holder: it takes anything while empty, then
                // only more of the same (up to inputCap -- kPedestalCap here).
                // Belts can therefore keep a pattern topped up but can never
                // contaminate a laid slot.
                if (mac.input.count(item) > 0) return true;
                for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
                    if (mac.input.count(static_cast<ItemId>(i)) > 0) return false;
                }
                return true;
            }
            case MachineKind::Storage:   return true; // a crate takes anything
            case MachineKind::Processor: break;
        }
        // A fuel-fired processor takes fuel as well as ingredients, so a belt
        // can keep the fire going. (An item that is BOTH is still just an
        // ingredient — see pickFuel.)
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

    // Room for every product this craft could yield. Asked BEFORE rollOutput,
    // because that roll advances the world's shared RNG counter and one thrown
    // away here would desync a sifting line from its own save. So a weighted
    // recipe needs room for every face it could roll, not just the one it would
    // have drawn -- stricter, deterministic, and the right behaviour anyway: a
    // Sifter whose iron pile is full should stop, not quietly skip the iron.
    bool outputHasRoom(const Machine& mac, const MachineRecipe& r) {
        const int cap = outputCap(mac);
        for (const RecipeOutput& o : r.outputs) {
            if (o.stack.id == ItemId::None || o.stack.count <= 0) continue;
            if (mac.output.count(o.stack.id) + o.stack.count > cap) return false;
        }
        return true;
    }

} // namespace

bool machineAccepts(const Machine& mac, ItemId item) {
    if (!wantsItem(mac, item)) return false;
    // A crate's stock lives in `output` -- its tick migrates it there so belts
    // can drain it -- so how full it is has to count BOTH halves, or the cap
    // would never bind and a crate would swallow the world.
    if (machineTraits(mac.type).kind == MachineKind::Storage) {
        return mac.input.count(item) + mac.output.count(item) < inputCap(mac);
    }
    // The capacity half. beltStep leaves an item sitting on a belt whose target
    // refuses it, so this is the whole of what makes a feed line back up.
    return bufferFor(mac, item).count(item) < inputCap(mac);
}

ItemId minerFilter(const Machine& mac) {
    for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
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
        if (!m.enabled) { m.crafting = false; continue; } // off = frozen
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
                 std::uint32_t seed, std::uint32_t& rngCounter,
                 CropSystem::CropMap& crops) {
    for (auto& [pos, m] : machines) {
        const MachineTraits& traits = machineTraits(m.type);
        // Generators and collectors ran in tickSelfPowered (their state does
        // not gate on power, and the recipe fallthrough would zero their
        // progress).
        if (traits.kind == MachineKind::Generator ||
            traits.kind == MachineKind::Collector) continue;
        m.crafting = false;
        m.jammed = false; // re-derived below; never saved

        // The master switch. Deliberately the FIRST thing asked, before power,
        // fuel, recipes or the crank: "off" should mean off for every kind at
        // once rather than being re-implemented per branch. Progress and every
        // buffer are kept, so switching back on resumes mid-craft.
        if (!m.enabled) continue;

        // The Rune Core runs BEFORE the power gate: a Lesser circle is
        // deliberately allowed to work on a dead network (slowly), so the
        // Circle can bootstrap the Generator that would power it.
        if (traits.kind == MachineKind::RuneCore) {
            tickRuneCore(world, machines, pos, m, power.energized(pos.x, pos.y, pos.z));
            continue;
        }
        if (traits.kind == MachineKind::Pedestal) continue; // a passive holder

        // A crate, entire. beltStep fills a machine's `input` and drains its
        // `output`, so migrating one to the other is what makes a single block
        // both feedable and drainable with no belt code of its own. Before the
        // power gate because a crate draws none.
        if (traits.kind == MachineKind::Storage) {
            for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                const int n = m.input.count(id);
                if (n <= 0) continue;
                m.input.remove(id, n);
                m.output.add(id, n);
            }
            continue;
        }

        if (traits.demand > 0 && !power.energized(pos.x, pos.y, pos.z)) continue;

        if (traits.kind == MachineKind::Miner) {
            tickMiner(world, pos, m);
            continue;
        }
        if (traits.kind == MachineKind::Harvester) {
            tickHarvester(world, pos, m, crops);
            continue;
        }
        if (traits.kind == MachineKind::Irrigator) {
            tickIrrigator(m);
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
        if (!active) {
            m.progress = 0.0f;
            // Work banked against a machine that has nothing to make is work
            // aimed at no job: drop it rather than let it land on the next one.
            m.crankBanked = 0.0f;
            continue;
        }

        // Nowhere to put the product: HOLD, exactly like the unpowered and
        // out-of-fuel cases below. Sits before the fuel block on purpose, so a
        // jammed burner doesn't eat its stock standing still -- the generator's
        // "hungry" rule again. Inputs are untouched, so unjamming it (drain the
        // output, or belt it into a crate) resumes the same craft where it
        // stopped.
        if (!outputHasRoom(m, *active)) {
            m.jammed = true;
            m.crafting = true;
            m.craftTime = active->seconds * traits.speedMult;
            continue;
        }

        // The hand-cranked tier does not run on time. Its progress is whatever
        // the player banked by turning the handle since the last tick (see the
        // panel's crank input), so a loaded Bloomery with nobody at it simply
        // stands there. A powered machine still advances by the clock, which
        // for it is always a positive step.
        const float step = traits.handCranked ? m.crankBanked : kTickSeconds;

        // Fuel-fired machines: heat is their power. Fuel is lit only once there
        // is something to make, so a loaded Furnace doesn't burn its stock down
        // while idle -- the generator's "hungry" rule applied to a recipe. Out
        // of fuel HOLDS progress rather than losing it, exactly like the
        // unpowered case above. Gating on `step` adds the cranked case for
        // free: an unattended Bloomery burns nothing, because nobody is working
        // it. Note this bails BEFORE the bank is spent, so a turn of the handle
        // against an unlit fire is owed to the player, not swallowed.
        if (traits.burnsFuel && step > 0.0f) {
            if (m.burnLeft <= 0.0f) {
                const ItemId fuel = pickFuel(m);
                if (fuel == ItemId::None) continue;
                fuelBuffer(m).remove(fuel, 1);
                m.burnLeft = fuelSeconds(fuel) * traits.fuelMult;
            }
            m.burnLeft = std::max(0.0f, m.burnLeft - kTickSeconds);
        }
        m.crankBanked = 0.0f; // spent below (or there was nothing to spend)

        // What the recipe costs here. speedMult is the manual tier's price:
        // for a cranked machine it buys rotations, not seconds.
        const float seconds = active->seconds * traits.speedMult;
        m.crafting = true;
        m.craftTime = seconds;
        m.progress += step;
        if (m.progress >= seconds) {
            for (const ItemStack& in : active->inputs) m.input.remove(in.id, in.count);
            const ItemStack won = rollOutput(*active, seed, rngCounter);
            if (won.id != ItemId::None && won.count > 0) m.output.add(won.id, won.count);
            m.progress = 0.0f;
        }
    }
}

std::vector<glm::ivec3> activeIrrigators(const MachineMap& machines) {
    std::vector<glm::ivec3> out;
    for (const auto& [pos, m] : machines) {
        if (machineTraits(m.type).kind != MachineKind::Irrigator) continue;
        // `enabled` matters here as much as anywhere: switching an irrigator
        // off has to actually stop the water, not merely stop it drinking.
        if (m.enabled && m.progress > 0.0f) out.push_back(pos);
    }
    return out;
}

void beltStep(BeltMap& belts, MachineMap& machines) {
    // Cargo motion is re-derived from scratch every step: clear it first, and
    // only an item that actually MOVES below gets a direction. A belt whose
    // cargo is stuck therefore renders parked in the middle of its cell, which
    // is what a jam should look like.
    for (auto& [pos, b] : belts) b.cameFrom = glm::ivec3(0);

    // 1. Belts deliver their item into a machine directly ahead (if it accepts).
    for (auto& [pos, b] : belts) {
        if (b.item == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto mit = machines.find(front);
        if (mit != machines.end() && machineAccepts(mit->second, b.item)) {
            bufferFor(mit->second, b.item).add(b.item, 1);
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
        // A filtered belt refuses what it isn't for, which is what turns a run
        // of them into sorting LANES. The refused item stays put and the line
        // behind it backs up -- visibly, since both the stuck cargo and the
        // target's filter draw as icons.
        if (tb->second.filter != ItemId::None && tb->second.filter != carried) continue;
        tb->second.item = carried;
        // Recorded on the RECEIVER, pointing back at where it came from: a
        // corner turns, so the direction it arrived from is not the direction
        // it will leave by.
        tb->second.cameFrom = -b.facing;
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
        // A filtered belt draws exactly one thing. Unfiltered, it falls back to
        // the old ordinal scan -- still arbitrary, but now it is the DEFAULT
        // rather than the only option, and the fix is one keypress on the belt.
        if (b.filter != ItemId::None) {
            if (out.count(b.filter) > 0) {
                out.remove(b.filter, 1);
                b.item = b.filter;
                b.cameFrom = -b.facing; // out of the machine behind
            }
            continue;
        }
        for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            if (out.count(id) > 0) {
                out.remove(id, 1);
                b.item = id;
                b.cameFrom = -b.facing;
                break;
            }
        }
    }
}

} // namespace MachineSystem
