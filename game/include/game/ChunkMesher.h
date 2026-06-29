#pragma once

#include "game/PowerSystem.h"

#include <glm/glm.hpp>
#include <vector>

class World;

// Builds renderable geometry by emitting a quad only where a solid block
// borders a non-solid neighbor (hidden-face removal), looking across chunk
// boundaries via the World. Output is an interleaved float array with layout:
//   position(3), normal(3), uv(2), emissive(1)
// where emissive > 0 makes a block self-illuminate (energized power blocks).
namespace ChunkMesher {
    // Append the visible faces of the chunk at chunkCoord into `out`.
    void appendChunk(std::vector<float>& out, const World& world,
                     const glm::ivec3& chunkCoord, const PowerState& power);
}
