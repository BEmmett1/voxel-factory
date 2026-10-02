#pragma once

#include "game/PowerSystem.h"
#include "game/Belt.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Chunk;
class World;

// Builds renderable geometry by emitting a quad only where a solid block
// borders a neighbor that doesn't fill its cell (hidden-face removal), looking
// across chunk boundaries via the World. Output is an interleaved float array
// with layout:
//   position(3), normal(3), uv(2), emissive(1)
// where emissive > 0 makes a block self-illuminate (energized power blocks,
// resource sources). Belt top faces get a direction arrow rotated to the
// belt's facing (looked up in `belts`).
//
// Two outputs, because they sample DIFFERENT textures: plain blocks are unit
// cubes with atlas.png tiles, while a block carrying a ShapeId (BlockShape.h)
// emits its baked sub-cube quads with UVs into shapes.png. Splitting them
// keeps one texture bound per draw instead of paying a per-vertex sheet
// selector across the whole world, and the shaped pass is the one transparency
// will eventually need for itself.
//
// The shaped buffer carries FIVE extra floats per vertex:
//   position(3), normal(3), uv(2), emissive(1), animBank(1), partOff(3), slot(1)
// `animBank` names the vertex's ShapeId so the shader can offset its UV to the
// current animation frame -- or 0 when the block is unpowered, which parks it
// on frame 0 (bank 0 is ShapeId::FullCube, whose offset is always zero). Power
// is already a mesh input for the glow, so that gate is free.
//
// `partOff`/`slot` move a block PART: the offset from its pivot is baked (a
// world-space chunk vertex cannot recover its own cell, so a pivot would be
// useless here) and the slot names the part's transform in uPartRot[].
//
// Only shapes can do either, so the whole ordinary world is spared the 20
// bytes; the plain mesh's VAO simply leaves those attributes disabled, which
// reads back as bank 0, offset (0,0,0) and slot 0 -- every one of them inert.
namespace ChunkMesher {
    using BeltMap = std::unordered_map<glm::ivec3, Belt, IVec3Hash>;
    using CellSet = std::unordered_set<glm::ivec3, IVec3Hash>;

    // Append the visible faces of `chunk` (at chunkCoord): full cubes into
    // `out`, shaped blocks into `shapedOut`. Block reads stay inside the
    // chunk's array; only boundary occlusion tests look at (prefetched)
    // neighbor chunks. `cranking` holds the hand-cranked machines being turned
    // right now, which animate although no power reaches them.
    void appendChunk(std::vector<float>& out, std::vector<float>& shapedOut,
                     const World& world,
                     const Chunk& chunk, const glm::ivec3& chunkCoord,
                     const PowerState& power, const BeltMap& belts,
                     const CellSet& cranking);
}
