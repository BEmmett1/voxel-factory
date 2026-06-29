#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <functional>

// Hash for glm::ivec3 so it can key unordered_map / unordered_set
// (chunk coordinates, energized-cell sets, raycast visited sets, ...).
struct IVec3Hash {
    std::size_t operator()(const glm::ivec3& v) const noexcept {
        std::size_t h = std::hash<int>()(v.x);
        h ^= std::hash<int>()(v.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(v.z) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};
