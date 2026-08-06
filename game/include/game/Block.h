#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

// The set of block types. Air is the empty block.
//  - Terrain:    Grass / Dirt / Stone
//  - Equipment:  Generator / Wire / Belt (conduit) / Grinder / Cauldron /
//                Infuser / Alembic / Miner  (placed from inventory items)
//  - Resource nodes (mined for raw items): HerbBush / CrystalNode / CopperOre /
//                SandNode / Spring / EssenceVent
//  - Sources:    glowing blocks that grow a patch of their resource's nodes
//                nearby over time; mining one drops its (re-placeable) item
//
// Ordinals are an ENCODING, not an identity. Each row carries a stable `key`
// and a save writes its own key table alongside the world (save v22), so a
// block that moves in this enum is translated on load rather than misread --
// which is what retired the old APPEND-ONLY rule here. Reordering or removing
// an entry is now a content decision, not a save-corruption hazard.
//
// Two things still hold. The kBlocks registry in Block.cpp is static_asserted
// against this order, so a missing or misplaced row is a compile error; and
// keys must stay unique and stable, because they are what the translation is
// built from (see ContentRegistry.h). Renaming a key IS the breaking change
// the ordinal used to be.
//
// uint16_t rather than uint8_t: the ceiling stopped being theoretical once
// content can come from outside this enum.
enum class BlockId : std::uint16_t {
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
    // ---- The recipe overhaul ------------------------------------------
    // Four powered machines. The Furnace burns FUEL rather than drawing
    // power (heat is its own resource); the other three are ordinary
    // Processors. Between them they own what used to be four free clicks in
    // the hand-craft menu: smelting, glass, vials, and dirt+sand -> stone.
    Furnace,
    Sifter,
    Glassblower,
    Compactor,
    // The MANUAL tier: one hand-cranked twin per Processor. Every one of
    // these is pure data -- a kBlocks row, a kMachineTraits row pointing at
    // its powered counterpart's recipes, and a build recipe. No new code,
    // no new MachineKind. They ask for no power and run at
    // MachineTraits::speedMult, which is the entire cost of playing before
    // you have electricity.
    Bloomery,       // Furnace
    Sieve,          // Sifter
    Blowpipe,       // Glassblower
    Tamper,         // Compactor
    Mortar,         // Grinder
    HandPress,      // Press
    Anvil,          // Forge
    CompostHeap,    // Composter
    MixingBowl,     // Cauldron
    InfusionStand,  // Infuser
    Still,          // Alembic
    HandDistiller,  // Distiller
    HandTransmuter, // Transmuter
    // Bulk storage. The answer to a machine whose output has filled up, and
    // -- with belt filters -- the sorter, since every belt pointing away from
    // one drains it independently.
    StorageCrate,
    // Farming: worked ground a crop can be planted on. Laying a field out is
    // a deliberate build step (a Copper Hoe RMB'd at Grass/Dirt), not a side
    // effect of walking around.
    TilledSoil,
    // The crop's four growth stages. Each visible stage costs a row because the
    // mesher picks a shape from the BlockId alone and Chunk is a flat BlockId
    // array with no per-cell metadata -- the timer can live in a side registry,
    // the LOOK cannot. Stage 3 is ripe; that is the only one the Harvester takes
    // and the only one that yields Herb.
    HerbCrop0,
    HerbCrop1,
    HerbCrop2,
    HerbCrop3,
    // Reaps ripe crops in reach and replants the cell, so a field runs itself.
    Harvester,
    // Spends Rain Water to keep the crops around it growing at the rain rate.
    Irrigator,
    Count
};

// Defined in Item.h; only the drop field below needs the type. The underlying
// type must match Item.h's exactly (the compiler enforces it).
enum class ItemId : std::uint16_t;
// Defined in Item.h; a fixed underlying type lets it be a BlockInfo member here.
enum class ToolType : std::uint8_t;
// Defined in BlockShape.h (which includes THIS header, so it can only be
// forward-declared); same fixed-underlying-type trick as ToolType.
enum class ShapeId : std::uint8_t;

// What a block offers underfoot, and what a plant demands of the cell below it.
// ORDERED, and the check is one comparison (`provides >= needsSoil`), so Tilled
// Soil satisfies a sapling for free without anything having to say so: tilled
// ground is still ground. This replaced a hardcoded
// `if (id == BlockId::Sapling)` in WorldEdit, which crops would have had to
// grow a second branch of -- and which a content pack could never have reached.
enum class SoilKind : std::uint8_t {
    None = 0, // not plantable at all
    Soil,     // Grass or Dirt: a sapling takes root here
    Tilled,   // worked ground: what crops need, and laying it out is a build step
};

// Spellings for the content pack format -- see kToolNames in Item.h. Index
// matches the enum.
inline constexpr const char* kSoilNames[] = {"none", "soil", "tilled"};

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
    // Stable identity, independent of both the ordinal and the display name.
    // The ordinal is a save/wire encoding that shifts the moment content is
    // added or removed around it; `name` is UI text that may be reworded (this
    // registry already displays SpringWater as "Rain Water"). The key is what
    // survives both, so it is what a save's id table and a future multiplayer
    // join handshake actually agree on. Unique within kBlocks (static_asserted).
    // Namespaced: "core:" here, "<mod>:" for content loaded from a mod.
    const char* key = "";
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
    // Farming's two halves of the same question. `provides` is what standing on
    // this block offers a plant; `needsSoil` is what this block demands of the
    // cell beneath it when placed. Both default to None, so an ordinary block
    // neither grows things nor cares what it sits on.
    SoilKind    provides = SoilKind::None;
    SoilKind    needsSoil = SoilKind::None;
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

// ---- The registry is a RUNTIME table ---------------------------------------
// kBlocks in Block.cpp is the SEED, not the registry: it is copied into a
// vector at first use, and content loaded from a pack is appended past it. So
// `BlockId::Count` no longer means "how many blocks there are" -- it means how
// many were COMPILED IN, which is a different and much narrower claim.
//
// Anything iterating all content wants blockCount(); anything sizing an array
// by content wants it too. `BlockId::Count` survives as the boundary between
// compiled and loaded content, and as the "no such block" sentinel that
// content::blockFromKey() returns.
//
// An ordinal past BlockId::Count is still a perfectly good BlockId: the enum
// has a fixed underlying type, so every value in its range is valid, which is
// what lets a loaded block ride every path a compiled one does -- including
// the raw bytes of a chunk and a save's id table.
std::size_t blockCount();

// Every row, compiled and loaded. Iterating this is the same as walking
// 0..blockCount(), and is what the validators and the content dump use.
const std::vector<BlockInfo>& blockRows();

// Append a row loaded from a content pack, and return its new ordinal. The
// caller owns proving the row is coherent (content::validate()); this only
// promises the id it hands back is the row's position.
//
// STARTUP ONLY, and for the same reason the recipe tables are: existing
// BlockIds must not move, and nothing may already be holding a BlockInfo& --
// growing the vector invalidates every one of them.
BlockId addBlock(const BlockInfo& row);

// Put the whole table back. A pack has to be APPLIED before anyone can ask
// whether the content set it produces is coherent, so a refusal needs a way
// back -- see content::applyPacks().
void restoreBlocks(std::vector<BlockInfo> rows);

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

// Can `id` be placed on top of `under`? One ordered comparison: a block that
// needs nothing goes anywhere, a sapling needs Soil or better, a crop needs
// Tilled exactly.
inline bool soilAccepts(BlockId under, BlockId id) {
    return blockInfo(under).provides >= blockInfo(id).needsSoil;
}
