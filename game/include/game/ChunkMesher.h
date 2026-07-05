#pragma once

#include "game/PowerSystem.h"
#include "game/Belt.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class Chunk;
class World;

// Builds renderable geometry by emitting a quad only where a solid block
// borders a non-solid neighbor (hidden-face removal), looking across chunk
// boundaries via the World. Output is an interleaved float array with layout:
//   position(3), normal(3), uv(2), emissive(1)
// where emissive > 0 makes a block self-illuminate (energized power blocks,
// resource sources). Belt top faces get a direction arrow rotated to the
// belt's facing (looked up in `belts`).
namespace ChunkMesher {
    using BeltMap = std::unordered_map<glm::ivec3, Belt, IVec3Hash>;

    // Append the visible faces of `chunk` (at chunkCoord) into `out`. Block
    // reads stay inside the chunk's array; only boundary occlusion tests look
    // at (prefetched) neighbor chunks.
    void appendChunk(std::vector<float>& out, const World& world,
                     const Chunk& chunk, const glm::ivec3& chunkCoord,
                     const PowerState& power, const BeltMap& belts);
}
