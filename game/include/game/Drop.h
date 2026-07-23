#pragma once

#include "game/Item.h"
#include "game/Dimension.h"

#include <glm/glm.hpp>

// A physical item lying in (or falling through) the world: what mining a block
// spawns, and what a non-void death scatters. The player auto-collects one by
// walking near it. Kept deliberately small — a flat std::vector of these is the
// whole registry (DropSystem.h), iterated in place with no per-frame allocation.
struct DroppedItem {
    glm::vec3   pos{0.0f};             // world-space center
    glm::vec3   vel{0.0f};             // toss + fall; zero once settled
    ItemId      id = ItemId::None;
    int         count = 0;
    float       pickupDelay = 0.0f;    // seconds before it can be collected
    DimensionId dim = DimensionId::Overworld;
    bool        settled = false;       // resting on ground: physics skips it (perf)
};
