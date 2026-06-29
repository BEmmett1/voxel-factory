#pragma once

#include "game/Block.h"

#include <array>

// A cubic block of the world. Coordinates are local (0..CHUNK_SIZE-1).
constexpr int CHUNK_SIZE = 16;

class Chunk {
public:
    Chunk(); // filled with Air

    BlockId get(int x, int y, int z) const;
    void    set(int x, int y, int z, BlockId id);

    bool inBounds(int x, int y, int z) const;

    bool dirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

private:
    static int index(int x, int y, int z) {
        return x + CHUNK_SIZE * (y + CHUNK_SIZE * z);
    }

    std::array<BlockId, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> m_blocks;
    bool m_dirty = true;
};
