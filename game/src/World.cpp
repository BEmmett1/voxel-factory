#include "game/World.h"

#include "game/BlockShape.h"

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
    const glm::ivec3 c = toChunkCoord(wx, wy, wz);
    Chunk& chunk = getOrCreateChunk(c);
    const glm::ivec3 l = toLocalCoord(wx, wy, wz);
    if (chunk.get(l.x, l.y, l.z) == id) return; // no-op write: stay clean
    chunk.set(l.x, l.y, l.z, id);

    // A block on a chunk face changes the neighbor's occlusion too; queue the
    // adjacent chunk(s) for a remesh (never create chunks just to mark them).
    const auto touch = [this, &c](int dx, int dy, int dz) {
        const auto it = m_chunks.find(c + glm::ivec3(dx, dy, dz));
        if (it != m_chunks.end()) it->second->markDirty();
    };
    if (l.x == 0) touch(-1, 0, 0);
    if (l.x == CHUNK_SIZE - 1) touch(1, 0, 0);
    if (l.y == 0) touch(0, -1, 0);
    if (l.y == CHUNK_SIZE - 1) touch(0, 1, 0);
    if (l.z == 0) touch(0, 0, -1);
    if (l.z == CHUNK_SIZE - 1) touch(0, 0, 1);
}

void World::markDirtyAt(int wx, int wy, int wz) {
    const auto it = m_chunks.find(toChunkCoord(wx, wy, wz));
    if (it != m_chunks.end()) it->second->markDirty();
}

void World::markDirtyAround(const glm::ivec3& pos) {
    markDirtyAt(pos.x, pos.y, pos.z);
    for (const glm::ivec3& d : kShapeFaceDirs) {
        const glm::ivec3 n = pos + d;
        markDirtyAt(n.x, n.y, n.z);
    }
}

Chunk& World::getOrCreateChunk(const glm::ivec3& coord) {
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        it = m_chunks.emplace(coord, std::make_unique<Chunk>()).first;
    }
    return *it->second;
}
