#pragma once

#include "game/Block.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

// Sub-cube block geometry: the boxes a block actually occupies inside its cell,
// instead of the implicit 1x1x1 every block was until now. A block names a
// ShapeId on its kBlocks row; the shape supplies both what to DRAW (quads, with
// baked atlas UVs) and what to COLLIDE with (boxes), which is why a tube can
// stop a ray while occluding nothing.
//
// Shape data is baked from Blockbench models by tools/bbmodel_to_shape.py into
// generated/BlockShapes.inl. Unlike BlockId and ItemId, ShapeId is NOT a save
// encoding -- shapes are pure presentation, so this enum may be reordered and
// rows may be removed freely.

// One textured face, fully resolved by the bake. Corners are cell-local
// (0..1) and wound COUNTER-CLOCKWISE seen from outside; the mesher adds the
// block's world origin and copies them. Everything that used to be
// reconstructed at runtime -- the face axis mapping, UV mirroring, element
// rotation -- is already folded in here, which is what lets a 45°-rotated
// element (an alembic's spout) exist at all: a rotated face is not
// axis-aligned, so there is nothing a lo/hi pair could describe.
//
// `face` survives only to say which neighbour `cull` consults. `cull` marks a
// face flush with a cell wall -- the only faces the mesher may drop against a
// fullCube neighbour, and never a rotated one.
struct ShapeQuad {
    glm::vec3    pos[4];
    glm::vec2    uv[4];
    glm::vec3    normal {0.0f};
    std::uint8_t face = 0;
    bool         cull = false;
    // Each corner as a vector FROM this quad's part pivot, and which part that
    // is. See ShapePart -- the offset is baked instead of the pivot because a
    // chunk vertex is world-space and cannot recover its own cell.
    glm::vec3    partOff[4] {};
    std::uint8_t part = 0;
};

// One movable group from the Blockbench outliner. Which parts actually move is
// GAMEPLAY policy, not model data, so a part is addressed by NAME and the
// registry of what animates lives in C++ rather than in the bake.
//
// Part 0 is the static root -- every model has one, including the ones with no
// groups at all (the crops), so a quad always names a valid part. `parent` is
// carried so animating a group can later take its subgroups with it without a
// re-bake; nothing reads it yet.
struct ShapePart {
    const char* name = "static";
    int         parent = -1;   // enclosing part, -1 for the root
    glm::vec3   pivot {0.0f};  // cell-local (0..1) point it turns about
};

// A cell-local AABB (0..1), for collision and raycasts. For a ROTATED element
// this is the bounding box of the rotated geometry, so collision is a little
// generous where the drawn shape is exact -- the deliberate split that lets
// rendering carry detail physics does not need.
struct ShapeAabb {
    glm::vec3 lo {0.0f};
    glm::vec3 hi {1.0f};
};

// Minecraft-style animated texture: `frames` bands stacked down the sheet,
// `vStride` apart in UV. Advancing a frame is a uniform, never a remesh --
// VoxelGame::updateShapeAnim() turns these into one v-offset per ShapeId each
// frame and voxel.vert shifts vUv by the one its aAnimBank names. `frameTime`
// is in ticks, like Blockbench writes it, and this game ticks at the same
// 20 Hz. A block whose network is unpowered is meshed with bank 0 instead, so
// it parks on frame 0.
struct ShapeAnim {
    int   frames = 1;
    int   frameTime = 0;
    float vStride = 0.0f;
};

