#include "game/Item.h"

#include "game/Atlas.h"

#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

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
        {.id = I::None, .key = "core:none",
         .name = "-"},
        // Raw materials (mined from world nodes).
        {.id = I::Stone, .key = "core:stone",
         .name = "Stone", .atlasTile = 64},
        {.id = I::CopperOre, .key = "core:copper_ore",
         .name = "Copper Ore", .atlasTile = 65, .nodeBlock = B::CopperOre},
        {.id = I::Sand, .key = "core:sand",
         .name = "Sand", .atlasTile = 66, .nodeBlock = B::SandNode},
        {.id = I::Herb, .key = "core:herb",
         .name = "Herb", .atlasTile = 67, .nodeBlock = B::HerbBush},
        {.id = I::Crystal, .key = "core:crystal",
         .name = "Crystal", .atlasTile = 68, .nodeBlock = B::CrystalNode},
        {.id = I::SpringWater, .key = "core:spring_water",
         .name = "Rain Water", .atlasTile = 69},
        {.id = I::Essence, .key = "core:essence",
         .name = "Essence", .atlasTile = 70, .nodeBlock = B::EssenceVent},
        // Equipment intermediates (hand-crafted).
        {.id = I::CopperIngot, .key = "core:copper_ingot",
         .name = "Copper Ingot", .atlasTile = 71},
        {.id = I::CopperPlate, .key = "core:copper_plate",
         .name = "Copper Plate", .atlasTile = 72},
        {.id = I::Glass, .key = "core:glass",
         .name = "Glass", .atlasTile = 73},
        {.id = I::Vial, .key = "core:vial",
         .name = "Vial", .atlasTile = 74},
        {.id = I::MachineFrame, .key = "core:machine_frame",
         .name = "Machine Frame", .atlasTile = 75},
        // Alchemy intermediates (machine-processed).
        {.id = I::GroundHerb, .key = "core:ground_herb",
         .name = "Ground Herb", .atlasTile = 80},
        {.id = I::CrystalDust, .key = "core:crystal_dust",
         .name = "Crystal Dust", .atlasTile = 81},
        {.id = I::HerbalTincture, .key = "core:herbal_tincture",
         .name = "Herbal Tincture", .atlasTile = 82},
        {.id = I::MineralSolution, .key = "core:mineral_solution",
         .name = "Mineral Solution", .atlasTile = 83},
        // Products.
        {.id = I::HealingDraught, .key = "core:healing_draught",
         .name = "Healing Draught", .atlasTile = 84},
        {.id = I::ManaVial, .key = "core:mana_vial",
         .name = "Mana Vial", .atlasTile = 85},
        {.id = I::ElixirOfVigor, .key = "core:elixir_of_vigor",
         .name = "Elixir of Vigor", .atlasTile = 86},
        {.id = I::RefinedElixir, .key = "core:refined_elixir",
         .name = "Refined Elixir", .atlasTile = 87},
        {.id = I::PhilosophersCatalyst, .key = "core:philosophers_catalyst",
         .name = "Philosopher's Catalyst", .atlasTile = 88},
        {.id = I::PhilosophersStone, .key = "core:philosophers_stone",
         .name = "Philosopher's Stone", .atlasTile = 89},
        // Placeables (each places a block).
        {.id = I::Conduit, .key = "core:conduit",
         .name = "Conduit", .placeable = true, .placesBlock = B::Belt},
        {.id = I::WireItem, .key = "core:wire_item",
         .name = "Wire", .placeable = true, .placesBlock = B::Wire},
        {.id = I::GeneratorItem, .key = "core:generator_item",
         .name = "Generator", .placeable = true, .placesBlock = B::Generator},
        {.id = I::GrinderItem, .key = "core:grinder_item",
         .name = "Grinder", .placeable = true, .placesBlock = B::Grinder},
        {.id = I::CauldronItem, .key = "core:cauldron_item",
         .name = "Cauldron", .placeable = true, .placesBlock = B::Cauldron},
        {.id = I::InfuserItem, .key = "core:infuser_item",
         .name = "Infuser", .placeable = true, .placesBlock = B::Infuser},
        {.id = I::AlembicItem, .key = "core:alembic_item",
         .name = "Alembic", .placeable = true, .placesBlock = B::Alembic},
        {.id = I::DistillerItem, .key = "core:distiller_item",
         .name = "Distiller", .placeable = true, .placesBlock = B::Distiller},
        {.id = I::TransmuterItem, .key = "core:transmuter_item",
         .name = "Transmuter", .placeable = true, .placesBlock = B::Transmuter},
        {.id = I::MinerItem, .key = "core:miner_item",
         .name = "Miner", .placeable = true, .placesBlock = B::Miner},
        // Placeable resource sources (relocatable / end-game craftable).
        {.id = I::HerbSourceItem, .key = "core:herb_source_item",
         .name = "Herb Source", .placeable = true, .placesBlock = B::SourceHerb},
        {.id = I::CrystalSourceItem, .key = "core:crystal_source_item",
         .name = "Crystal Source", .placeable = true, .placesBlock = B::SourceCrystal},
        {.id = I::CopperSourceItem, .key = "core:copper_source_item",
         .name = "Copper Source", .placeable = true, .placesBlock = B::SourceCopper},
        {.id = I::SandSourceItem, .key = "core:sand_source_item",
         .name = "Sand Source", .placeable = true, .placesBlock = B::SourceSand},
        {.id = I::EssenceSourceItem, .key = "core:essence_source_item",
         .name = "Essence Source", .placeable = true, .placesBlock = B::SourceEssence},
        // Tools.
        {.id = I::Wrench, .key = "core:wrench",
         .name = "Wrench", .atlasTile = 78},
        // Collected terrain (placeable back; conserves the island's material).
        {.id = I::DirtItem, .key = "core:dirt_item",
         .name = "Dirt", .placeable = true, .placesBlock = B::Dirt},
        {.id = I::GrassItem, .key = "core:grass_item",
         .name = "Grass", .placeable = true, .placesBlock = B::Grass},
        // Structural.
        {.id = I::ScaffoldItem, .key = "core:scaffold_item",
         .name = "Scaffold", .placeable = true, .placesBlock = B::Scaffold},
        // Forestry (wood is the raw; saplings replant).
        {.id = I::Wood, .key = "core:wood",
         .name = "Wood", .atlasTile = 76},
        {.id = I::SaplingItem, .key = "core:sapling_item",
         .name = "Sapling", .placeable = true, .placesBlock = B::Sapling},
        {.id = I::Bucket, .key = "core:bucket",
         .name = "Bucket", .atlasTile = 77},
        // Rain collection (rain is the only water).
        {.id = I::RainBarrelItem, .key = "core:rain_barrel_item",
         .name = "Rain Barrel", .placeable = true, .placesBlock = B::RainBarrel},
        // Combat.
        {.id = I::CopperSword, .key = "core:copper_sword",
         .name = "Copper Sword", .atlasTile = 79,
         .weaponDamage = 2.0f},
        // Boss tier.
        {.id = I::TeleportKey, .key = "core:teleport_key",
         .name = "Teleport Key", .atlasTile = 90},
        {.id = I::VoidCatalyst, .key = "core:void_catalyst",
         .name = "Void Catalyst", .atlasTile = 91},
        {.id = I::StormKey, .key = "core:storm_key",
         .name = "Storm Key", .atlasTile = 92},
        {.id = I::StormCore, .key = "core:storm_core",
         .name = "Storm Core", .atlasTile = 93},
        // Source fusion. Resonance is Miner-harvestable (nodeBlock); the
        // source item relocates the hybrid (borrows its block side tile); the
        // catalyst is a consumed tool.
        {.id = I::Resonance, .key = "core:resonance",
         .name = "Resonance", .atlasTile = 94, .nodeBlock = B::ResonantNode},
        {.id = I::ResonantSourceItem, .key = "core:resonant_source_item",
         .name = "Resonant Source", .placeable = true, .placesBlock = B::ResonantSource},
        {.id = I::FusionCatalyst, .key = "core:fusion_catalyst",
         .name = "Fusion Catalyst", .atlasTile = 95},
        // Mining tools. `tool` is the block class they break; `toolTier` gates
        // which blocks drop; `miningSpeed` divides the block's by-hand hardness,
        // so higher tiers break faster. Copper is the top tier.
        {.id = I::CopperPickaxe, .key = "core:copper_pickaxe",
         .name = "Copper Pickaxe", .atlasTile = 96,
         .tool = ToolType::Pickaxe, .toolTier = kTierCopper, .miningSpeed = 8.0f},
        {.id = I::CopperAxe, .key = "core:copper_axe",
         .name = "Copper Axe", .atlasTile = 97,
         .tool = ToolType::Axe, .toolTier = kTierCopper, .miningSpeed = 8.0f},
        {.id = I::CopperShovel, .key = "core:copper_shovel",
         .name = "Copper Shovel", .atlasTile = 98,
         .tool = ToolType::Shovel, .toolTier = kTierCopper, .miningSpeed = 8.0f},
        // Early grind: hand-gathered bits + the Wood and Stone tiers.
        {.id = I::Stick, .key = "core:stick",
         .name = "Stick", .atlasTile = 99},
        {.id = I::Pebble, .key = "core:pebble",
         .name = "Pebble", .atlasTile = 100},
        {.id = I::WoodPickaxe, .key = "core:wood_pickaxe",
         .name = "Wood Pickaxe", .atlasTile = 101,
         .tool = ToolType::Pickaxe, .toolTier = kTierWood, .miningSpeed = 4.0f},
        {.id = I::WoodAxe, .key = "core:wood_axe",
         .name = "Wood Axe", .atlasTile = 102,
         .tool = ToolType::Axe, .toolTier = kTierWood, .miningSpeed = 4.0f},
        {.id = I::StonePickaxe, .key = "core:stone_pickaxe",
         .name = "Stone Pickaxe", .atlasTile = 103,
         .tool = ToolType::Pickaxe, .toolTier = kTierStone, .miningSpeed = 6.0f},
        {.id = I::StoneAxe, .key = "core:stone_axe",
         .name = "Stone Axe", .atlasTile = 104,
         .tool = ToolType::Axe, .toolTier = kTierStone, .miningSpeed = 6.0f},
        {.id = I::StoneShovel, .key = "core:stone_shovel",
         .name = "Stone Shovel", .atlasTile = 105,
         .tool = ToolType::Shovel, .toolTier = kTierStone, .miningSpeed = 6.0f},
        // The Composter machine (renewable dirt).
        {.id = I::ComposterItem, .key = "core:composter_item",
         .name = "Composter", .placeable = true, .placesBlock = B::Composter},
        // Combat armor. `armorSlot` picks the equip slot; `armor` is the flat
        // combat damage reduction (fraction). Icons live in atlas tiles 106+
        // (see assets/ATLAS.md). The Copper set totals 0.30; the boss-gated
        // Aegis set totals 0.50.
        {.id = I::CopperHelm, .key = "core:copper_helm",
          .name = "Copper Helm",  .atlasTile = 106,
         .armorSlot = ArmorSlot::Head, .armor = 0.08f},
        {.id = I::CopperChest, .key = "core:copper_chest",
         .name = "Copper Chestplate", .atlasTile = 107,
         .armorSlot = ArmorSlot::Body, .armor = 0.16f},
        {.id = I::CopperBoots, .key = "core:copper_boots",
         .name = "Copper Boots", .atlasTile = 108,
         .armorSlot = ArmorSlot::Feet, .armor = 0.06f},
        {.id = I::AegisHelm, .key = "core:aegis_helm",
           .name = "Aegis Helm",   .atlasTile = 109,
         .armorSlot = ArmorSlot::Head, .armor = 0.14f},
        {.id = I::AegisChest, .key = "core:aegis_chest",
          .name = "Aegis Chestplate", .atlasTile = 110,
         .armorSlot = ArmorSlot::Body, .armor = 0.26f},
        {.id = I::AegisBoots, .key = "core:aegis_boots",
          .name = "Aegis Boots",  .atlasTile = 111,
         .armorSlot = ArmorSlot::Feet, .armor = 0.10f},
        // The Forge machine (block-crafts weapons/armor).
        {.id = I::ForgeItem, .key = "core:forge_item",
         .name = "Forge", .placeable = true, .placesBlock = B::Forge},
        // The shared parts tier, all Press-made: rods draw from ingots, gears
        // from rods, casings from plates, etched plates from plates + crystal
        // dust. The three converge into the Machine Frame. Icons at 112+.
        {.id = I::CopperRod, .key = "core:copper_rod",
         .name = "Copper Rod", .atlasTile = 112},
        {.id = I::Gear, .key = "core:gear",
         .name = "Gear", .atlasTile = 113},
        {.id = I::MachineCasing, .key = "core:machine_casing",
         .name = "Machine Casing", .atlasTile = 114},
        {.id = I::EtchedPlate, .key = "core:etched_plate",
         .name = "Etched Plate", .atlasTile = 115},
        // The Press machine (forms the parts tier + assembles the frame).
        {.id = I::PressItem, .key = "core:press_item",
         .name = "Press", .placeable = true, .placesBlock = B::Press},
        // The Alchemy Circle multiblock (placeables borrow their block's side
        // tile for the icon, so these need no atlasTile of their own).
        {.id = I::RuneCoreItem, .key = "core:rune_core_item",
         .name = "Rune Core", .placeable = true,
         .placesBlock = B::RuneCore},
        {.id = I::PedestalItem, .key = "core:pedestal_item",
         .name = "Pedestal", .placeable = true,
         .placesBlock = B::Pedestal},
        // ---- The recipe overhaul ----------------------------------------
        // Charcoal: the Furnace's own product and the best fuel in kFuelSeed, so
        // the first thing a new Bloomery does is make its own better fuel.
        {.id = I::Charcoal, .key = "core:charcoal",
         .name = "Charcoal", .atlasTile = 181},
        // Sifted from sand. Four nuggets smelt into one ingot.
        {.id = I::IronNugget, .key = "core:iron_nugget",
         .name = "Iron Nugget", .atlasTile = 176},
        {.id = I::CopperNugget, .key = "core:copper_nugget",
         .name = "Copper Nugget", .atlasTile = 177},
        // Iron: the structural metal, and the tool/armor tier above copper.
        {.id = I::IronIngot, .key = "core:iron_ingot",
         .name = "Iron Ingot", .atlasTile = 178},
        {.id = I::IronPlate, .key = "core:iron_plate",
         .name = "Iron Plate", .atlasTile = 179},
        {.id = I::IronRod, .key = "core:iron_rod",
         .name = "Iron Rod", .atlasTile = 180},
        {.id = I::IronPickaxe, .key = "core:iron_pickaxe",
         .name = "Iron Pickaxe", .atlasTile = 182,
         .tool = ToolType::Pickaxe, .toolTier = kTierIron, .miningSpeed = 12.0f},
        {.id = I::IronAxe, .key = "core:iron_axe",
         .name = "Iron Axe", .atlasTile = 183,
         .tool = ToolType::Axe, .toolTier = kTierIron, .miningSpeed = 12.0f},
        {.id = I::IronShovel, .key = "core:iron_shovel",
         .name = "Iron Shovel", .atlasTile = 184,
         .tool = ToolType::Shovel, .toolTier = kTierIron, .miningSpeed = 12.0f},
        {.id = I::IronSword, .key = "core:iron_sword",
         .name = "Iron Sword", .atlasTile = 185,
         .weaponDamage = 3.0f},
        // The iron armor set totals 0.40 -- between Copper (0.30) and the
        // boss-gated Aegis (0.50), so it is the set you EARN rather than loot.
        {.id = I::IronHelm, .key = "core:iron_helm",
          .name = "Iron Helm",  .atlasTile = 186,
         .armorSlot = ArmorSlot::Head, .armor = 0.11f},
        {.id = I::IronChest, .key = "core:iron_chest",
         .name = "Iron Chestplate", .atlasTile = 187,
         .armorSlot = ArmorSlot::Body, .armor = 0.21f},
        {.id = I::IronBoots, .key = "core:iron_boots",
         .name = "Iron Boots", .atlasTile = 188,
         .armorSlot = ArmorSlot::Feet, .armor = 0.08f},
        // The four new powered machines (placeables borrow their block's side
        // tile for the icon, so none of these need an atlasTile).
        {.id = I::FurnaceItem, .key = "core:furnace_item",
         .name = "Furnace", .placeable = true, .placesBlock = B::Furnace},
        {.id = I::SifterItem, .key = "core:sifter_item",
         .name = "Sifter", .placeable = true, .placesBlock = B::Sifter},
        {.id = I::GlassblowerItem, .key = "core:glassblower_item",
         .name = "Glassblower", .placeable = true,
         .placesBlock = B::Glassblower},
        {.id = I::CompactorItem, .key = "core:compactor_item",
         .name = "Compactor", .placeable = true,
         .placesBlock = B::Compactor},
        // The manual tier: one hand-cranked twin per Processor. Each runs its
        // powered counterpart's recipes (MachineTraits::recipeGroup) at
        // MachineTraits::speedMult, and asks for no power at all.
        {.id = I::BloomeryItem, .key = "core:bloomery_item",
         .name = "Bloomery", .placeable = true, .placesBlock = B::Bloomery},
        {.id = I::SieveItem, .key = "core:sieve_item",
         .name = "Sieve", .placeable = true, .placesBlock = B::Sieve},
        {.id = I::BlowpipeItem, .key = "core:blowpipe_item",
         .name = "Blowpipe", .placeable = true, .placesBlock = B::Blowpipe},
        {.id = I::TamperItem, .key = "core:tamper_item",
         .name = "Tamper", .placeable = true, .placesBlock = B::Tamper},
        {.id = I::MortarItem, .key = "core:mortar_item",
         .name = "Mortar", .placeable = true, .placesBlock = B::Mortar},
        {.id = I::HandPressItem, .key = "core:hand_press_item",
         .name = "Hand Press", .placeable = true,
         .placesBlock = B::HandPress},
        {.id = I::AnvilItem, .key = "core:anvil_item",
         .name = "Anvil", .placeable = true, .placesBlock = B::Anvil},
        {.id = I::CompostHeapItem, .key = "core:compost_heap_item",
         .name = "Compost Heap", .placeable = true,
         .placesBlock = B::CompostHeap},
        {.id = I::MixingBowlItem, .key = "core:mixing_bowl_item",
         .name = "Mixing Bowl", .placeable = true,
         .placesBlock = B::MixingBowl},
        {.id = I::InfusionStandItem, .key = "core:infusion_stand_item",
         .name = "Infusion Stand", .placeable = true,
         .placesBlock = B::InfusionStand},
        {.id = I::StillItem, .key = "core:still_item",
         .name = "Still", .placeable = true, .placesBlock = B::Still},
        {.id = I::HandDistillerItem, .key = "core:hand_distiller_item",
         .name = "Hand Distiller", .placeable = true,
         .placesBlock = B::HandDistiller},
        {.id = I::HandTransmuterItem, .key = "core:hand_transmuter_item",
         .name = "Hand Transmuter", .placeable = true,
         .placesBlock = B::HandTransmuter},
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

    // See the matching assert in Block.cpp: a duplicate key silently aliases
    // two items everywhere identity is resolved by key rather than ordinal.
    static_assert([] {
        for (std::size_t i = 0; i < std::size(kItems); ++i) {
            const std::string_view a{kItems[i].key};
            if (a.empty()) return false;
            for (std::size_t j = i + 1; j < std::size(kItems); ++j) {
                if (a == std::string_view{kItems[j].key}) return false;
            }
        }
        return true;
    }(), "kItems keys must be unique and non-empty");

    // Seeded from kItems, grown by content packs -- the blockTable() note in
    // Block.cpp applies here word for word.
    std::vector<ItemInfo>& itemTable() {
        static std::vector<ItemInfo> table(std::begin(kItems), std::end(kItems));
        return table;
    }

} // namespace

const ItemInfo& itemInfo(ItemId id) {
    return itemTable()[static_cast<std::size_t>(id)];
}

std::size_t itemCount() {
    return itemTable().size();
}

const std::vector<ItemInfo>& itemRows() {
    return itemTable();
}

void restoreItems(std::vector<ItemInfo> rows) {
    itemTable() = std::move(rows);
}

ItemId addItem(const ItemInfo& row) {
    const auto id = static_cast<ItemId>(itemTable().size());
    itemTable().push_back(row);
    itemTable().back().id = id;
    return id;
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
