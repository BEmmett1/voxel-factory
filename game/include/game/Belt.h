#pragma once

#include "game/Item.h"

#include <glm/glm.hpp>

// A conduit segment. Carries at most one item, which advances in `facing`
// each belt step. (Slot-based multi-item belts are a future refinement.)
struct Belt {
    glm::ivec3 facing{0, 0, 1}; // unit cardinal direction items travel
    ItemId     item = ItemId::None;
};
