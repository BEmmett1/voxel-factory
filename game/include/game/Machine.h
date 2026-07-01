#pragma once

#include "game/Block.h"
#include "game/Inventory.h"

// Runtime state for a placed machine block. Items wait in `input`, finished
// goods collect in `output`, and `progress` counts seconds into the current
// craft. The input/output buffers are belt-ready (Step 5 will fill/drain them).
struct Machine {
    BlockId   type = BlockId::Air;
    Inventory input;
    Inventory output;
    float     progress = 0.0f;   // seconds into the active recipe
    bool      crafting = false;  // had a valid powered recipe this tick
    float     craftTime = 1.0f;  // seconds of the active recipe (for the bar)
    int       selectedRecipe = -1; // index into this type's recipe list; -1 = auto
};
