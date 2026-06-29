#pragma once

#include <glm/glm.hpp>
#include <vector>

class Chunk;

// Builds a renderable mesh for a chunk by emitting a quad only where a solid
// block borders a non-solid neighbor (hidden-face removal). Output is an
// interleaved float array with layout: position(3), normal(3), color(3).
namespace ChunkMesher {
    std::vector<float> build(const Chunk& chunk, const glm::vec3& origin);
}
