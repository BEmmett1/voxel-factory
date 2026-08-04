#include "game/ContentValidate.h"

#include "game/AlchemyCircle.h"
#include "game/Atlas.h"
#include "game/Block.h"
#include "game/BlockShape.h"
#include "game/ContentRegistry.h"
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
    // Shared by the recipe tables and the block/item registries, because it is
    // the same rule for the same reason: a key IS what a save stores, so a
    // duplicate makes two rows indistinguishable on load and an empty one makes
    // the row unsaveable. Nothing else in the build catches either.
    void checkKeys(const std::vector<std::string>& keys, const char* table,
                   std::vector<std::string>& out) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].empty()) {
                out.push_back(std::string("a ") + table + " has an empty key");
                continue;
            }
            for (std::size_t j = i + 1; j < keys.size(); ++j) {
                if (keys[i] == keys[j]) {
                    out.push_back(std::string("duplicate ") + table + " key '" + keys[i] + "'");
                }
            }
        }
    }

    void checkRecipeKeys(std::vector<std::string>& out) {
        std::vector<std::string> hand, mach, circ;
        for (const Recipe& r : handcraftRecipes()) hand.push_back(r.key);
        for (const MachineRecipe& r : machineRecipes()) mach.push_back(r.key);
        for (const CircleRecipe& r : circleRecipes()) circ.push_back(r.key);
        checkKeys(hand, "hand-craft recipe", out);
        checkKeys(mach, "machine recipe", out);
        checkKeys(circ, "circle recipe", out);

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
        for (int b = 1; b < static_cast<int>(blockCount()); ++b) {
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
        for (const MachineTraits& traits : machineTraitRows()) {
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

    // ---- The registries themselves ----------------------------------------
    // Every rule here is a static_assert in Block.cpp / Item.cpp / Machine.h
    // as well, guarding the COMPILED rows. These are the same rules asked of
    // the whole runtime table, which is where a pack's rows are -- and the
    // reason they are worth asking twice is that the consequences are not
    // "wrong behaviour" but memory: a block whose `solid` disagrees with its
    // shape makes the collision code trust a box array that isn't there, and a
    // tile index past the sheet is an out-of-bounds write in the fallback
    // atlas generator.
    void checkRegistries(std::vector<std::string>& out) {
        constexpr std::size_t kTiles = static_cast<std::size_t>(Atlas::Rows * Atlas::Cols);

        // The sentinel has to stay out of reach. A registry this large is
        // absurd today, and would silently make "no such content" name a row.
        if (blockCount() >= static_cast<std::size_t>(content::kNoBlock)) {
            out.push_back("there are too many blocks for the 'no such block' sentinel");
        }
        if (itemCount() >= static_cast<std::size_t>(content::kNoItem)) {
            out.push_back("there are too many items for the 'no such item' sentinel");
        }

        // Keys are the identity: a duplicate aliases two rows everywhere at
        // once -- a save's id table maps both onto one, and two packs claiming
        // the same key overwrite each other.
        std::vector<std::string> keys;
        for (const BlockInfo& b : blockRows()) keys.push_back(b.key ? b.key : "");
        checkKeys(keys, "block", out);
        keys.clear();
        for (const ItemInfo& i : itemRows()) keys.push_back(i.key ? i.key : "");
        checkKeys(keys, "item", out);

        for (std::size_t i = 0; i < blockRows().size(); ++i) {
            const BlockInfo& b = blockRows()[i];
            const std::string named = std::string("block '") +
                                      (b.key ? b.key : "?") + "' ";
            if (static_cast<std::size_t>(b.id) != i) {
                out.push_back(named + "is not at its own ordinal");
            }
            // fullCube decides what the mesher may HIDE behind this block, so a
            // non-solid one would occlude a face you can walk and shoot through.
            if (b.fullCube && !b.solid) out.push_back(named + "is a full cube but not solid");
            if (static_cast<std::size_t>(b.shape) >= static_cast<std::size_t>(ShapeId::Count)) {
                out.push_back(named + "names a shape this build does not have");
                continue; // the checks below would read past kBlockShapes
            }
            // Solidity and geometry must agree, or physics and rendering
            // disagree about where the block is: this is what lets Collision.cpp
            // trust blockBoxes() alone.
            if (b.solid != !blockShape(b.shape).boxes.empty()) {
                out.push_back(named + "is solid exactly when its shape has collision "
                                      "boxes, and these disagree");
            }
            const ShapeAabb& s = blockShape(b.shape).bounds;
            if (b.fullCube && (s.lo != glm::vec3(0.0f) || s.hi != glm::vec3(1.0f))) {
                out.push_back(named + "is a full cube but its shape does not fill the cell");
            }
            // The fallback atlas generator writes a 16x16 swatch at
            // tile % Cols, tile / Cols -- with no bounds check, because until
            // now every tile came from a table a human wrote.
            for (const int tile : {b.tiles.top, b.tiles.side, b.tiles.bottom}) {
                if (tile < 0 || static_cast<std::size_t>(tile) >= kTiles) {
                    out.push_back(named + "uses atlas tile " + std::to_string(tile) +
                                  ", which is outside the " + std::to_string(kTiles) +
                                  "-tile sheet");
                }
            }
            if (b.machine != hasMachineTraits(b.id)) {
                out.push_back(named + (b.machine ? "is a machine with no traits row"
                                                 : "is not a machine but has a traits row"));
            }
            if (b.source && b.spawnsNode == BlockId::Air) {
                out.push_back(named + "is a source that grows nothing");
            }
        }

        for (std::size_t i = 0; i < itemRows().size(); ++i) {
            const ItemInfo& it = itemRows()[i];
            const std::string named = std::string("item '") +
                                      (it.key ? it.key : "?") + "' ";
            if (static_cast<std::size_t>(it.id) != i) {
                out.push_back(named + "is not at its own ordinal");
            }
            if (it.atlasTile >= 0 && static_cast<std::size_t>(it.atlasTile) >= kTiles) {
                out.push_back(named + "uses atlas tile " + std::to_string(it.atlasTile) +
                              ", which is outside the " + std::to_string(kTiles) +
                              "-tile sheet");
            }
            // A placeable that places nothing is a click that does nothing, and
            // iconTile() would borrow Air's tile for its icon.
            if (it.placeable && it.placesBlock == BlockId::Air) {
                out.push_back(named + "is placeable but places nothing");
            }
        }

        // One row per machine, and a manual twin must delegate exactly one hop
        // to a machine that runs its own list -- recipeGroupFor() is a plain
        // lookup and two twins pointing at each other would spin.
        for (std::size_t i = 0; i < machineTraitRows().size(); ++i) {
            const MachineTraits& t = machineTraitRows()[i];
            const std::string named =
                std::string("machine '") + blockName(t.block) + "' ";
            for (std::size_t j = i + 1; j < machineTraitRows().size(); ++j) {
                if (machineTraitRows()[j].block == t.block) {
                    out.push_back(named + "has more than one traits row");
                }
            }
            // Cranked implies delegating, but NOT the reverse: the Bloomery
            // borrows the Furnace's recipes and is driven by its fire rather
            // than by an arm. A handle with no recipe list, though, is a handle
            // attached to nothing.
            if (t.handCranked && t.recipeGroup == BlockId::Air) {
                out.push_back(named + "is hand-cranked but has no recipe group "
                                      "to crank (a handle needs a job)");
            }
            if (t.recipeGroup == BlockId::Air) continue;
            if (!hasMachineTraits(t.recipeGroup)) {
                out.push_back(named + "delegates its recipes to something that is "
                                      "not a machine");
            } else if (machineTraits(t.recipeGroup).recipeGroup != BlockId::Air) {
                out.push_back(named + "delegates to a machine that itself delegates");
            }
        }

        for (const FuelInfo& f : fuelRows()) {
            if (f.item == ItemId::None) out.push_back("a fuel row names no item");
            else if (f.seconds <= 0.0f) {
                out.push_back(std::string("fuel '") + itemName(f.item) +
                              "' burns for no time at all");
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
        std::vector<bool> have(itemCount(), false);
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
        for (int b = 1; b < static_cast<int>(blockCount()); ++b) {
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
                for (const MachineTraits& mt : machineTraitRows()) {
                    if (recipeGroupFor(mt.block) != r.machine) continue;
                    if (known(blockDrop(mt.block).id)) { ok = true; break; }
                }
                for (const ItemStack& in : r.inputs) ok = ok && known(in.id);
                if (!ok) continue;
                for (const RecipeOutput& o : r.outputs) changed |= gain(o.stack.id);
            }
        }

        // Every machine must be buildable, and every recipe input obtainable.
        for (const MachineTraits& traits : machineTraitRows()) {
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
        checkRegistries(out);
        checkRecipeKeys(out);
        checkCircleShadowing(out);
        checkReachability(out);
        return out;
    }

} // namespace content
