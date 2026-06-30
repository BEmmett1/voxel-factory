#include "game/Block.h"

#include <array>

namespace {
    // Indexed by BlockId. Keep this in sync with the enum order.
    const std::array<BlockInfo, static_cast<std::size_t>(BlockId::Count)> kBlocks = {{
        /* Air         */ {false, {0.00f, 0.00f, 0.00f}},
        /* Grass       */ {true,  {0.30f, 0.62f, 0.26f}},
        /* Dirt        */ {true,  {0.45f, 0.31f, 0.18f}},
        /* Stone       */ {true,  {0.50f, 0.50f, 0.53f}},
        /* Generator   */ {true,  {0.86f, 0.45f, 0.12f}},
        /* Wire        */ {true,  {0.82f, 0.72f, 0.20f}},
        /* Belt        */ {true,  {0.22f, 0.22f, 0.26f}},
        /* Grinder     */ {true,  {0.45f, 0.45f, 0.48f}},
        /* Cauldron    */ {true,  {0.18f, 0.18f, 0.22f}},
        /* Infuser     */ {true,  {0.40f, 0.62f, 0.60f}},
        /* Alembic     */ {true,  {0.72f, 0.58f, 0.28f}},
        /* Miner       */ {true,  {0.35f, 0.40f, 0.46f}},
        /* HerbBush    */ {true,  {0.20f, 0.55f, 0.22f}},
        /* CrystalNode */ {true,  {0.55f, 0.45f, 0.85f}},
        /* CopperOre   */ {true,  {0.70f, 0.45f, 0.30f}},
        /* SandNode    */ {true,  {0.85f, 0.78f, 0.55f}},
        /* WaterSource */ {true,  {0.25f, 0.50f, 0.85f}},
        /* EssenceVent */ {true,  {0.60f, 0.28f, 0.72f}},
    }};
}

const BlockInfo& blockInfo(BlockId id) {
    return kBlocks[static_cast<std::size_t>(id)];
}

bool isMachine(BlockId id) {
    return id == BlockId::Grinder || id == BlockId::Cauldron ||
           id == BlockId::Infuser || id == BlockId::Alembic || id == BlockId::Miner;
}

const char* blockName(BlockId id) {
    switch (id) {
        case BlockId::Air:         return "Air";
        case BlockId::Grass:       return "Grass";
        case BlockId::Dirt:        return "Dirt";
        case BlockId::Stone:       return "Stone";
        case BlockId::Generator:   return "Generator";
        case BlockId::Wire:        return "Wire";
        case BlockId::Belt:        return "Conduit";
        case BlockId::Grinder:     return "Grinder";
        case BlockId::Cauldron:    return "Cauldron";
        case BlockId::Infuser:     return "Infuser";
        case BlockId::Alembic:     return "Alembic";
        case BlockId::Miner:       return "Miner";
        case BlockId::HerbBush:    return "Herb Bush";
        case BlockId::CrystalNode: return "Crystal Node";
        case BlockId::CopperOre:   return "Copper Ore";
        case BlockId::SandNode:    return "Sand";
        case BlockId::WaterSource: return "Spring";
        case BlockId::EssenceVent: return "Essence Vent";
        default:                   return "?";
    }
}
