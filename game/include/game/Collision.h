#pragma once

#include <glm/glm.hpp>

class World;

// Shape-aware geometry queries against the world: free functions over
// (World&, ...), the MachineSystem/WorldEdit precedent.
//
// Every query walks a cell's blockBoxes() (BlockShape.h) rather than assuming
// the unit cube each block used to be. For a full cube that is one box spanning
// the whole cell, so these are exact replacements for the floor()-based tests
// they took over from.
//
// NOTE: all of them iterate the cells an AABB overlaps and test only THAT
// cell's boxes, which assumes shape geometry stays inside its own cell (0..1).
// The bake enforces that; a model hanging into its neighbour would be missed
// here, not silently half-handled.
namespace Collision {

    // Ray vs AABB slab test. On hit, tOut is the entry distance (0 when the
    // origin is already inside).
    bool rayAabb(const glm::vec3& origin, const glm::vec3& dir,
                 const glm::vec3& lo, const glm::vec3& hi, float& tOut);

    // As above, and normalOut is the outward normal of the face entered
    // through (zero when the origin starts inside the box).
    bool rayAabb(const glm::vec3& origin, const glm::vec3& dir,
                 const glm::vec3& lo, const glm::vec3& hi, float& tOut,
                 glm::ivec3& normalOut);

    // Does the AABB overlap any block geometry? Touching is not overlapping,
    // so a box resting exactly on a surface is clear.
    bool boxOverlapsWorld(const World& world, const glm::vec3& lo,
                          const glm::vec3& hi);

    // The highest geometry surface in one cell, or -infinity if the cell holds
    // nothing to stand on. Cheap enough to call per falling entity per tick.
    float surfaceTopAt(const World& world, int x, int y, int z);

    // The highest surface a falling box with this horizontal footprint can
    // land on, considering only surfaces at or below yHigh. Returns
    // -infinity when there is nothing under it.
    //
    // This is what replaces `floor(y) + 1`: with sub-cube shapes the thing you
    // land on is a box top, which is rarely a cell boundary.
    float landingSurface(const World& world, float x0, float x1,
                         float z0, float z1, float yLow, float yHigh);

} // namespace Collision
