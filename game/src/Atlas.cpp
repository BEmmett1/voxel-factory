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

    void uvForBlock(BlockId id, glm::vec2& uvMin, glm::vec2& uvMax) {
        uvForTile(static_cast<int>(id), uvMin, uvMax);
    }

} // namespace Atlas
