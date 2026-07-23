#pragma once

#include "game/Drop.h"

#include <glm/glm.hpp>
#include <vector>

class World;

// The dropped-item simulation: free functions over a flat drop list + the
// world (the MachineSystem / WorldEdit precedent). VoxelGame owns the vector
// and the glue (spawning from mining, pickup into the inventory, rendering,
// saving); this module owns spawn-time merging and the fall/settle physics.
namespace DropSystem {

    // Add `count` of `id` at `pos`. Merges into an existing nearby drop of the
    // same id/dimension instead of appending (bounds the entity count under
    // repeated mining); enforces a hard cap by evicting the oldest settled
    // drop. `pickupDelay` holds off collection (a death toss gives the player a
    // moment to move before re-grabbing the scattered pack).
    void spawn(std::vector<DroppedItem>& drops, const glm::vec3& pos, ItemId id,
               int count, DimensionId dim, float pickupDelay = 0.0f);

    // Advance the drops in `dim` by `dt`: gravity + settle onto the first solid
    // block below, and tick down pickup delays. Settled drops are skipped
    // entirely, so a field of resting items costs almost nothing.
    void tick(std::vector<DroppedItem>& drops, const World& world, float dt,
              DimensionId dim);

} // namespace DropSystem
