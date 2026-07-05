#pragma once

#include "game/Block.h"
#include "game/Chunk.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

// A sparse, unbounded grid of chunks. Block access is in world coordinates;
// the World routes to the owning chunk, creating it on write as needed.
class World {
public:
    using ChunkMap = std::unordered_map<glm::ivec3, std::unique_ptr<Chunk>, IVec3Hash>;

    BlockId getBlock(int wx, int wy, int wz) const; // Air where no chunk exists
    void    setBlock(int wx, int wy, int wz, BlockId id);

    // Queue the owning chunk for a remesh without changing any block (used
    // when non-block state baked into the mesh changes: belt facing, power).
    void markDirtyAt(int wx, int wy, int wz);

    const ChunkMap& chunks() const { return m_chunks; }

    // World -> chunk/local coordinate split (handles negatives correctly).
    static glm::ivec3 toChunkCoord(int wx, int wy, int wz);
    static glm::ivec3 toLocalCoord(int wx, int wy, int wz);

private:
    Chunk& getOrCreateChunk(const glm::ivec3& coord);

    ChunkMap m_chunks;
};
