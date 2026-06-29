#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// The set of block types. Air is the empty block. The automation blocks
// (Generator/Wire/Machine/Belt) render as flat-colored markers for now; their
// behavior arrives in later milestones.
enum class BlockId : std::uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Generator,
    Wire,
    Machine,
    Belt,
    Count
};

struct BlockInfo {
    bool      solid;  // does it occlude neighbors / get meshed?
    glm::vec3 color;  // flat base color (pre-lighting)
};

// Static properties for a block type.
const BlockInfo& blockInfo(BlockId id);

inline bool isSolid(BlockId id) {
    return blockInfo(id).solid;
}
