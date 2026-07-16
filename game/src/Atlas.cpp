#include "game/Atlas.h"

namespace Atlas {

    void uvForTile(int tile, glm::vec2& uvMin, glm::vec2& uvMax) {
        const int col = tile % Cols;
        const int row = tile / Cols;

        const float inset = 0.5f; // half a texel
        uvMin.x = (col * TilePx + inset) / static_cast<float>(WidthPx);
        uvMin.y = (row * TilePx + inset) / static_cast<float>(HeightPx);
        uvMax.x = ((col + 1) * TilePx - inset) / static_cast<float>(WidthPx);
        uvMax.y = ((row + 1) * TilePx - inset) / static_cast<float>(HeightPx);
    }

    void uvForBlockFace(BlockId id, const glm::vec3& normal,
                        glm::vec2& uvMin, glm::vec2& uvMax) {
        const BlockTiles& t = tilesForBlock(id);
        const int tile = normal.y > 0.5f ? t.top
                       : normal.y < -0.5f ? t.bottom
                                          : t.side;
        uvForTile(tile, uvMin, uvMax);
    }

} // namespace Atlas
