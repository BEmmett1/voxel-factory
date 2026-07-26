#include "game/Collision.h"

#include "game/BlockShape.h"
#include "game/World.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

    constexpr float kInf = std::numeric_limits<float>::infinity();

    int cellOf(float v) { return static_cast<int>(std::floor(v)); }

} // namespace

namespace Collision {

    bool rayAabb(const glm::vec3& origin, const glm::vec3& dir,
                 const glm::vec3& lo, const glm::vec3& hi, float& tOut,
                 glm::ivec3& normalOut) {
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();
        int   axis = -1;
        int   sign = 0;

        for (int i = 0; i < 3; ++i) {
            if (std::abs(dir[i]) < 1e-8f) {
                // Parallel to this slab: either inside it for the whole ray or
                // never.
                if (origin[i] < lo[i] || origin[i] > hi[i]) return false;
                continue;
            }
            float t0 = (lo[i] - origin[i]) / dir[i];
            float t1 = (hi[i] - origin[i]) / dir[i];
            // Whichever plane is crossed first is the face entered through;
            // its outward normal points back along this axis.
            int faceSign = -1;
            if (t0 > t1) {
                std::swap(t0, t1);
                faceSign = +1;
            }
            if (t0 > tMin) {
                tMin = t0;
                axis = i;
                sign = faceSign;
            }
            tMax = std::min(tMax, t1);
            if (tMin > tMax) return false;
        }

        tOut = tMin;
        normalOut = glm::ivec3(0);
        if (axis >= 0) normalOut[axis] = sign;
        return true;
    }

    bool rayAabb(const glm::vec3& origin, const glm::vec3& dir,
                 const glm::vec3& lo, const glm::vec3& hi, float& tOut) {
        glm::ivec3 ignored{0};
        return rayAabb(origin, dir, lo, hi, tOut, ignored);
    }

    bool boxOverlapsWorld(const World& world, const glm::vec3& lo,
                          const glm::vec3& hi) {
        const int x0 = cellOf(lo.x), x1 = cellOf(hi.x);
        const int y0 = cellOf(lo.y), y1 = cellOf(hi.y);
        const int z0 = cellOf(lo.z), z1 = cellOf(hi.z);

        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(z));
                    for (const ShapeAabb& b : blockBoxes(world.getBlock(x, y, z))) {
                        const glm::vec3 blo = cell + b.lo;
                        const glm::vec3 bhi = cell + b.hi;
                        // Strict: touching a surface is not overlapping it, so
                        // standing exactly on a block top stays clear.
                        if (lo.x < bhi.x && hi.x > blo.x &&
                            lo.y < bhi.y && hi.y > blo.y &&
                            lo.z < bhi.z && hi.z > blo.z) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    float surfaceTopAt(const World& world, int x, int y, int z) {
        float top = -kInf;
        for (const ShapeAabb& b : blockBoxes(world.getBlock(x, y, z))) {
            top = std::max(top, static_cast<float>(y) + b.hi.y);
        }
        return top;
    }

    float landingSurface(const World& world, float x0, float x1,
                         float z0, float z1, float yLow, float yHigh) {
        const int cx0 = cellOf(x0), cx1 = cellOf(x1);
        const int cz0 = cellOf(z0), cz1 = cellOf(z1);
        // One cell below yLow too: a surface flush with a cell boundary
        // belongs to the cell underneath it.
        const int cy0 = cellOf(yLow) - 1, cy1 = cellOf(yHigh);

        float best = -kInf;
        for (int y = cy0; y <= cy1; ++y) {
            for (int z = cz0; z <= cz1; ++z) {
                for (int x = cx0; x <= cx1; ++x) {
                    for (const ShapeAabb& b : blockBoxes(world.getBlock(x, y, z))) {
                        const float top = static_cast<float>(y) + b.hi.y;
                        if (top <= yHigh + 1e-4f) best = std::max(best, top);
                    }
                }
            }
        }
        return best;
    }

} // namespace Collision
