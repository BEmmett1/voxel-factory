#include "game/Atlas.h"

#include <array>

namespace {

    // Indexed by BlockId; keep in sync with the enum order. Tile numbering is
    // documented in assets/ATLAS.md. Machines show a distinct top and reuse
    // their side underneath; grass sits on dirt; logs show rings on both ends.
    const std::array<Atlas::BlockTiles, static_cast<std::size_t>(BlockId::Count)> kBlockTiles = {{
        /* Air           */ {0, 0, 0},
        /* Grass         */ {0, 1, 2},
        /* Dirt          */ {2, 2, 2},
        /* Stone         */ {3, 3, 3},
        /* Generator     */ {16, 17, 17},
        /* Wire          */ {18, 18, 18},
        /* Belt          */ {19, 19, 19},
        /* Grinder       */ {21, 22, 22},
        /* Cauldron      */ {23, 24, 24},
        /* Infuser       */ {25, 26, 26},
        /* Alembic       */ {27, 28, 28},
        /* Distiller     */ {29, 30, 30},
        /* Transmuter    */ {31, 32, 32},
        /* Miner         */ {33, 34, 34},
        /* HerbBush      */ {48, 48, 48},
        /* CrystalNode   */ {49, 49, 49},
        /* CopperOre     */ {50, 50, 50},
        /* SandNode      */ {51, 51, 51},
        /* EssenceVent   */ {53, 53, 53},
        /* SourceHerb    */ {54, 54, 54},
        /* SourceCrystal */ {55, 55, 55},
        /* SourceCopper  */ {56, 56, 56},
        /* SourceSand    */ {57, 57, 57},
        /* SourceEssence */ {59, 59, 59},
        /* Scaffold      */ {4, 4, 4},
        /* Sapling       */ {5, 5, 5},
        /* Log           */ {6, 7, 6},
        /* Leaves        */ {8, 8, 8},
        /* RainBarrel    */ {52, 58, 58}, // open water top, stave sides
    }};

} // namespace

namespace Atlas {

    const BlockTiles& tilesForBlock(BlockId id) {
        return kBlockTiles[static_cast<std::size_t>(id)];
    }

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
