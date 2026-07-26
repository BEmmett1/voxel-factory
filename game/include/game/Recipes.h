#pragma once

#include "game/Item.h"

#include <vector>

// A crafting recipe: consume `inputs`, produce `output`.
struct Recipe {
    std::vector<ItemStack> inputs;
    ItemStack              output;
};

// Hand-craft recipes available in the crafting menu (the equipment track).
// Ordered, so the menu's selection index is stable.
const std::vector<Recipe>& handcraftRecipes();

// A recipe a machine performs over time when powered and supplied.
struct MachineRecipe {
    BlockId                machine;
    std::vector<ItemStack> inputs;
    ItemStack              output;
    float                  seconds; // processing time
};

// The reagent-processing chain (grinder/cauldron/infuser/alembic/...).
const std::vector<MachineRecipe>& machineRecipes();

// The recipes a given machine type can run, in stable order (the order the
// machine panel lists them and Machine::selectedRecipe indexes them).
std::vector<const MachineRecipe*> recipesForMachine(BlockId type);

// An Alchemy Circle pattern: a ring NECKLACE plus an optional catalyst in the
// Rune Core's own buffer. `ring` holds 4 entries (the cardinal pedestals, a
// Lesser circle can run it) or 8 (the full ring, Greater only), listed
// clockwise; {None, 0} is a slot that must be EMPTY. Slots match on
// "holds at least this many", so belt-fed pedestals keep a circle running.
//
// Matching is rotation-invariant, which is what keeps same-ingredient recipes
// distinct by ARRANGEMENT (two plates on one pedestal vs. one on each of two)
// without ever punishing which way the player faced when they built it.
//
// APPEND-ONLY, like every other recipe table: Machine::selectedRecipe is a
// saved index into this order.
struct CircleRecipe {
    ItemStack              center{};  // catalyst in the core; {None, 0} = none
    std::vector<ItemStack> ring;      // 4 or 8 slots, clockwise
    ItemStack              output;
    float                  seconds;
};

// Every Alchemy Circle pattern, in stable (saved-index) order.
const std::vector<CircleRecipe>& circleRecipes();
