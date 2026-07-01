#include "game/Item.h"

#include <array>

namespace {
    // Indexed by ItemId. Keep in sync with the enum order.
    // Fields: name, atlasTile, placeable, placesBlock.
    const std::array<ItemInfo, static_cast<std::size_t>(ItemId::Count)> kItems = {{
        /* None                 */ {"-",                     0, false, BlockId::Air},
        /* Stone                */ {"Stone",               24, false, BlockId::Air},
        /* CopperOre            */ {"Copper Ore",          25, false, BlockId::Air},
        /* Sand                 */ {"Sand",                26, false, BlockId::Air},
        /* Herb                 */ {"Herb",                27, false, BlockId::Air},
        /* Crystal              */ {"Crystal",             28, false, BlockId::Air},
        /* SpringWater          */ {"Spring Water",        29, false, BlockId::Air},
        /* Essence              */ {"Essence",             30, false, BlockId::Air},
        /* CopperIngot          */ {"Copper Ingot",        31, false, BlockId::Air},
        /* CopperPlate          */ {"Copper Plate",        32, false, BlockId::Air},
        /* Glass                */ {"Glass",               33, false, BlockId::Air},
        /* Vial                 */ {"Vial",                34, false, BlockId::Air},
        /* MachineFrame         */ {"Machine Frame",       35, false, BlockId::Air},
        /* GroundHerb           */ {"Ground Herb",         36, false, BlockId::Air},
        /* CrystalDust          */ {"Crystal Dust",        37, false, BlockId::Air},
        /* HerbalTincture       */ {"Herbal Tincture",     38, false, BlockId::Air},
        /* MineralSolution      */ {"Mineral Solution",    39, false, BlockId::Air},
        /* HealingDraught       */ {"Healing Draught",     40, false, BlockId::Air},
        /* ManaVial             */ {"Mana Vial",           41, false, BlockId::Air},
        /* ElixirOfVigor        */ {"Elixir of Vigor",     42, false, BlockId::Air},
        /* RefinedElixir        */ {"Refined Elixir",      43, false, BlockId::Air},
        /* PhilosophersCatalyst */ {"Philosopher's Catalyst", 44, false, BlockId::Air},
        /* PhilosophersStone    */ {"Philosopher's Stone", 45, false, BlockId::Air},
        /* Conduit              */ {"Conduit",             46, true,  BlockId::Belt},
        /* WireItem             */ {"Wire",                47, true,  BlockId::Wire},
        /* GeneratorItem        */ {"Generator",           48, true,  BlockId::Generator},
        /* GrinderItem          */ {"Grinder",             49, true,  BlockId::Grinder},
        /* CauldronItem         */ {"Cauldron",            50, true,  BlockId::Cauldron},
        /* InfuserItem          */ {"Infuser",             51, true,  BlockId::Infuser},
        /* AlembicItem          */ {"Alembic",             52, true,  BlockId::Alembic},
        /* MinerItem            */ {"Miner",               53, true,  BlockId::Miner},
        /* HerbSourceItem       */ {"Herb Source",         54, true,  BlockId::SourceHerb},
        /* CrystalSourceItem    */ {"Crystal Source",      55, true,  BlockId::SourceCrystal},
        /* CopperSourceItem     */ {"Copper Source",       56, true,  BlockId::SourceCopper},
        /* SandSourceItem       */ {"Sand Source",         57, true,  BlockId::SourceSand},
        /* WaterSourceItem      */ {"Water Source",        58, true,  BlockId::SourceWater},
        /* EssenceSourceItem    */ {"Essence Source",      59, true,  BlockId::SourceEssence},
    }};
}

const ItemInfo& itemInfo(ItemId id) {
    return kItems[static_cast<std::size_t>(id)];
}

ItemStack blockDrop(BlockId id) {
    switch (id) {
        // Resource nodes -> raw materials.
        case BlockId::HerbBush:    return {ItemId::Herb, 1};
        case BlockId::CrystalNode: return {ItemId::Crystal, 1};
        case BlockId::CopperOre:   return {ItemId::CopperOre, 1};
        case BlockId::SandNode:    return {ItemId::Sand, 1};
        case BlockId::Spring:      return {ItemId::SpringWater, 1};
        case BlockId::EssenceVent: return {ItemId::Essence, 1};
        case BlockId::Stone:       return {ItemId::Stone, 1};
        // Sources -> their placeable item (relocatable).
        case BlockId::SourceHerb:    return {ItemId::HerbSourceItem, 1};
        case BlockId::SourceCrystal: return {ItemId::CrystalSourceItem, 1};
        case BlockId::SourceCopper:  return {ItemId::CopperSourceItem, 1};
        case BlockId::SourceSand:    return {ItemId::SandSourceItem, 1};
        case BlockId::SourceWater:   return {ItemId::WaterSourceItem, 1};
        case BlockId::SourceEssence: return {ItemId::EssenceSourceItem, 1};
        // Placed equipment -> its placeable item back.
        case BlockId::Generator:   return {ItemId::GeneratorItem, 1};
        case BlockId::Wire:        return {ItemId::WireItem, 1};
        case BlockId::Belt:        return {ItemId::Conduit, 1};
        case BlockId::Grinder:     return {ItemId::GrinderItem, 1};
        case BlockId::Cauldron:    return {ItemId::CauldronItem, 1};
        case BlockId::Infuser:     return {ItemId::InfuserItem, 1};
        case BlockId::Alembic:     return {ItemId::AlembicItem, 1};
        case BlockId::Miner:       return {ItemId::MinerItem, 1};
        // Grass / Dirt / Air -> nothing.
        default:                   return {ItemId::None, 0};
    }
}
