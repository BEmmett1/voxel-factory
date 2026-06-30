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

// The reagent-processing chain (grinder/cauldron/infuser/alembic).
const std::vector<MachineRecipe>& machineRecipes();
