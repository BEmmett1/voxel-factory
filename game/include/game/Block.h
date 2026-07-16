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
//
// APPEND-ONLY: the ordinal is the on-disk save encoding (SaveSystem writes raw
// block bytes), so new blocks go immediately before Count and existing entries
// never move. The kBlocks registry in Block.cpp is static_asserted against
// this order — a missing or misplaced row is a compile error.
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
    EssenceVent,
    // Resource sources (patch spawners)
    SourceHerb,
    SourceCrystal,
    SourceCopper,
    SourceSand,
    SourceEssence,
    // Cheap structural block for building up/across (no flight!)
    Scaffold,
    // Forestry: a planted sapling grows into a log trunk + leaf canopy
    Sapling,
    Log,
    Leaves,
    // Collects rain water into its output buffer (there are no springs --
    // rain is the island's only water)
    RainBarrel,
    Count
};

// Defined in Item.h; only the drop field below needs the type.
enum class ItemId : std::uint8_t;

// What mining a block yields ({None, 0} = nothing).
struct BlockDrop {
    ItemId item  = ItemId{}; // ItemId::None
    int    count = 0;
};

// A block's atlas tiles by face; the four side faces share one tile.
// (Atlas geometry — grid size, UV math — lives in Atlas.h.)
struct BlockTiles {
    int top    = 0;
    int side   = 0;
    int bottom = 0;
};

// Everything static about a block type, one registry row per BlockId.
struct BlockInfo {
    BlockId     id;                 // must equal the row's position (static_asserted)
    const char* name = "?";         // human-readable, e.g. for UI / window title
    bool        solid = true;       // does it occlude neighbors / get meshed?
    glm::vec3   color {0.0f};       // flat base color (pre-lighting)
    float       emissive = 0.0f;    // constant self-illumination (sources glow)
    bool        machine = false;    // processing machine (has a Machine entity)
    bool        source = false;     // resource source (patch spawner)
    bool        node = false;       // harvestable resource node (what Miners collect)
    BlockId     spawnsNode = BlockId::Air; // the node a source grows (sources only)
    BlockDrop   drop {};            // what mining it yields
    BlockTiles  tiles {};           // atlas tiles per face
};

// Static properties for a block type.
const BlockInfo& blockInfo(BlockId id);

inline const char* blockName(BlockId id)   { return blockInfo(id).name; }
inline bool isSolid(BlockId id)            { return blockInfo(id).solid; }
inline bool isMachine(BlockId id)          { return blockInfo(id).machine; }
inline bool isSource(BlockId id)           { return blockInfo(id).source; }
inline bool isResourceNode(BlockId id)     { return blockInfo(id).node; }
inline BlockId sourceSpawnsNode(BlockId id){ return blockInfo(id).spawnsNode; }
