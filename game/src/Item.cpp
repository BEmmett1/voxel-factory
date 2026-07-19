#include "game/Item.h"

#include "game/Atlas.h"

#include <cstddef>
#include <iterator>

namespace {

    using B = BlockId;
    using I = ItemId;

    // One row per ItemId, in enum order — static_asserted below, so a missing
    // or misplaced row is a compile error. Omitted fields take ItemInfo's
    // defaults (not placeable, no icon tile, no node).
    // Material icons live in atlas rows 4-5 (tiles 64+; see assets/ATLAS.md).
    // Placeables keep atlasTile = -1: their icon is the placed block's side
    // tile (see iconTile). Raws that a Miner can target carry the node block
    // that yields them in nodeBlock.
    constexpr ItemInfo kItems[] = {
        {.id = I::None, .name = "-"},
        // Raw materials (mined from world nodes).
        {.id = I::Stone, .name = "Stone", .atlasTile = 64},
        {.id = I::CopperOre, .name = "Copper Ore", .atlasTile = 65, .nodeBlock = B::CopperOre},
        {.id = I::Sand, .name = "Sand", .atlasTile = 66, .nodeBlock = B::SandNode},
        {.id = I::Herb, .name = "Herb", .atlasTile = 67, .nodeBlock = B::HerbBush},
        {.id = I::Crystal, .name = "Crystal", .atlasTile = 68, .nodeBlock = B::CrystalNode},
        {.id = I::SpringWater, .name = "Rain Water", .atlasTile = 69},
        {.id = I::Essence, .name = "Essence", .atlasTile = 70, .nodeBlock = B::EssenceVent},
        // Equipment intermediates (hand-crafted).
        {.id = I::CopperIngot, .name = "Copper Ingot", .atlasTile = 71},
        {.id = I::CopperPlate, .name = "Copper Plate", .atlasTile = 72},
        {.id = I::Glass, .name = "Glass", .atlasTile = 73},
        {.id = I::Vial, .name = "Vial", .atlasTile = 74},
        {.id = I::MachineFrame, .name = "Machine Frame", .atlasTile = 75},
        // Alchemy intermediates (machine-processed).
        {.id = I::GroundHerb, .name = "Ground Herb", .atlasTile = 80},
        {.id = I::CrystalDust, .name = "Crystal Dust", .atlasTile = 81},
        {.id = I::HerbalTincture, .name = "Herbal Tincture", .atlasTile = 82},
        {.id = I::MineralSolution, .name = "Mineral Solution", .atlasTile = 83},
        // Products.
        {.id = I::HealingDraught, .name = "Healing Draught", .atlasTile = 84},
        {.id = I::ManaVial, .name = "Mana Vial", .atlasTile = 85},
        {.id = I::ElixirOfVigor, .name = "Elixir of Vigor", .atlasTile = 86},
        {.id = I::RefinedElixir, .name = "Refined Elixir", .atlasTile = 87},
        {.id = I::PhilosophersCatalyst, .name = "Philosopher's Catalyst", .atlasTile = 88},
        {.id = I::PhilosophersStone, .name = "Philosopher's Stone", .atlasTile = 89},
        // Placeables (each places a block).
        {.id = I::Conduit, .name = "Conduit", .placeable = true, .placesBlock = B::Belt},
        {.id = I::WireItem, .name = "Wire", .placeable = true, .placesBlock = B::Wire},
        {.id = I::GeneratorItem, .name = "Generator", .placeable = true, .placesBlock = B::Generator},
        {.id = I::GrinderItem, .name = "Grinder", .placeable = true, .placesBlock = B::Grinder},
        {.id = I::CauldronItem, .name = "Cauldron", .placeable = true, .placesBlock = B::Cauldron},
        {.id = I::InfuserItem, .name = "Infuser", .placeable = true, .placesBlock = B::Infuser},
        {.id = I::AlembicItem, .name = "Alembic", .placeable = true, .placesBlock = B::Alembic},
        {.id = I::DistillerItem, .name = "Distiller", .placeable = true, .placesBlock = B::Distiller},
        {.id = I::TransmuterItem, .name = "Transmuter", .placeable = true, .placesBlock = B::Transmuter},
        {.id = I::MinerItem, .name = "Miner", .placeable = true, .placesBlock = B::Miner},
        // Placeable resource sources (relocatable / end-game craftable).
        {.id = I::HerbSourceItem, .name = "Herb Source", .placeable = true, .placesBlock = B::SourceHerb},
        {.id = I::CrystalSourceItem, .name = "Crystal Source", .placeable = true, .placesBlock = B::SourceCrystal},
        {.id = I::CopperSourceItem, .name = "Copper Source", .placeable = true, .placesBlock = B::SourceCopper},
        {.id = I::SandSourceItem, .name = "Sand Source", .placeable = true, .placesBlock = B::SourceSand},
        {.id = I::EssenceSourceItem, .name = "Essence Source", .placeable = true, .placesBlock = B::SourceEssence},
        // Tools.
        {.id = I::Wrench, .name = "Wrench", .atlasTile = 78},
        // Collected terrain (placeable back; conserves the island's material).
        {.id = I::DirtItem, .name = "Dirt", .placeable = true, .placesBlock = B::Dirt},
        {.id = I::GrassItem, .name = "Grass", .placeable = true, .placesBlock = B::Grass},
        // Structural.
        {.id = I::ScaffoldItem, .name = "Scaffold", .placeable = true, .placesBlock = B::Scaffold},
        // Forestry (wood is the raw; saplings replant).
        {.id = I::Wood, .name = "Wood", .atlasTile = 76},
        {.id = I::SaplingItem, .name = "Sapling", .placeable = true, .placesBlock = B::Sapling},
        {.id = I::Bucket, .name = "Bucket", .atlasTile = 77},
        // Rain collection (rain is the only water).
        {.id = I::RainBarrelItem, .name = "Rain Barrel", .placeable = true, .placesBlock = B::RainBarrel},
        // Combat.
        {.id = I::CopperSword, .name = "Copper Sword", .atlasTile = 79},
        // Boss tier.
        {.id = I::TeleportKey, .name = "Teleport Key", .atlasTile = 90},
        {.id = I::VoidCatalyst, .name = "Void Catalyst", .atlasTile = 91},
    };

    static_assert(std::size(kItems) == static_cast<std::size_t>(ItemId::Count),
                  "kItems needs exactly one row per ItemId");

    constexpr bool itemsInEnumOrder() {
        for (std::size_t i = 0; i < std::size(kItems); ++i) {
            if (kItems[i].id != static_cast<ItemId>(i)) return false;
        }
        return true;
    }
    static_assert(itemsInEnumOrder(), "kItems rows must be in ItemId enum order");

} // namespace

const ItemInfo& itemInfo(ItemId id) {
    return kItems[static_cast<std::size_t>(id)];
}

int iconTile(ItemId id) {
    const ItemInfo& info = itemInfo(id);
    if (info.placeable) {
        return Atlas::tilesForBlock(info.placesBlock).side;
    }
    return info.atlasTile;
}

BlockId nodeForRaw(ItemId id) {
    return itemInfo(id).nodeBlock;
}

ItemStack blockDrop(BlockId id) {
    const BlockDrop& d = blockInfo(id).drop;
    return {d.item, d.count};
}
