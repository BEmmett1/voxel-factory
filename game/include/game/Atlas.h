#pragma once

#include "game/Block.h"

#include <glm/glm.hpp>

// Layout of the block texture atlas. The atlas is a grid of square tiles, one
// per BlockId (tile index == the enum value). These constants are shared by the
// atlas generator (which fills the pixels) and the mesher (which emits UVs), so
// they stay in lockstep.
namespace Atlas {
    constexpr int Cols   = 4;
    constexpr int Rows   = 4;   // room for up to 16 block tiles
    constexpr int TilePx = 16;
    constexpr int WidthPx  = Cols * TilePx;
    constexpr int HeightPx = Rows * TilePx;

    // UV rectangle for a block's tile, inset by half a texel to avoid bleeding
    // into neighboring tiles under nearest-neighbor sampling.
    void uvForBlock(BlockId id, glm::vec2& uvMin, glm::vec2& uvMax);
}
