#include "game/World.h"

namespace {
    // Floor division / positive modulo so chunk routing works for negative
    // world coordinates too.
    int floorDiv(int a, int b) {
        int q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }
    int floorMod(int a, int b) {
        int r = a % b;
        if (r != 0 && ((a < 0) != (b < 0))) r += b;
        return r;
    }
}

glm::ivec3 World::toChunkCoord(int wx, int wy, int wz) {
    return {floorDiv(wx, CHUNK_SIZE), floorDiv(wy, CHUNK_SIZE), floorDiv(wz, CHUNK_SIZE)};
}

glm::ivec3 World::toLocalCoord(int wx, int wy, int wz) {
    return {floorMod(wx, CHUNK_SIZE), floorMod(wy, CHUNK_SIZE), floorMod(wz, CHUNK_SIZE)};
}

BlockId World::getBlock(int wx, int wy, int wz) const {
    const auto it = m_chunks.find(toChunkCoord(wx, wy, wz));
    if (it == m_chunks.end()) return BlockId::Air;
    const glm::ivec3 l = toLocalCoord(wx, wy, wz);
    return it->second->get(l.x, l.y, l.z);
}

void World::setBlock(int wx, int wy, int wz, BlockId id) {
    Chunk& chunk = getOrCreateChunk(toChunkCoord(wx, wy, wz));
    const glm::ivec3 l = toLocalCoord(wx, wy, wz);
    chunk.set(l.x, l.y, l.z, id);
}

Chunk& World::getOrCreateChunk(const glm::ivec3& coord) {
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        it = m_chunks.emplace(coord, std::make_unique<Chunk>()).first;
    }
    return *it->second;
}
