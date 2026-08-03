#include "game/ContentValidate.h"

#include "game/AlchemyCircle.h"
#include "game/Block.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"
#include "game/World.h"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

// Everything here used to live inside runSelfTest() in main.cpp, in the same
// order, saying the same things. What changed is only who may ask: a validator
// that returns text can be run against content that arrived at RUNTIME, which
// an exit code cannot.

namespace {

    std::string name(ItemId id) { return itemName(id); }

    // "recipe 'press/plate'" / "circle 'circle/wrench'" -- every diagnostic
    // names the row it is about, since that is what the author edits.
    std::string key(const MachineRecipe& r) { return "recipe '" + std::string(r.key) + "'"; }
    std::string key(const CircleRecipe& r)  { return "circle '" + std::string(r.key) + "'"; }

    // ---- Keys are the identity, so they must be unique and non-empty -------
    // The keys ARE the save format for a locked machine: a duplicate makes two
    // recipes indistinguishable on load, and an empty one makes a lock
    // unsaveable. Nothing else in the build catches either.
    void checkKeys(const std::vector<std::string>& keys, const char* table,
                   std::vector<std::string>& out) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].empty()) {
                out.push_back(std::string("a ") + table + " recipe has an empty key");
                continue;
            }
            for (std::size_t j = i + 1; j < keys.size(); ++j) {
                if (keys[i] == keys[j]) {
                    out.push_back(std::string("duplicate ") + table + " recipe key '" +
                                  keys[i] + "'");
                }
            }
        }
    }

    void checkRecipeKeys(std::vector<std::string>& out) {
        std::vector<std::string> hand, mach, circ;
        for (const Recipe& r : handcraftRecipes()) hand.push_back(r.key);
        for (const MachineRecipe& r : machineRecipes()) mach.push_back(r.key);
        for (const CircleRecipe& r : circleRecipes()) circ.push_back(r.key);
        checkKeys(hand, "hand-craft", out);
        checkKeys(mach, "machine", out);
        checkKeys(circ, "circle", out);

        // A machine recipe must name a machine that actually RUNS a list.
        // recipesForMachine() resolves a manual twin through its recipeGroup,
        // so a row addressed to the twin (a natural mistake to write) would be
        // returned by nothing at all -- a dead row rather than an error.
        for (const MachineRecipe& r : machineRecipes()) {
            if (!isMachine(r.machine)) {
                out.push_back(key(r) + " names " + blockName(r.machine) +
                              ", which is not a machine");
            } else if (recipeGroupFor(r.machine) != r.machine) {
                out.push_back(key(r) + " names " + blockName(r.machine) +
                              ", which runs " + blockName(recipeGroupFor(r.machine)) +
                              "'s recipes -- address it to that machine instead");
            }
        }

        // Round-trip: a key resolves back to the row it names, for every row of
        // every machine -- including the manual twins, which reach their
        // powered counterpart's list through MachineTraits::recipeGroup.
        for (int b = 1; b < static_cast<int>(BlockId::Count); ++b) {
            const BlockId type = static_cast<BlockId>(b);
            if (!isMachine(type)) continue;
            const auto rows = recipesForMachine(type);
            for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
                if (recipeIndexForKey(type, recipeKeyFor(type, i)) != i) {
                    out.push_back(std::string("recipe key '") + recipeKeyFor(type, i) +
                                  "' does not resolve back to its own row on " +
                                  blockName(type));
                }
            }
            // A key that no longer names anything lands on AUTO, never on
            // whatever row happens to sit at some index today. This is the
            // whole promise that lets the tables be edited freely.
            if (recipeIndexForKey(type, "no/such/recipe") != -1 ||
                recipeIndexForKey(type, "") != -1) {
                out.push_back(std::string("an unknown recipe key does not resolve to AUTO on ") +
                              blockName(type));
            }
        }
        for (int i = 0; i < static_cast<int>(circleRecipes().size()); ++i) {
            if (circleIndexForKey(circleKeyFor(i)) != i) {
                out.push_back(std::string("circle key '") + circleKeyFor(i) +
                              "' does not resolve back to its own row");
            }
        }
        if (circleIndexForKey("no/such/recipe") != -1) {
            out.push_back("an unknown circle key does not resolve to AUTO");
        }

        // A manual twin must run EXACTLY its powered counterpart's rows, or the
        // two tiers would drift and a lock would not survive an upgrade.
        for (const MachineTraits& traits : kMachineTraits) {
            if (traits.recipeGroup == BlockId::Air) continue;
            const auto mine = recipesForMachine(traits.block);
            const auto theirs = recipesForMachine(traits.recipeGroup);
            bool same = mine.size() == theirs.size();
            for (std::size_t i = 0; same && i < mine.size(); ++i) same = mine[i] == theirs[i];
            if (!same) {
                out.push_back(std::string(blockName(traits.block)) + " does not run exactly " +
                              blockName(traits.recipeGroup) + "'s recipes");
            }
        }
    }

    // ---- Circle patterns are unambiguous ----------------------------------
    // Ring slots match on "holds AT LEAST this many", so one pattern can be a
    // superset of another and silently shadow it -- a recipe you can lay
    // perfectly and never get. Order is the fix, and this is what checks it:
    // lay each pattern exactly and confirm the matcher returns THAT recipe.
    void checkCircleShadowing(std::vector<std::string>& out) {
        World cw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> cm;
        const glm::ivec3 core{80, 30, 80};
        cw.setBlock(core.x, core.y, core.z, BlockId::RuneCore);
        cm[core].type = BlockId::RuneCore;
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
            const glm::ivec3 p = AlchemyCircle::slotPos(core, sl);
            cw.setBlock(p.x, p.y, p.z, BlockId::Pedestal);
            cm[p].type = BlockId::Pedestal;
        }

        const auto& all = circleRecipes();
        for (std::size_t k = 0; k < all.size(); ++k) {
            const CircleRecipe& want = all[k];
            if (want.ring.size() != 4 && want.ring.size() != 8) {
                out.push_back(key(want) + " has " + std::to_string(want.ring.size()) +
                              " ring slots; a pattern must have 4 (the cardinals) or 8");
                continue;
            }
            for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
                cm[AlchemyCircle::slotPos(core, sl)].input = Inventory{};
            }
            // A 4-slot pattern lists the CARDINALS (the even ring slots).
            const int stride = want.ring.size() == 4 ? 2 : 1;
            for (std::size_t ringIdx = 0; ringIdx < want.ring.size(); ++ringIdx) {
                if (want.ring[ringIdx].id == ItemId::None) continue;
                cm[AlchemyCircle::slotPos(core, static_cast<int>(ringIdx) * stride)]
                    .input.add(want.ring[ringIdx].id, want.ring[ringIdx].count);
            }
            Inventory centre;
            if (want.center.id != ItemId::None) centre.add(want.center.id, want.center.count);

            const auto ring = AlchemyCircle::ringContents(cw, cm, core);
            const auto got = AlchemyCircle::findMatch(ring, centre,
                                                      AlchemyCircle::Tier::Greater, true);
            if (!got || got.recipe != &want) {
                out.push_back(key(want) + " is shadowed by '" +
                              (got ? std::string(got.recipe->key) : std::string("(nothing)")) +
                              "' -- list the more demanding pattern first");
            }
        }
    }

    // ---- Tech-tree reachability (the deadlock check) ----------------------
    // This is what replaces "the recipe tables are append-only". They can now
    // be edited freely, so the guardrail has to be about MEANING rather than
    // ordering: starting from nothing but what the world hands you, the closure
    // over all three recipe surfaces must reach every machine and every recipe
    // input. Edit a recipe into a deadlock and this says so.
    void checkReachability(std::vector<std::string>& out) {
        std::vector<bool> have(static_cast<std::size_t>(ItemId::Count), false);
        auto known = [&](ItemId id) { return have[static_cast<std::size_t>(id)]; };
        auto gain = [&](ItemId id) {
            if (id == ItemId::None || known(id)) return false;
            have[static_cast<std::size_t>(id)] = true;
            return true;
        };

        // Seed: everything the world yields to a bare hand or a tool -- block
        // drops (ore, wood, sand, stone, leaves' sticks), plus the two rain
        // items. Machines you PLACE drop themselves, so seeding block drops
        // would beg the question; only naturally-occurring blocks count.
        for (int b = 1; b < static_cast<int>(BlockId::Count); ++b) {
            const BlockId id = static_cast<BlockId>(b);
            if (isMachine(id) || isSource(id)) continue;
            gain(blockDrop(id).id);
        }
        gain(ItemId::Stick);
        gain(ItemId::Pebble);
        gain(ItemId::SpringWater); // the Bucket in the rain, and the barrel
        // Boss drops enter the economy through COMBAT rather than a recipe, so
        // the closure has to be told about them (kSpecies is private to
        // CreatureSystem.cpp). Anything gated on these is gated on a fight,
        // which is the design, not a deadlock.
        gain(ItemId::VoidCatalyst);
        gain(ItemId::StormCore);

        // Fixpoint over the three surfaces. A machine recipe is only usable
        // once the machine ITSELF is reachable, which is the part that makes
        // this a real bootstrap test rather than a shopping list.
        for (bool changed = true; changed;) {
            changed = false;
            for (const Recipe& r : handcraftRecipes()) {
                bool ok = true;
                for (const ItemStack& in : r.inputs) ok = ok && known(in.id);
                if (ok) changed |= gain(r.output.id);
            }
            for (const CircleRecipe& r : circleRecipes()) {
                bool ok = known(ItemId::RuneCoreItem) && known(ItemId::PedestalItem) &&
                          (r.center.id == ItemId::None || known(r.center.id));
                for (const ItemStack& in : r.ring) ok = ok && (in.id == ItemId::None || known(in.id));
                if (ok) changed |= gain(r.output.id);
            }
            for (const MachineRecipe& r : machineRecipes()) {
                // ANY machine that runs this list will do. The manual twins are
                // the whole point: a Bloomery smelts the Furnace's recipes,
                // which is what breaks the circularity of "ingots need a
                // Furnace, a Furnace needs ingots".
                bool ok = false;
                for (const MachineTraits& mt : kMachineTraits) {
                    if (recipeGroupFor(mt.block) != r.machine) continue;
                    if (known(blockDrop(mt.block).id)) { ok = true; break; }
                }
                for (const ItemStack& in : r.inputs) ok = ok && known(in.id);
                if (!ok) continue;
                for (const RecipeOutput& o : r.outputs) changed |= gain(o.stack.id);
            }
        }

        // Every machine must be buildable, and every recipe input obtainable.
        for (const MachineTraits& traits : kMachineTraits) {
            if (known(blockDrop(traits.block).id)) continue;
            out.push_back(std::string(blockName(traits.block)) + " can never be built");
        }
        for (const MachineRecipe& r : machineRecipes()) {
            for (const ItemStack& in : r.inputs) {
                if (known(in.id)) continue;
                out.push_back(key(r) + " needs unreachable " + name(in.id));
            }
        }
        for (const CircleRecipe& r : circleRecipes()) {
            if (r.center.id != ItemId::None && !known(r.center.id)) {
                out.push_back(key(r) + " needs unreachable " + name(r.center.id));
            }
            for (const ItemStack& in : r.ring) {
                if (in.id == ItemId::None || known(in.id)) continue;
                out.push_back(key(r) + " needs unreachable " + name(in.id));
            }
        }

        // The bootstrap itself: the Alchemy Circle is where nearly every recipe
        // now lives, and its two parts cost Copper Ingots, which cost a fire.
        // So SOME machine that needs neither power nor a circle must be
        // hand-craftable, or a fresh world is stuck at sticks and pebbles.
        if (!known(ItemId::CopperIngot)) {
            out.push_back("nothing reachable smelts a Copper Ingot -- a fresh world "
                          "is stuck at sticks and pebbles");
        }
        if (!known(ItemId::RuneCoreItem) || !known(ItemId::PedestalItem)) {
            out.push_back("the Alchemy Circle's own parts are unreachable, so every "
                          "recipe on it is too");
        }
        if (!known(ItemId::MachineFrame)) {
            out.push_back("Machine Frame is unreachable, so no machine past the "
                          "bootstrap pair can be built");
        }
    }

} // namespace

namespace content {

    std::vector<std::string> validate() {
        std::vector<std::string> out;
        checkRecipeKeys(out);
        checkCircleShadowing(out);
        checkReachability(out);
        return out;
    }

} // namespace content
