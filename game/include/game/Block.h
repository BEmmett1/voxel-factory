#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// The set of block types. Air is the empty block.
//  - Terrain:    Grass / Dirt / Stone
//  - Equipment:  Generator / Wire / Belt (conduit) / Grinder / Cauldron /
//                Infuser / Alembic / Miner  (placed from inventory items)
//  - Resource nodes (mined for raw items): HerbBush / CrystalNode / CopperOre /
//                SandNode / WaterSource / EssenceVent
enum class BlockId : std::uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    // Equipment / machines
    Generator,
    Wire,
    Belt,
    Grinder,
    Cauldron,
    Infuser,
    Alembic,
    Miner,
    // Resource nodes
    HerbBush,
    CrystalNode,
    CopperOre,
    SandNode,
    WaterSource,
    EssenceVent,
    Count
};

struct BlockInfo {
    bool      solid;  // does it occlude neighbors / get meshed?
    glm::vec3 color;  // flat base color (pre-lighting)
};

// Static properties for a block type.
const BlockInfo& blockInfo(BlockId id);

// Human-readable name, e.g. for UI / window title.
const char* blockName(BlockId id);

inline bool isSolid(BlockId id) {
    return blockInfo(id).solid;
}

// A processing machine (grinder/cauldron/infuser/alembic/miner).
bool isMachine(BlockId id);
