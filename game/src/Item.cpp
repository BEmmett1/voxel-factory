#include "game/Item.h"

#include "game/Atlas.h"

#include <array>

namespace {
    // Indexed by ItemId. Keep in sync with the enum order.
    // Fields: name, atlasTile, placeable, placesBlock.
    // Material icons live in atlas rows 4-5 (tiles 64+; see assets/ATLAS.md).
    // Placeables carry -1: their icon is the placed block's side tile.
    const std::array<ItemInfo, static_cast<std::size_t>(ItemId::Count)> kItems = {{
        /* None                 */ {"-",                    -1, false, BlockId::Air},
        /* Stone                */ {"Stone",                64, false, BlockId::Air},
        /* CopperOre            */ {"Copper Ore",           65, false, BlockId::Air},
        /* Sand                 */ {"Sand",                 66, false, BlockId::Air},
        /* Herb                 */ {"Herb",                 67, false, BlockId::Air},
        /* Crystal              */ {"Crystal",              68, false, BlockId::Air},
        /* SpringWater          */ {"Rain Water",           69, false, BlockId::Air},
        /* Essence              */ {"Essence",              70, false, BlockId::Air},
        /* CopperIngot          */ {"Copper Ingot",         71, false, BlockId::Air},
        /* CopperPlate          */ {"Copper Plate",         72, false, BlockId::Air},
        /* Glass                */ {"Glass",                73, false, BlockId::Air},
        /* Vial                 */ {"Vial",                 74, false, BlockId::Air},
        /* MachineFrame         */ {"Machine Frame",        75, false, BlockId::Air},
        /* GroundHerb           */ {"Ground Herb",          80, false, BlockId::Air},
        /* CrystalDust          */ {"Crystal Dust",         81, false, BlockId::Air},
        /* HerbalTincture       */ {"Herbal Tincture",      82, false, BlockId::Air},
        /* MineralSolution      */ {"Mineral Solution",     83, false, BlockId::Air},
        /* HealingDraught       */ {"Healing Draught",      84, false, BlockId::Air},
        /* ManaVial             */ {"Mana Vial",            85, false, BlockId::Air},
        /* ElixirOfVigor        */ {"Elixir of Vigor",      86, false, BlockId::Air},
        /* RefinedElixir        */ {"Refined Elixir",       87, false, BlockId::Air},
        /* PhilosophersCatalyst */ {"Philosopher's Catalyst", 88, false, BlockId::Air},
        /* PhilosophersStone    */ {"Philosopher's Stone",  89, false, BlockId::Air},
        /* Conduit              */ {"Conduit",              -1, true,  BlockId::Belt},
        /* WireItem             */ {"Wire",                 -1, true,  BlockId::Wire},
        /* GeneratorItem        */ {"Generator",            -1, true,  BlockId::Generator},
        /* GrinderItem          */ {"Grinder",              -1, true,  BlockId::Grinder},
        /* CauldronItem         */ {"Cauldron",             -1, true,  BlockId::Cauldron},
        /* InfuserItem          */ {"Infuser",              -1, true,  BlockId::Infuser},
        /* AlembicItem          */ {"Alembic",              -1, true,  BlockId::Alembic},
        /* DistillerItem        */ {"Distiller",            -1, true,  BlockId::Distiller},
        /* TransmuterItem       */ {"Transmuter",           -1, true,  BlockId::Transmuter},
        /* MinerItem            */ {"Miner",                -1, true,  BlockId::Miner},
        /* HerbSourceItem       */ {"Herb Source",          -1, true,  BlockId::SourceHerb},
        /* CrystalSourceItem    */ {"Crystal Source",       -1, true,  BlockId::SourceCrystal},
        /* CopperSourceItem     */ {"Copper Source",        -1, true,  BlockId::SourceCopper},
        /* SandSourceItem       */ {"Sand Source",          -1, true,  BlockId::SourceSand},
        /* EssenceSourceItem    */ {"Essence Source",       -1, true,  BlockId::SourceEssence},
        /* Wrench               */ {"Wrench",               78, false, BlockId::Air},
        /* DirtItem             */ {"Dirt",                 -1, true,  BlockId::Dirt},
        /* GrassItem            */ {"Grass",                -1, true,  BlockId::Grass},
        /* ScaffoldItem         */ {"Scaffold",             -1, true,  BlockId::Scaffold},
        /* Wood                 */ {"Wood",                 76, false, BlockId::Air},
        /* SaplingItem          */ {"Sapling",              -1, true,  BlockId::Sapling},
        /* Bucket               */ {"Bucket",               77, false, BlockId::Air},
        /* RainBarrelItem       */ {"Rain Barrel",          -1, true,  BlockId::RainBarrel},
        /* CopperSword          */ {"Copper Sword",         79, false, BlockId::Air},
    }};
}

int iconTile(ItemId id) {
    const ItemInfo& info = itemInfo(id);
    if (info.placeable) {
        return Atlas::tilesForBlock(info.placesBlock).side;
    }
    return info.atlasTile;
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
        case BlockId::EssenceVent: return {ItemId::Essence, 1};
        case BlockId::Stone:       return {ItemId::Stone, 1};
        // Sources -> their placeable item (relocatable).
        case BlockId::SourceHerb:    return {ItemId::HerbSourceItem, 1};
        case BlockId::SourceCrystal: return {ItemId::CrystalSourceItem, 1};
        case BlockId::SourceCopper:  return {ItemId::CopperSourceItem, 1};
        case BlockId::SourceSand:    return {ItemId::SandSourceItem, 1};
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
        // Forestry: logs yield wood; leaves drop nothing here (the chance
        // sapling drop is rolled at the mining site, not in this table).
        case BlockId::Sapling:     return {ItemId::SaplingItem, 1};
        // Wood is fuel AND structure now, so a log splits into two.
        case BlockId::Log:         return {ItemId::Wood, 2};
        case BlockId::RainBarrel:  return {ItemId::RainBarrelItem, 1};
        // Air -> nothing.
        default:                   return {ItemId::None, 0};
    }
}