// Shape identity. FullCube is 0 so it stays the BlockInfo default (that field
// is declared with a forward-declared ShapeId, so it cannot name an enumerator
// -- same trick as ToolType).
enum class ShapeId : std::uint8_t {
    FullCube = 0,   // the implicit unit cube: no quads, the mesher's fast path
    Empty,          // occupies nothing (Air): no quads, no collision boxes
    BrewingCauldron,
    AlchemicalAlembic,
    AugerMiningRig,
    ArcaneInfuser,
    // Farming's four growth stages. Crossed planes rather than a detailed
    // model, deliberately: this is the first content placed in BULK, and 4
    // quads (0.8 KB of chunk mesh) against the Infuser's 367 is what keeps a
    // field from costing megabytes.
    HerbCrop0,
    HerbCrop1,
    HerbCrop2,
    HerbCrop3,
    // Connected shapes: the six arms are PARTS, drawn per cell (kConnectParts).
    ConduitHub,
    WireHub,
    // The Alchemy Circle -- the most-looked-at thing in the game, since you lay
    // a pattern by hand and then stand there watching it.
    RuneCore,
    MossyRunePedestal,
    TreeSapling,
    // Nodes and sources: what the island scatters across its outer band,
    // and what the player navigates by.
    HerbBush,
    VioletCrystalCluster,
    DarkStoneVent,
    BicolourCrystalNode,
    MossyShrineStandingStone,
    RunedStandingStone,
    VerdigrisStandingStone,
    SandSource,
    EssenceSource,
    ResonantSource,
    TimberScaffoldFrame,
    Count
};

#include "game/generated/BlockShapes.inl"

