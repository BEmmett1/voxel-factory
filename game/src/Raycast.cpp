#include "game/Raycast.h"

#include "game/Chunk.h"
#include "game/Block.h"

#include <cmath>
#include <limits>

RaycastHit raycastVoxel(const Chunk& chunk, const glm::vec3& origin,
                        const glm::vec3& dir, float maxDistance) {
    RaycastHit result;

    glm::vec3 d = dir;
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len <= 0.0f) return result;
    d /= len;

    // Current voxel containing the ray origin.
    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = d.x > 0 ? 1 : (d.x < 0 ? -1 : 0);
    const int stepY = d.y > 0 ? 1 : (d.y < 0 ? -1 : 0);
    const int stepZ = d.z > 0 ? 1 : (d.z < 0 ? -1 : 0);

    const float inf = std::numeric_limits<float>::infinity();

    // Distance along the ray to the next voxel boundary on each axis.
    auto firstBoundary = [](float o, int cell, int step, float dir) -> float {
        if (step == 0) return std::numeric_limits<float>::infinity();
        const float next = (step > 0) ? static_cast<float>(cell + 1) : static_cast<float>(cell);
        return (next - o) / dir;
    };

    float tMaxX = firstBoundary(origin.x, x, stepX, d.x);
    float tMaxY = firstBoundary(origin.y, y, stepY, d.y);
    float tMaxZ = firstBoundary(origin.z, z, stepZ, d.z);

    const float tDeltaX = stepX != 0 ? std::abs(1.0f / d.x) : inf;
    const float tDeltaY = stepY != 0 ? std::abs(1.0f / d.y) : inf;
    const float tDeltaZ = stepZ != 0 ? std::abs(1.0f / d.z) : inf;

    glm::ivec3 normal{0};
    float t = 0.0f;

    while (t <= maxDistance) {
        if (chunk.inBounds(x, y, z) && isSolid(chunk.get(x, y, z))) {
            result.hit = true;
            result.block = {x, y, z};
            result.normal = normal;
            return result;
        }

        // Advance into the next voxel along the nearest boundary.
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            normal = {-stepX, 0, 0};
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            normal = {0, -stepY, 0};
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            normal = {0, 0, -stepZ};
        }
    }

    return result;
}
