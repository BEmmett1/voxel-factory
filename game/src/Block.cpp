#include "game/Block.h"

#include <array>

namespace {
    // Indexed by BlockId. Keep this in sync with the enum order.
    // Fields: solid, color, emissive.
    const std::array<BlockInfo, static_cast<std::size_t>(BlockId::Count)> kBlocks = {{
        /* Air           */ {false, {0.00f, 0.00f, 0.00f}, 0.0f},
        /* Grass         */ {true,  {0.30f, 0.62f, 0.26f}, 0.0f},
        /* Dirt          */ {true,  {0.45f, 0.31f, 0.18f}, 0.0f},
        /* Stone         */ {true,  {0.50f, 0.50f, 0.53f}, 0.0f},
        /* Generator     */ {true,  {0.86f, 0.45f, 0.12f}, 0.0f},
        /* Wire          */ {true,  {0.82f, 0.72f, 0.20f}, 0.0f},
        /* Belt          */ {true,  {0.22f, 0.22f, 0.26f}, 0.0f},
        /* Grinder       */ {true,  {0.45f, 0.45f, 0.48f}, 0.0f},
        /* Cauldron      */ {true,  {0.18f, 0.18f, 0.22f}, 0.0f},
        /* Infuser       */ {true,  {0.40f, 0.62f, 0.60f}, 0.0f},
        /* Alembic       */ {true,  {0.72f, 0.58f, 0.28f}, 0.0f},
        /* Distiller     */ {true,  {0.58f, 0.30f, 0.55f}, 0.0f},
        /* Transmuter    */ {true,  {0.85f, 0.75f, 0.35f}, 0.0f},
        /* Miner         */ {true,  {0.35f, 0.40f, 0.46f}, 0.0f},
        /* HerbBush      */ {true,  {0.20f, 0.55f, 0.22f}, 0.0f},
        /* CrystalNode   */ {true,  {0.55f, 0.45f, 0.85f}, 0.0f},
        /* CopperOre     */ {true,  {0.70f, 0.45f, 0.30f}, 0.0f},
        /* SandNode      */ {true,  {0.85f, 0.78f, 0.55f}, 0.0f},
        /* EssenceVent   */ {true,  {0.60f, 0.28f, 0.72f}, 0.0f},
        /* SourceHerb    */ {true,  {0.30f, 0.95f, 0.30f}, 0.6f},
        /* SourceCrystal */ {true,  {0.75f, 0.55f, 1.00f}, 0.6f},
        /* SourceCopper  */ {true,  {1.00f, 0.55f, 0.25f}, 0.6f},
        /* SourceSand    */ {true,  {1.00f, 0.92f, 0.55f}, 0.6f},
        /* SourceEssence */ {true,  {0.85f, 0.35f, 1.00f}, 0.6f},
        /* Scaffold      */ {true,  {0.68f, 0.62f, 0.48f}, 0.0f},
        /* Sapling       */ {true,  {0.45f, 0.72f, 0.28f}, 0.0f},
        /* Log           */ {true,  {0.45f, 0.33f, 0.18f}, 0.0f},
        /* Leaves        */ {true,  {0.18f, 0.50f, 0.16f}, 0.0f},
        /* RainBarrel    */ {true,  {0.40f, 0.28f, 0.15f}, 0.0f},
    }};
}

const BlockInfo& blockInfo(BlockId id) {
    return kBlocks[static_cast<std::size_t>(id)];
}

bool isMachine(BlockId id) {
    return id == BlockId::Grinder || id == BlockId::Cauldron ||
           id == BlockId::Infuser || id == BlockId::Alembic ||
           id == BlockId::Distiller || id == BlockId::Transmuter ||
           id == BlockId::Miner || id == BlockId::Generator ||
           id == BlockId::RainBarrel;
}

bool isSource(BlockId id) {
    return id >= BlockId::SourceHerb && id <= BlockId::SourceEssence;
}

bool isResourceNode(BlockId id) {
    return id >= BlockId::HerbBush && id <= BlockId::EssenceVent;
}

BlockId sourceSpawnsNode(BlockId id) {
    switch (id) {
        case BlockId::SourceHerb:    return BlockId::HerbBush;
        case BlockId::SourceCrystal: return BlockId::CrystalNode;
        case BlockId::SourceCopper:  return BlockId::CopperOre;
        case BlockId::SourceSand:    return BlockId::SandNode;
        case BlockId::SourceEssence: return BlockId::EssenceVent;
        default:                     return BlockId::Air;
    }
}

const char* blockName(BlockId id) {
    switch (id) {
        case BlockId::Air:           return "Air";
        case BlockId::Grass:         return "Grass";
        case BlockId::Dirt:          return "Dirt";
        case BlockId::Stone:         return "Stone";
        case BlockId::Generator:     return "Generator";
        case BlockId::Wire:          return "Wire";
        case BlockId::Belt:          return "Conduit";
        case BlockId::Grinder:       return "Grinder";
        case BlockId::Cauldron:      return "Cauldron";
        case BlockId::Infuser:       return "Infuser";
        case BlockId::Alembic:       return "Alembic";
        case BlockId::Distiller:     return "Distiller";
        case BlockId::Transmuter:    return "Transmuter";
        case BlockId::Miner:         return "Miner";
        case BlockId::HerbBush:      return "Herb Bush";
        case BlockId::CrystalNode:   return "Crystal Node";
        case BlockId::CopperOre:     return "Copper Ore";
        case BlockId::SandNode:      return "Sand";
        case BlockId::EssenceVent:   return "Essence Vent";
        case BlockId::SourceHerb:    return "Herb Source";
        case BlockId::SourceCrystal: return "Crystal Source";
        case BlockId::SourceCopper:  return "Copper Source";
        case BlockId::SourceSand:    return "Sand Source";
        case BlockId::SourceEssence: return "Essence Source";
        case BlockId::Scaffold:      return "Scaffold";
        case BlockId::Sapling:       return "Sapling";
        case BlockId::Log:           return "Log";
        case BlockId::Leaves:        return "Leaves";
        case BlockId::RainBarrel:    return "Rain Barrel";
        default:                     return "?";
    }
}
