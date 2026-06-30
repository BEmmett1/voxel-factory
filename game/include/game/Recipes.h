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
