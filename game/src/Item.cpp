#include "game/Item.h"

#include <array>

namespace {
    // Indexed by ItemId. Keep in sync with the enum order.
    // Fields: name, atlasTile, placeable, placesBlock.
    // Item icon tiles live at 32+ in the atlas; tiles 0-31 belong to blocks.
    const std::array<ItemInfo, static_cast<std::size_t>(ItemId::Count)> kItems = {{
        /* None                 */ {"-",                     0, false, BlockId::Air},
        /* Stone                */ {"Stone",               32, false, BlockId::Air},
        /* CopperOre            */ {"Copper Ore",          33, false, BlockId::Air},
        /* Sand                 */ {"Sand",                34, false, BlockId::Air},
        /* Herb                 */ {"Herb",                35, false, BlockId::Air},
        /* Crystal              */ {"Crystal",             36, false, BlockId::Air},
        /* SpringWater          */ {"Spring Water",        37, false, BlockId::Air},
        /* Essence              */ {"Essence",             38, false, BlockId::Air},
        /* CopperIngot          */ {"Copper Ingot",        39, false, BlockId::Air},
        /* CopperPlate          */ {"Copper Plate",        40, false, BlockId::Air},
        /* Glass                */ {"Glass",               41, false, BlockId::Air},
        /* Vial                 */ {"Vial",                42, false, BlockId::Air},
        /* MachineFrame         */ {"Machine Frame",       43, false, BlockId::Air},
        /* GroundHerb           */ {"Ground Herb",         44, false, BlockId::Air},
        /* CrystalDust          */ {"Crystal Dust",        45, false, BlockId::Air},
        /* HerbalTincture       */ {"Herbal Tincture",     46, false, BlockId::Air},
        /* MineralSolution      */ {"Mineral Solution",    47, false, BlockId::Air},
        /* HealingDraught       */ {"Healing Draught",     48, false, BlockId::Air},
        /* ManaVial             */ {"Mana Vial",           49, false, BlockId::Air},
        /* ElixirOfVigor        */ {"Elixir of Vigor",     50, false, BlockId::Air},
        /* RefinedElixir        */ {"Refined Elixir",      51, false, BlockId::Air},
        /* PhilosophersCatalyst */ {"Philosopher's Catalyst", 52, false, BlockId::Air},
        /* PhilosophersStone    */ {"Philosopher's Stone", 53, false, BlockId::Air},
        /* Conduit              */ {"Conduit",             54, true,  BlockId::Belt},
        /* WireItem             */ {"Wire",                55, true,  BlockId::Wire},
        /* GeneratorItem        */ {"Generator",           56, true,  BlockId::Generator},
        /* GrinderItem          */ {"Grinder",             57, true,  BlockId::Grinder},
        /* CauldronItem         */ {"Cauldron",            58, true,  BlockId::Cauldron},
        /* InfuserItem          */ {"Infuser",             59, true,  BlockId::Infuser},
        /* AlembicItem          */ {"Alembic",             60, true,  BlockId::Alembic},
        /* DistillerItem        */ {"Distiller",           61, true,  BlockId::Distiller},
        /* TransmuterItem       */ {"Transmuter",          62, true,  BlockId::Transmuter},
        /* MinerItem            */ {"Miner",               63, true,  BlockId::Miner},
        /* HerbSourceItem       */ {"Herb Source",         64, true,  BlockId::SourceHerb},
        /* CrystalSourceItem    */ {"Crystal Source",      65, true,  BlockId::SourceCrystal},
        /* CopperSourceItem     */ {"Copper Source",       66, true,  BlockId::SourceCopper},
        /* SandSourceItem       */ {"Sand Source",         67, true,  BlockId::SourceSand},
        /* WaterSourceItem      */ {"Water Source",        68, true,  BlockId::SourceWater},
        /* EssenceSourceItem    */ {"Essence Source",      69, true,  BlockId::SourceEssence},
        /* Wrench               */ {"Wrench",              70, false, BlockId::Air},
        /* DirtItem             */ {"Dirt",                71, true,  BlockId::Dirt},
        /* GrassItem            */ {"Grass",               72, true,  BlockId::Grass},
        /* ScaffoldItem         */ {"Scaffold",            73, true,  BlockId::Scaffold},
    }};
}

const ItemInfo& itemInfo(ItemId id) {
    return kItems[static_cast<std::size_t>(id)];
}

BlockId nodeForRaw(ItemId id) {
    switch (id) {
        case ItemId::Herb:        return BlockId::HerbBush;
        case ItemId::Crystal:     return BlockId::CrystalNode;
        case ItemId::CopperOre:   return BlockId::CopperOre;
        case ItemId::Sand:        return BlockId::SandNode;
        case ItemId::SpringWater: return BlockId::Spring;
        case ItemId::Essence:     return BlockId::EssenceVent;
        default:                  return BlockId::Air;
    }
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
        case BlockId::Distiller:   return {ItemId::DistillerItem, 1};
        case BlockId::Transmuter:  return {ItemId::TransmuterItem, 1};
        case BlockId::Miner:       return {ItemId::MinerItem, 1};
        // Terrain is collectable (and placeable back) -- the island's material
        // is conserved rather than lost.
        case BlockId::Dirt:        return {ItemId::DirtItem, 1};
        case BlockId::Grass:       return {ItemId::GrassItem, 1};
        case BlockId::Scaffold:    return {ItemId::ScaffoldItem, 1};
        // Air -> nothing.
        default:                   return {ItemId::None, 0};
    }
}