// The unit cube's collision box. FullCube carries no quads on purpose: the
// mesher's existing kFaces path draws it, and routing every plain block through
// per-quad geometry would cost the whole world's meshing speed to buy nothing.
inline constexpr ShapeAabb kUnitCubeBoxes[] = {{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};

// Everything about a shape, one row per ShapeId.
struct BlockShape {
    ShapeId                    id;      // must equal the row's position
    std::span<const ShapeQuad> quads;   // empty => draw it as a unit cube
    std::span<const ShapeAabb> boxes;   // empty => nothing to stand on or hit
    ShapeAabb                  bounds;  // union of `boxes`; broad phase + highlight
    ShapeAnim                  anim;
    std::span<const ShapePart> parts;   // empty => the shape has only part 0
};

inline constexpr BlockShape kBlockShapes[] = {
    {.id = ShapeId::FullCube, .boxes = kUnitCubeBoxes},
    {.id = ShapeId::Empty, .bounds = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}},
    {.id = ShapeId::BrewingCauldron,
     .quads = kShapeQuadsBrewingCauldron,
     .boxes = kShapeBoxesBrewingCauldron,
     .bounds = kShapeBoundsBrewingCauldron,
     .anim = kShapeAnimBrewingCauldron,
     .parts = kShapePartsBrewingCauldron},
    {.id = ShapeId::AlchemicalAlembic,
     .quads = kShapeQuadsAlchemicalAlembic,
     .boxes = kShapeBoxesAlchemicalAlembic,
     .bounds = kShapeBoundsAlchemicalAlembic,
     .anim = kShapeAnimAlchemicalAlembic,
     .parts = kShapePartsAlchemicalAlembic},
    {.id = ShapeId::AugerMiningRig,
     .quads = kShapeQuadsAugerMiningRig,
     .boxes = kShapeBoxesAugerMiningRig,
     .bounds = kShapeBoundsAugerMiningRig,
     .anim = kShapeAnimAugerMiningRig,
     .parts = kShapePartsAugerMiningRig},
    {.id = ShapeId::ArcaneInfuser,
     .quads = kShapeQuadsArcaneInfuser,
     .boxes = kShapeBoxesArcaneInfuser,
     .bounds = kShapeBoundsArcaneInfuser,
     .anim = kShapeAnimArcaneInfuser,
     .parts = kShapePartsArcaneInfuser},
    {.id = ShapeId::HerbCrop0,
     .quads = kShapeQuadsHerbCrop0,
     .boxes = kShapeBoxesHerbCrop0,
     .bounds = kShapeBoundsHerbCrop0,
     .anim = kShapeAnimHerbCrop0,
     .parts = kShapePartsHerbCrop0},
    {.id = ShapeId::HerbCrop1,
     .quads = kShapeQuadsHerbCrop1,
     .boxes = kShapeBoxesHerbCrop1,
     .bounds = kShapeBoundsHerbCrop1,
     .anim = kShapeAnimHerbCrop1,
     .parts = kShapePartsHerbCrop1},
    {.id = ShapeId::HerbCrop2,
     .quads = kShapeQuadsHerbCrop2,
     .boxes = kShapeBoxesHerbCrop2,
     .bounds = kShapeBoundsHerbCrop2,
     .anim = kShapeAnimHerbCrop2,
     .parts = kShapePartsHerbCrop2},
    {.id = ShapeId::HerbCrop3,
     .quads = kShapeQuadsHerbCrop3,
     .boxes = kShapeBoxesHerbCrop3,
     .bounds = kShapeBoundsHerbCrop3,
     .anim = kShapeAnimHerbCrop3,
     .parts = kShapePartsHerbCrop3},
    {.id = ShapeId::ConduitHub,
     .quads = kShapeQuadsConduitHub,
     .boxes = kShapeBoxesConduitHub,
     .bounds = kShapeBoundsConduitHub,
     .anim = kShapeAnimConduitHub,
     .parts = kShapePartsConduitHub},
    {.id = ShapeId::WireHub,
     .quads = kShapeQuadsWireHub,
     .boxes = kShapeBoxesWireHub,
     .bounds = kShapeBoundsWireHub,
     .anim = kShapeAnimWireHub,
     .parts = kShapePartsWireHub},
    {.id = ShapeId::RuneCore,
     .quads = kShapeQuadsRuneCore,
     .boxes = kShapeBoxesRuneCore,
     .bounds = kShapeBoundsRuneCore,
     .anim = kShapeAnimRuneCore,
     .parts = kShapePartsRuneCore},
    {.id = ShapeId::MossyRunePedestal,
     .quads = kShapeQuadsMossyRunePedestal,
     .boxes = kShapeBoxesMossyRunePedestal,
     .bounds = kShapeBoundsMossyRunePedestal,
     .anim = kShapeAnimMossyRunePedestal,
     .parts = kShapePartsMossyRunePedestal},
    {.id = ShapeId::TreeSapling,
     .quads = kShapeQuadsTreeSapling,
     .boxes = kShapeBoxesTreeSapling,
     .bounds = kShapeBoundsTreeSapling,
     .anim = kShapeAnimTreeSapling,
     .parts = kShapePartsTreeSapling},
    {.id = ShapeId::HerbBush,
     .quads = kShapeQuadsHerbBush,
     .boxes = kShapeBoxesHerbBush,
     .bounds = kShapeBoundsHerbBush,
     .anim = kShapeAnimHerbBush,
     .parts = kShapePartsHerbBush},
    {.id = ShapeId::VioletCrystalCluster,
     .quads = kShapeQuadsVioletCrystalCluster,
     .boxes = kShapeBoxesVioletCrystalCluster,
     .bounds = kShapeBoundsVioletCrystalCluster,
     .anim = kShapeAnimVioletCrystalCluster,
     .parts = kShapePartsVioletCrystalCluster},
    {.id = ShapeId::DarkStoneVent,
     .quads = kShapeQuadsDarkStoneVent,
     .boxes = kShapeBoxesDarkStoneVent,
     .bounds = kShapeBoundsDarkStoneVent,
     .anim = kShapeAnimDarkStoneVent,
     .parts = kShapePartsDarkStoneVent},
    {.id = ShapeId::BicolourCrystalNode,
     .quads = kShapeQuadsBicolourCrystalNode,
     .boxes = kShapeBoxesBicolourCrystalNode,
     .bounds = kShapeBoundsBicolourCrystalNode,
     .anim = kShapeAnimBicolourCrystalNode,
     .parts = kShapePartsBicolourCrystalNode},
    {.id = ShapeId::MossyShrineStandingStone,
     .quads = kShapeQuadsMossyShrineStandingStone,
     .boxes = kShapeBoxesMossyShrineStandingStone,
     .bounds = kShapeBoundsMossyShrineStandingStone,
     .anim = kShapeAnimMossyShrineStandingStone,
     .parts = kShapePartsMossyShrineStandingStone},
    {.id = ShapeId::RunedStandingStone,
     .quads = kShapeQuadsRunedStandingStone,
     .boxes = kShapeBoxesRunedStandingStone,
     .bounds = kShapeBoundsRunedStandingStone,
     .anim = kShapeAnimRunedStandingStone,
     .parts = kShapePartsRunedStandingStone},
    {.id = ShapeId::VerdigrisStandingStone,
     .quads = kShapeQuadsVerdigrisStandingStone,
     .boxes = kShapeBoxesVerdigrisStandingStone,
     .bounds = kShapeBoundsVerdigrisStandingStone,
     .anim = kShapeAnimVerdigrisStandingStone,
     .parts = kShapePartsVerdigrisStandingStone},
    {.id = ShapeId::SandSource,
     .quads = kShapeQuadsSandSource,
     .boxes = kShapeBoxesSandSource,
     .bounds = kShapeBoundsSandSource,
     .anim = kShapeAnimSandSource,
     .parts = kShapePartsSandSource},
    {.id = ShapeId::EssenceSource,
     .quads = kShapeQuadsEssenceSource,
     .boxes = kShapeBoxesEssenceSource,
     .bounds = kShapeBoundsEssenceSource,
     .anim = kShapeAnimEssenceSource,
     .parts = kShapePartsEssenceSource},
    {.id = ShapeId::ResonantSource,
     .quads = kShapeQuadsResonantSource,
     .boxes = kShapeBoxesResonantSource,
     .bounds = kShapeBoundsResonantSource,
     .anim = kShapeAnimResonantSource,
     .parts = kShapePartsResonantSource},
    {.id = ShapeId::TimberScaffoldFrame,
     .quads = kShapeQuadsTimberScaffoldFrame,
     .boxes = kShapeBoxesTimberScaffoldFrame,
     .bounds = kShapeBoundsTimberScaffoldFrame,
     .anim = kShapeAnimTimberScaffoldFrame,
     .parts = kShapePartsTimberScaffoldFrame},
};

