#pragma once

#include "game/Block.h"

#include <glm/glm.hpp>

// Layout of the texture atlas: a grid of square tiles shared by the world
// mesher and the UI icon renderer. The pixels come from assets/atlas.png when
// present (hand-paintable, 256x256), otherwise from the procedural fallback in
// VoxelGame::buildAtlas(). Tile indices are row * Cols + col; the full map
// lives in assets/ATLAS.md. Rows: 0 terrain + trees, 1-2 machines, 3 nodes +
// sources, 4-7 item icons + tools, 8-10 more machines, 11 more item icons.
//
// The sheet grew from 8 rows to 16 when the recipe overhaul landed. Because a
// tile index is `row * Cols + col` and Cols did not change, every existing
// index kept its meaning -- growing DOWNWARD is the only free way to expand
// this grid, and the reason to never widen it.
namespace Atlas {
    constexpr int Cols   = 16;
    constexpr int Rows   = 16;  // 256 tiles
    constexpr int TilePx = 16;
    constexpr int WidthPx  = Cols * TilePx;  // 256
    constexpr int HeightPx = Rows * TilePx;  // 256

    // Special tile: conduit top face with a direction arrow (drawn pointing
    // toward +v; the mesher rotates UVs to match each belt's facing).
    constexpr int BeltArrowTile = 20;

    // A block's tiles by face live on its BlockInfo registry row (Block.h) —
    // one row per block holds everything, so the tables can't drift apart.
    using BlockTiles = ::BlockTiles;
    inline const BlockTiles& tilesForBlock(BlockId id) { return blockInfo(id).tiles; }

    // UV rectangle for a tile index, inset by half a texel to avoid bleeding
    // into neighboring tiles under nearest-neighbor sampling.
    void uvForTile(int tile, glm::vec2& uvMin, glm::vec2& uvMax);

    // UV rectangle for the block face with this outward normal.
    void uvForBlockFace(BlockId id, const glm::vec3& normal,
                        glm::vec2& uvMin, glm::vec2& uvMax);
}
