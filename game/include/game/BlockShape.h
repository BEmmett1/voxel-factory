#pragma once

#include "game/Block.h"

#include <glm/glm.hpp>

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
};

inline constexpr BlockShape kBlockShapes[] = {
    {.id = ShapeId::FullCube, .boxes = kUnitCubeBoxes},
    {.id = ShapeId::Empty, .bounds = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}},
    {.id = ShapeId::BrewingCauldron,
     .quads = kShapeQuadsBrewingCauldron,
     .boxes = kShapeBoxesBrewingCauldron,
     .bounds = kShapeBoundsBrewingCauldron,
     .anim = kShapeAnimBrewingCauldron},
    {.id = ShapeId::AlchemicalAlembic,
     .quads = kShapeQuadsAlchemicalAlembic,
     .boxes = kShapeBoxesAlchemicalAlembic,
     .bounds = kShapeBoundsAlchemicalAlembic,
     .anim = kShapeAnimAlchemicalAlembic},
    {.id = ShapeId::AugerMiningRig,
     .quads = kShapeQuadsAugerMiningRig,
     .boxes = kShapeBoxesAugerMiningRig,
     .bounds = kShapeBoundsAugerMiningRig,
     .anim = kShapeAnimAugerMiningRig},
    {.id = ShapeId::ArcaneInfuser,
     .quads = kShapeQuadsArcaneInfuser,
     .boxes = kShapeBoxesArcaneInfuser,
     .bounds = kShapeBoundsArcaneInfuser,
     .anim = kShapeAnimArcaneInfuser},
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

// ---- Shape names, for the content pack format -----------------------------
// A shape is baked from a Blockbench model, so a pack cannot author one -- but
// it must be able to SAY which existing shape a block uses, and "3" is exactly
// the kind of ordinal the whole content-key exercise was about not writing
// down. One table serves both the dump and the loader, so a new shape is a name
// here and nothing else.
inline constexpr const char* kShapeNames[] = {
    "full_cube", "empty", "brewing_cauldron",
    "alchemical_alembic", "auger_mining_rig", "arcane_infuser",
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
