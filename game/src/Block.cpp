#include "game/Block.h"

#include <array>

namespace {
    // Indexed by BlockId. Keep this in sync with the enum order.
    const std::array<BlockInfo, static_cast<std::size_t>(BlockId::Count)> kBlocks = {{
        /* Air       */ {false, {0.00f, 0.00f, 0.00f}},
        /* Grass     */ {true,  {0.30f, 0.62f, 0.26f}},
        /* Dirt      */ {true,  {0.45f, 0.31f, 0.18f}},
        /* Stone     */ {true,  {0.50f, 0.50f, 0.53f}},
        /* Generator */ {true,  {0.86f, 0.45f, 0.12f}},
        /* Wire      */ {true,  {0.82f, 0.72f, 0.20f}},
        /* Machine   */ {true,  {0.40f, 0.46f, 0.56f}},
        /* Belt      */ {true,  {0.22f, 0.22f, 0.26f}},
    }};
}

const BlockInfo& blockInfo(BlockId id) {
    return kBlocks[static_cast<std::size_t>(id)];
}
