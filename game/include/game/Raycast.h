#pragma once

#include <glm/glm.hpp>

class World;

// Result of marching a ray through the voxel grid.
struct RaycastHit {
    bool       hit = false;
    glm::ivec3 block{0};   // the solid block the ray struck
    glm::ivec3 normal{0};  // face it entered through; block + normal is the
                           // empty cell to place a new block into
};

// March a ray (origin + t*dir) through the world and return the first solid
// block within maxDistance, using the Amanatides & Woo voxel traversal.
RaycastHit raycastVoxel(const World& world, const glm::vec3& origin,
                        const glm::vec3& dir, float maxDistance);