static_assert(std::size(kBlockShapes) == static_cast<std::size_t>(ShapeId::Count),
              "kBlockShapes needs exactly one row per ShapeId");

static_assert([] {
    for (std::size_t i = 0; i < std::size(kBlockShapes); ++i) {
        if (kBlockShapes[i].id != static_cast<ShapeId>(i)) return false;
    }
    return true;
}(), "kBlockShapes rows must be in ShapeId enum order");

inline constexpr const BlockShape& blockShape(ShapeId id) {
    return kBlockShapes[static_cast<std::size_t>(id)];
}
inline const BlockShape& blockShape(BlockId id) {
    return blockShape(blockInfo(id).shape);
}

// ---- Moving parts ---------------------------------------------------------
// The bake emits every named group as a part; WHICH of them move is gameplay
// policy, so it is stated here rather than in the model. A part is named, not
// numbered, so re-authoring a model cannot silently animate the wrong lump of
// it -- and a name its shape does not have is a compile error, not a part that
// quietly never moves.
//
// Only an animated part costs a uPartRot[] slot, which is what keeps the array
// small: the four models below carry ~37 groups between them and spend four
// slots. Slot 0 is identity and belongs to everything else.

enum class PartMotion : std::uint8_t {
    Spin,   // continuous rotation about `axis`; `rate` is turns per second
    Rock,   // sine sway about `axis`; `amount` is degrees either side
    Pulse,  // sine breathing; `amount` is the scale delta, `axis` unused
};

struct PartAnim {
    ShapeId     shape;
    const char* part;    // a ShapePart name from the bake
    PartMotion  motion;
    glm::vec3   axis;
    float       rate;    // Spin: turns/s. Rock, Pulse: cycles/s.
    float       amount;  // Rock: degrees. Pulse: scale delta. Spin: unused.
};

// Rates are chosen so rate * kAnimClockWrap is a whole number of cycles: the
// clock wraps at an hour, and a spin that is mid-turn when it wraps would jump.
inline constexpr PartAnim kPartAnims[] = {
    // The auger's bit turns while it is cutting. Down the Y axis, about a pivot
    // the bake read as (0.5, 0.125, 0.5) -- the bottom of the drill.
    {ShapeId::AugerMiningRig, "drill", PartMotion::Spin, {0.0f, 1.0f, 0.0f}, 0.75f, 0.0f},
    // A liquid surface, not a machine part: a slow shallow sway reads as a
    // simmer, where a full rotation would read as a bug.
    {ShapeId::BrewingCauldron, "contents", PartMotion::Rock, {1.0f, 0.0f, 0.0f}, 0.5f, 3.0f},
    // The infuser's core turns and its emitter throbs, which is the whole
    // reason PartMotion::Pulse exists -- and why the uniform is a 3x3 that can
    // carry scale rather than a rotation-only encoding.
    {ShapeId::ArcaneInfuser, "core", PartMotion::Spin, {0.0f, 1.0f, 0.0f}, 0.35f, 0.0f},
    {ShapeId::ArcaneInfuser, "emitter", PartMotion::Pulse, {0.0f, 1.0f, 0.0f}, 0.8f, 0.06f},
};

