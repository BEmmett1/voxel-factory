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
        {.id = I::StormKey, .name = "Storm Key", .atlasTile = 92},
        {.id = I::StormCore, .name = "Storm Core", .atlasTile = 93},
        // Source fusion. Resonance is Miner-harvestable (nodeBlock); the
        // source item relocates the hybrid (borrows its block side tile); the
        // catalyst is a consumed tool.
        {.id = I::Resonance, .name = "Resonance", .atlasTile = 94, .nodeBlock = B::ResonantNode},
        {.id = I::ResonantSourceItem, .name = "Resonant Source", .placeable = true, .placesBlock = B::ResonantSource},
        {.id = I::FusionCatalyst, .name = "Fusion Catalyst", .atlasTile = 95},
        // Mining tools. `tool` is the block class they break; `toolTier` gates
        // which blocks drop; `miningSpeed` divides the block's by-hand hardness,
        // so higher tiers break faster. Copper is the top tier.
        {.id = I::CopperPickaxe, .name = "Copper Pickaxe", .atlasTile = 96,
         .tool = ToolType::Pickaxe, .toolTier = kTierCopper, .miningSpeed = 8.0f},
        {.id = I::CopperAxe, .name = "Copper Axe", .atlasTile = 97,
         .tool = ToolType::Axe, .toolTier = kTierCopper, .miningSpeed = 8.0f},
        {.id = I::CopperShovel, .name = "Copper Shovel", .atlasTile = 98,
         .tool = ToolType::Shovel, .toolTier = kTierCopper, .miningSpeed = 8.0f},
        // Early grind: hand-gathered bits + the Wood and Stone tiers.
        {.id = I::Stick, .name = "Stick", .atlasTile = 99},
        {.id = I::Pebble, .name = "Pebble", .atlasTile = 100},
        {.id = I::WoodPickaxe, .name = "Wood Pickaxe", .atlasTile = 101,
         .tool = ToolType::Pickaxe, .toolTier = kTierWood, .miningSpeed = 4.0f},
        {.id = I::WoodAxe, .name = "Wood Axe", .atlasTile = 102,
         .tool = ToolType::Axe, .toolTier = kTierWood, .miningSpeed = 4.0f},
        {.id = I::StonePickaxe, .name = "Stone Pickaxe", .atlasTile = 103,
         .tool = ToolType::Pickaxe, .toolTier = kTierStone, .miningSpeed = 6.0f},
        {.id = I::StoneAxe, .name = "Stone Axe", .atlasTile = 104,
         .tool = ToolType::Axe, .toolTier = kTierStone, .miningSpeed = 6.0f},
        {.id = I::StoneShovel, .name = "Stone Shovel", .atlasTile = 105,
         .tool = ToolType::Shovel, .toolTier = kTierStone, .miningSpeed = 6.0f},
        // The Composter machine (renewable dirt).
        {.id = I::ComposterItem, .name = "Composter", .placeable = true, .placesBlock = B::Composter},
        // Combat armor. `armorSlot` picks the equip slot; `armor` is the flat
        // combat damage reduction (fraction). Icons live in atlas tiles 106+
        // (see assets/ATLAS.md). The Copper set totals 0.30; the boss-gated
        // Aegis set totals 0.50.
        {.id = I::CopperHelm,  .name = "Copper Helm",  .atlasTile = 106,
         .armorSlot = ArmorSlot::Head, .armor = 0.08f},
        {.id = I::CopperChest, .name = "Copper Chestplate", .atlasTile = 107,
         .armorSlot = ArmorSlot::Body, .armor = 0.16f},
        {.id = I::CopperBoots, .name = "Copper Boots", .atlasTile = 108,
         .armorSlot = ArmorSlot::Feet, .armor = 0.06f},
        {.id = I::AegisHelm,   .name = "Aegis Helm",   .atlasTile = 109,
         .armorSlot = ArmorSlot::Head, .armor = 0.14f},
        {.id = I::AegisChest,  .name = "Aegis Chestplate", .atlasTile = 110,
         .armorSlot = ArmorSlot::Body, .armor = 0.26f},
        {.id = I::AegisBoots,  .name = "Aegis Boots",  .atlasTile = 111,
         .armorSlot = ArmorSlot::Feet, .armor = 0.10f},
        // The Forge machine (block-crafts weapons/armor).
        {.id = I::ForgeItem, .name = "Forge", .placeable = true, .placesBlock = B::Forge},
        // The shared parts tier, all Press-made: rods draw from ingots, gears
        // from rods, casings from plates, etched plates from plates + crystal
        // dust. The three converge into the Machine Frame. Icons at 112+.
        {.id = I::CopperRod, .name = "Copper Rod", .atlasTile = 112},
        {.id = I::Gear, .name = "Gear", .atlasTile = 113},
        {.id = I::MachineCasing, .name = "Machine Casing", .atlasTile = 114},
        {.id = I::EtchedPlate, .name = "Etched Plate", .atlasTile = 115},
        // The Press machine (forms the parts tier + assembles the frame).
        {.id = I::PressItem, .name = "Press", .placeable = true, .placesBlock = B::Press},
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
