#pragma once

#include "game/Block.h"
#include "game/Inventory.h"

#include <glm/glm.hpp>

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

    // Miner only, transient (not saved; re-acquired after load): the node
    // being drilled, so the reach isn't re-scanned every tick.
    glm::ivec3 target{0};
    bool       hasTarget = false;
    int        rescanCooldown = 0; // ticks until the next idle scan
};
