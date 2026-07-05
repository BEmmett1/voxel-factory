#pragma once

#include "game/Block.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>
#include <unordered_set>

class World;

// Which world cells belong to a satisfied power network (and so render as
// "energized"). Stored sparsely — only energized cells are present.
class PowerState {
public:
    bool energized(int wx, int wy, int wz) const {
        return m_energized.find({wx, wy, wz}) != m_energized.end();
    }
    void setEnergized(const glm::ivec3& c) { m_energized.insert(c); }

    // All energized cells, for diffing two states against each other.
    const std::unordered_set<glm::ivec3, IVec3Hash>& cells() const { return m_energized; }

    bool operator==(const PowerState& other) const { return m_energized == other.m_energized; }
    bool operator!=(const PowerState& other) const { return !(*this == other); }

private:
    std::unordered_set<glm::ivec3, IVec3Hash> m_energized;
};

namespace PowerSystem {
    // Blocks that participate in power networks.
    bool isPowerNode(BlockId id);

    // Power produced / demanded by a single block, in arbitrary power units.
    int production(BlockId id);
    int demand(BlockId id);

    // Find connected power networks across the world and mark every cell of
    // each *satisfied* network (production >= demand, with some production)
    // as energized.
    PowerState solve(const World& world);
}