// A shape's part index by name, or -1. Constexpr so the table below and the
// static_asserts under it are all resolved at compile time.
inline constexpr int partIndex(ShapeId shape, std::string_view name) {
    const std::span<const ShapePart> parts = blockShape(shape).parts;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (name == std::string_view(parts[i].name)) return static_cast<int>(i);
    }
    return -1;
}

// Generous: the biggest shipped model carries 9 parts.
inline constexpr std::size_t kMaxPartsPerShape = 16;

// Every kPartAnims row must name a part its shape actually has. Without this a
// typo would compile into a part that simply never moves -- the exact failure
// this table is meant to make impossible.
static_assert([] {
    for (const PartAnim& a : kPartAnims) {
        if (partIndex(a.shape, a.part) < 0) return false;
    }
    return true;
}(), "a kPartAnims row names a part that its shape does not have");

static_assert([] {
    for (const BlockShape& s : kBlockShapes) {
        if (s.parts.size() > kMaxPartsPerShape) return false;
    }
    return true;
}(), "a baked shape has more parts than kMaxPartsPerShape");

// (ShapeId, part) -> uPartRot slot, resolved once at compile time so the mesher
// never does a string compare. Row i of kPartAnims owns slot i + 1.
inline constexpr auto kPartSlots = [] {
    std::array<std::array<std::uint8_t, kMaxPartsPerShape>,
               static_cast<std::size_t>(ShapeId::Count)> table {};
    for (std::size_t i = 0; i < std::size(kPartAnims); ++i) {
        const std::size_t shape = static_cast<std::size_t>(kPartAnims[i].shape);
        const std::size_t part =
            static_cast<std::size_t>(partIndex(kPartAnims[i].shape, kPartAnims[i].part));
        table[shape][part] = static_cast<std::uint8_t>(i + 1);
    }
    return table;
}();

// Slot 0 -- identity, nothing moves -- for any part with no animation, and for
// ShapeId::FullCube, which is what the mesher passes for an UNPOWERED block.
inline constexpr int partSlot(ShapeId shape, std::size_t part) {
    if (part >= kMaxPartsPerShape) return 0;
    return kPartSlots[static_cast<std::size_t>(shape)][part];
}

// ---- Connection parts -----------------------------------------------------
// A tube is not one shape: what it looks like depends on what is NEXT to it.
// Rather than bake a model per arrangement (64 of them for six faces), the six
// arms are named PARTS of one model and the mesher shows each only when that
// neighbour connects. So the art can never disagree with itself -- the arm IS
// the hub's arm -- and a straight run, a corner and a junction are all the same
// baked shape.
//
// This is the kPartAnims discipline a second time, and for the same reason: a
// part is addressed by NAME, so re-authoring a model cannot silently connect a
// different lump of it, and a typo is a compile error rather than an arm that
// quietly never appears.

// The canonical face order, shared with the mesher's kFaces so the two cannot
// drift apart -- kConnectParts::face and ShapeQuad::face both index this.
inline constexpr glm::ivec3 kShapeFaceDirs[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};
inline constexpr int kFaceCount = 6;

struct ConnectPart {
    ShapeId      shape;
    const char*  part;  // a ShapePart name from the bake
    std::uint8_t face;  // index into kShapeFaceDirs
};

// Blockbench names its axes the way Minecraft does: north is -Z, south is +Z.
// Both models below follow it, and the selftest proves each arm's geometry
// really does reach the wall its name claims -- a mirrored model would
// otherwise connect correctly and point the wrong way.
inline constexpr ConnectPart kConnectParts[] = {
    {ShapeId::ConduitHub, "arm_east", 0},
    {ShapeId::ConduitHub, "arm_west", 1},
    {ShapeId::ConduitHub, "arm_up", 2},
    {ShapeId::ConduitHub, "arm_down", 3},
    {ShapeId::ConduitHub, "arm_south", 4},
    {ShapeId::ConduitHub, "arm_north", 5},
    {ShapeId::WireHub, "arm_east", 0},
    {ShapeId::WireHub, "arm_west", 1},
    {ShapeId::WireHub, "arm_up", 2},
    {ShapeId::WireHub, "arm_down", 3},
    {ShapeId::WireHub, "arm_south", 4},
    {ShapeId::WireHub, "arm_north", 5},
};

