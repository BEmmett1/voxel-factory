#pragma once

#include "game/Chunk.h"
#include "game/Block.h"

#include <array>

// Per-cell result of a power solve: which blocks belong to a satisfied power
// network (and should therefore render as "energized").
class PowerState {
public:
    bool energized(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
            return false;
        return m_energized[idx(x, y, z)];
    }
    void setEnergized(int x, int y, int z, bool v) { m_energized[idx(x, y, z)] = v; }

    bool operator==(const PowerState&) const = default;

private:
    static int idx(int x, int y, int z) {
        return x + CHUNK_SIZE * (y + CHUNK_SIZE * z);
    }
    std::array<bool, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> m_energized{}; // all false
};

namespace PowerSystem {
    // Blocks that participate in power networks.
    bool isPowerNode(BlockId id);

    // Power produced / demanded by a single block, in arbitrary power units.
    int production(BlockId id);
    int demand(BlockId id);

    // Find connected power networks and mark every cell of each *satisfied*
    // network (production >= demand, with some production) as energized.
    PowerState solve(const Chunk& chunk);
}
