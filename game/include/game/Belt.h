#pragma once

#include "game/Item.h"

#include <glm/glm.hpp>

// A conduit segment. Carries at most one item, which advances in `facing`
// each belt step. (Slot-based multi-item belts are a future refinement.)
struct Belt {
    glm::ivec3 facing{0, 0, 1}; // unit cardinal direction items travel
    ItemId     item = ItemId::None;
    // What this belt is willing to carry; None = anything.
    //
    // This retired beltStep's old rule for draining a mixed machine output,
    // which was "whichever item has the lowest ItemId ordinal" -- arbitrary,
    // invisible, and impossible to teach. A filter makes the choice the
    // PLAYER's and puts it on the block where it can be seen.
    //
    // It binds in both directions: a filtered belt pulls only its item out of
    // a machine, and refuses to accept anything else from the belt behind. The
    // second half is what makes a sorting LANE possible rather than just a
    // sorting tap -- and what can stall the line behind it, which is the
    // intended, visible failure (the stuck item and the filter icon are both
    // drawn).
    ItemId     filter = ItemId::None;
};
