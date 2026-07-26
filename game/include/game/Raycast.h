#pragma once

#include <glm/glm.hpp>

class World;

// Result of marching a ray through the voxel grid.
struct RaycastHit {
    bool       hit = false;
    glm::ivec3 block{0};   // the solid block the ray struck
    glm::ivec3 normal{0};  // face it entered through; block + normal is the
                           // empty cell to place a new block into
    float      t = 0.0f;   // distance along the (normalized) ray to the hit
    glm::vec3  point{0.0f}; // origin + t * dir, so callers stop re-deriving it
};

// March a ray (origin + t*dir) through the world and return the first block
// GEOMETRY it strikes within maxDistance, using the Amanatides & Woo voxel
// traversal. Entering a cell is not a hit: the ray is tested against that
// cell's blockBoxes(), and a miss keeps stepping, so a ray can pass through
// the gaps in a sub-cube shape. `normal` is the face of the BOX that was hit,
// which for a full cube is the face the ray entered the cell through.
RaycastHit raycastVoxel(const World& world, const glm::vec3& origin,
                        const glm::vec3& dir, float maxDistance);
