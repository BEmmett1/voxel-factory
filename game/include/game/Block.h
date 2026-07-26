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
    // The boss arena's ground (BossArena dimension). Drops nothing -- the
    // arena is transient and not a quarry.
    VoidStone,
    // Source fusion: two adjacent different sources fuse (via a Fusion
    // Catalyst) into a Resonant Source that grows Resonant Nodes.
    ResonantNode,
    ResonantSource,
    // Composter machine: composts plant matter into renewable Dirt.
    Composter,
    // Forge machine: block-crafts weapons/armor from the plate/frame chain.
    Forge,
    // Press machine: forms the shared parts tier (rods, gears, casings,
    // etched plates) and assembles them into the Machine Frame every other
    // machine is built from.
    Press,
    // The Alchemy Circle multiblock: a Rune Core reading the Pedestals on the
    // eight ring cells at radius 2. Crafting that used to be a free menu click
    // now occupies factory floor and can be belt-fed. See AlchemyCircle.h.
    RuneCore,
    Pedestal,
    Count
};

// Defined in Item.h; only the drop field below needs the type.
enum class ItemId : std::uint8_t;
// Defined in Item.h; a fixed underlying type lets it be a BlockInfo member here.
enum class ToolType : std::uint8_t;
// Defined in BlockShape.h (which includes THIS header, so it can only be
// forward-declared); same fixed-underlying-type trick as ToolType.
enum class ShapeId : std::uint8_t;

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
    // `solid` and `fullCube` were one flag until sub-cube block shapes needed
    // them apart: a tube or a slab still stops rays and blocks movement while
    // occluding nothing. `solid` = participates in physics and raycasts (and
    // gets meshed); `fullCube` = fills its cell, which is what lets the mesher
    // drop a hidden face and a roof keep the rain out. fullCube implies solid
    // (static_asserted in Block.cpp). Every block is a full cube today.
    bool        solid = true;
    bool        fullCube = true;
    glm::vec3   color {0.0f};       // flat base color (pre-lighting)
    float       emissive = 0.0f;    // constant self-illumination (sources glow)
    bool        machine = false;    // processing machine (has a Machine entity)
    bool        source = false;     // resource source (patch spawner)
    bool        node = false;       // harvestable resource node (what Miners collect)
    BlockId     spawnsNode = BlockId::Air; // the node a source grows (sources only)
    BlockDrop   drop {};            // what mining it yields
    BlockTiles  tiles {};           // atlas tiles per face
    // Timed breaking + tool gating. `hardness` is the seconds to break BY HAND
    // (0 => effectively instant); a matching tool divides that by its speed.
    // `tool` is the class that breaks it efficiently; `toolTier` is the minimum
    // tool tier for the drop (0 = ungated soft block: always drops, tool just
    // faster; >0 = gated: no drop without the right class at that tier).
    float       hardness = 0.0f;
    ToolType    tool = ToolType{};  // ToolType::None (0)
    int         toolTier = 0;
    // Which sub-cube geometry the block occupies (BlockShape.h). Default is
    // ShapeId::FullCube — the implicit unit cube every block was before shapes
    // existed. Presentation only: never saved, so ShapeId may be reordered.
    // Declared LAST so a row can append `.shape = ...` without having to slot
    // it ahead of the fields it already sets (designators must appear in
    // member order).
    ShapeId     shape = ShapeId{};  // ShapeId::FullCube (0)
};

// Static properties for a block type.
const BlockInfo& blockInfo(BlockId id);

inline const char* blockName(BlockId id)   { return blockInfo(id).name; }
inline bool isSolid(BlockId id)            { return blockInfo(id).solid; }
inline bool isFullCube(BlockId id)         { return blockInfo(id).fullCube; }
inline ShapeId blockShapeId(BlockId id)    { return blockInfo(id).shape; }
inline bool isMachine(BlockId id)          { return blockInfo(id).machine; }
inline bool isSource(BlockId id)           { return blockInfo(id).source; }
inline bool isResourceNode(BlockId id)     { return blockInfo(id).node; }
inline BlockId sourceSpawnsNode(BlockId id){ return blockInfo(id).spawnsNode; }
inline float blockHardness(BlockId id)     { return blockInfo(id).hardness; }
inline ToolType blockTool(BlockId id)      { return blockInfo(id).tool; }
inline int  blockToolTier(BlockId id)      { return blockInfo(id).toolTier; }
