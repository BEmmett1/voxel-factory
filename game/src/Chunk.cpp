#include "game/Chunk.h"

Chunk::Chunk() {
    m_blocks.fill(BlockId::Air);
}

bool Chunk::inBounds(int x, int y, int z) const {
    return x >= 0 && x < CHUNK_SIZE &&
           y >= 0 && y < CHUNK_SIZE &&
           z >= 0 && z < CHUNK_SIZE;
}

BlockId Chunk::get(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return BlockId::Air;
    return m_blocks[index(x, y, z)];
}

void Chunk::set(int x, int y, int z, BlockId id) {
    if (!inBounds(x, y, z)) return;
    m_blocks[index(x, y, z)] = id;
    m_dirty = true;
}
