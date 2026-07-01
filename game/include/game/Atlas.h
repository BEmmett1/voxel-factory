#pragma once

#include "game/Block.h"

#include <glm/glm.hpp>

// Layout of the block texture atlas. The atlas is a grid of square tiles, one
// per BlockId (tile index == the enum value). These constants are shared by the
// atlas generator (which fills the pixels) and the mesher (which emits UVs), so
// they stay in lockstep.
namespace Atlas {
    constexpr int Cols   = 8;
    constexpr int Rows   = 12;  // 96 tiles: blocks use 0-31, item icons 32+
    constexpr int TilePx = 16;
    constexpr int WidthPx  = Cols * TilePx;
    constexpr int HeightPx = Rows * TilePx;

    // UV rectangle for a tile index, inset by half a texel to avoid bleeding
    // into neighboring tiles under nearest-neighbor sampling.
    void uvForTile(int tile, glm::vec2& uvMin, glm::vec2& uvMax);

    // Block tiles are indexed by the block's enum value.
    void uvForBlock(BlockId id, glm::vec2& uvMin, glm::vec2& uvMax);
}
