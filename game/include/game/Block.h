#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// The set of block types. Air is the empty block.
//  - Terrain:    Grass / Dirt / Stone
//  - Equipment:  Generator / Wire / Belt (conduit) / Grinder / Cauldron /
//                Infuser / Alembic / Miner  (placed from inventory items)
//  - Resource nodes (mined for raw items): HerbBush / CrystalNode / CopperOre /
//                SandNode / Spring / EssenceVent
//  - Sources:    glowing blocks that grow a patch of their resource's nodes
//                nearby over time; mining one drops its (re-placeable) item
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
    Distiller,
    Transmuter,
    Miner,
    // Resource nodes
    HerbBush,
    CrystalNode,
    CopperOre,
    SandNode,
    Spring,
    EssenceVent,
    // Resource sources (patch spawners)
    SourceHerb,
    SourceCrystal,
    SourceCopper,
    SourceSand,
    SourceWater,
    SourceEssence,
    // Cheap structural block for building up/across (no flight!)
    Scaffold,
    // Forestry: a planted sapling grows into a log trunk + leaf canopy
    Sapling,
    Log,
    Leaves,
    Count
};

struct BlockInfo {
    bool      solid;    // does it occlude neighbors / get meshed?
    glm::vec3 color;    // flat base color (pre-lighting)
    float     emissive; // constant self-illumination (sources glow)
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

// A resource source (patch spawner).
bool isSource(BlockId id);

// A harvestable resource node (grown by a source; what Miners collect).
bool isResourceNode(BlockId id);

// The node block a source grows (Air if `id` is not a source).
BlockId sourceSpawnsNode(BlockId id);