static_assert([] {
    for (const ConnectPart& c : kConnectParts) {
        if (partIndex(c.shape, c.part) < 0) return false;
        if (c.face >= kFaceCount) return false;
    }
    return true;
}(), "a kConnectParts row names a part its shape does not have, or a bad face");

// (ShapeId, part) -> the face that part needs, or -1 for "always drawn".
// Resolved at compile time so the mesher never does a string compare, exactly
// like kPartSlots.
inline constexpr auto kPartFaces = [] {
    std::array<std::array<std::int8_t, kMaxPartsPerShape>,
               static_cast<std::size_t>(ShapeId::Count)> table {};
    for (auto& row : table) row.fill(-1);
    for (const ConnectPart& c : kConnectParts) {
        const std::size_t shape = static_cast<std::size_t>(c.shape);
        const std::size_t part = static_cast<std::size_t>(partIndex(c.shape, c.part));
        table[shape][part] = static_cast<std::int8_t>(c.face);
    }
    return table;
}();

// Which face a part hangs off, or -1 if it is always drawn.
inline constexpr int partFace(ShapeId shape, std::size_t part) {
    if (part >= kMaxPartsPerShape) return -1;
    return kPartFaces[static_cast<std::size_t>(shape)][part];
}

// Does this shape have connection parts at all? Everything else takes the
// mesher's plain path with no mask work, so machines pay nothing for this.
inline constexpr bool shapeConnects(ShapeId shape) {
    for (const ConnectPart& c : kConnectParts) {
        if (c.shape == shape) return true;
    }
    return false;
}

// Every face's bit set: what a shape with no connection parts is drawn with.
inline constexpr std::uint8_t kAllFaces = 0x3F;

// ---- Shape names, for the content pack format -----------------------------
// A shape is baked from a Blockbench model, so a pack cannot author one -- but
// it must be able to SAY which existing shape a block uses, and "3" is exactly
// the kind of ordinal the whole content-key exercise was about not writing
// down. One table serves both the dump and the loader, so a new shape is a name
// here and nothing else.
inline constexpr const char* kShapeNames[] = {
    "full_cube", "empty", "brewing_cauldron",
    "alchemical_alembic", "auger_mining_rig", "arcane_infuser",
    "herb_crop_0", "herb_crop_1", "herb_crop_2", "herb_crop_3",
    "conduit_hub", "wire_hub", "rune_core", "mossy_rune_pedestal",
    "tree_sapling",
    "herb_bush", "violet_crystal_cluster", "dark_stone_vent",
    "bicolour_crystal_node", "mossy_shrine_standing_stone", "runed_standing_stone",
    "verdigris_standing_stone",
    "sand_source", "essence_source", "resonant_source",
    "timber_scaffold_frame",
};
static_assert(std::size(kShapeNames) == static_cast<std::size_t>(ShapeId::Count),
              "kShapeNames needs exactly one name per ShapeId");

inline const char* shapeName(ShapeId id) {
    return kShapeNames[static_cast<std::size_t>(id)];
}

// ShapeId::Count when the name is unknown -- shapes are compiled in, so unlike
// blocks and items this sentinel can never collide with a real row.
inline ShapeId shapeFromName(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kShapeNames); ++i) {
        if (name == kShapeNames[i]) return static_cast<ShapeId>(i);
    }
    return ShapeId::Count;
}

// The boxes a block occupies, cell-local. Physics and raycasts walk these
// instead of assuming a unit cube; an empty span means the cell is passable.
inline std::span<const ShapeAabb> blockBoxes(BlockId id) {
    return blockShape(id).boxes;
}

// The union of those boxes -- a broad-phase reject and what the target
// highlight wraps.
inline const ShapeAabb& blockBounds(BlockId id) {
    return blockShape(id).bounds;
}
