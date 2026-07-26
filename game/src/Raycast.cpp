#include "game/Raycast.h"

#include "game/World.h"
#include "game/Block.h"
#include "game/BlockShape.h"
#include "game/Collision.h"

#include <cmath>
#include <limits>

RaycastHit raycastVoxel(const World& world, const glm::vec3& origin,
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
        // Entering the cell only makes it a CANDIDATE. Test the ray against
        // the block's actual boxes and take the nearest entry; a miss falls
        // through to the step below and the ray carries on, which is what
        // lets it thread the gaps in a sub-cube shape.
        const auto boxes = blockBoxes(world.getBlock(x, y, z));
        if (!boxes.empty()) {
            const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(z));
            float      bestT = inf;
            glm::ivec3 bestNormal{0};
            for (const ShapeAabb& b : boxes) {
                float      bt = 0.0f;
                glm::ivec3 bn{0};
                if (Collision::rayAabb(origin, d, cell + b.lo, cell + b.hi, bt, bn) &&
                    bt <= maxDistance && bt < bestT) {
                    bestT = bt;
                    bestNormal = bn;
                }
            }
            if (bestT < inf) {
                result.hit = true;
                result.block = {x, y, z};
                // A ray starting inside the box has no entry face; fall back
                // to the cell face the traversal came through.
                result.normal = bestNormal != glm::ivec3(0) ? bestNormal : normal;
                result.t = bestT;
                result.point = origin + d * bestT;
                return result;
            }
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
