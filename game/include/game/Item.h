#pragma once

#include "game/Block.h"

#include <cstdint>

// Things the player can hold in their inventory. Distinct from BlockId: some
// items are placeable (they place a block when used), most are materials.
//
// APPEND-ONLY: the save format serializes inventories by enum index and
// accepts older, shorter item sets, so new items go immediately before Count
// and existing entries never move. The kItems registry in Item.cpp is
// static_asserted against this order.
enum class ItemId : std::uint8_t {
    None = 0,
    // Raw materials (mined from world nodes)
    Stone,
    CopperOre,
    Sand,
    Herb,
    Crystal,
    SpringWater,
    Essence,
    // Equipment intermediates (hand-crafted)
    CopperIngot,
    CopperPlate,
    Glass,
    Vial,
    MachineFrame,
    // Alchemy intermediates (machine-processed)
    GroundHerb,
    CrystalDust,
    HerbalTincture,
    MineralSolution,
    // Products
    HealingDraught,
    ManaVial,
    ElixirOfVigor,
    RefinedElixir,
    PhilosophersCatalyst,
    PhilosophersStone,
    // Placeables (each places a block)
    Conduit,
    WireItem,
    GeneratorItem,
    GrinderItem,
    CauldronItem,
    InfuserItem,
    AlembicItem,
    DistillerItem,
    TransmuterItem,
    MinerItem,
    // Placeable resource sources (relocatable / end-game craftable)
    HerbSourceItem,
    CrystalSourceItem,
    CopperSourceItem,
    SandSourceItem,
    EssenceSourceItem,
    // Tools
    Wrench,
    // Collected terrain (placeable back; conserves the island's material)
    DirtItem,
    GrassItem,
    // Structural
    ScaffoldItem,
    // Forestry (wood is the raw; saplings replant)
    Wood,
    SaplingItem,
    Bucket,
    // Rain collection (rain is the only water)
    RainBarrelItem,
    // Combat (new entries must be APPENDED here: the save format serializes
    // inventories by enum index and accepts older, shorter item sets)
    CopperSword,
    // Boss tier: keys are crafted, consumed tickets to the arena dimension;
    // each boss's unique drop feeds the next tier (the Void Catalyst crafts
    // the Storm Key; the Storm Core awaits the generator-tier line).
    TeleportKey,
    VoidCatalyst,
    StormKey,
    StormCore,
    // Source fusion: the hybrid raw the Resonant Source yields, its
    // relocatable source item, and the consumed catalyst that fuses a pair.
    Resonance,
    ResonantSourceItem,
    FusionCatalyst,
    // Mining tools: gate tough blocks behind the matching tool + tier. A gated
    // block yields nothing unless the held tool is the right class AND at least
    // its tier. The Copper tier tops a Wood -> Stone -> Copper ladder. APPENDED
    // at the enum tail so older saves stay loadable (readInventory accepts
    // shorter item sets).
    CopperPickaxe,
    CopperAxe,
    CopperShovel,
    // Early-game grind: gathered by hand (sticks from leaves, pebbles from
    // sifting dirt/grass) and the two lower tool tiers they build.
    Stick,
    Pebble,
    WoodPickaxe,
    WoodAxe,
    StonePickaxe,
    StoneAxe,
    StoneShovel,
    // The Composter machine (renewable dirt from plant matter).
    ComposterItem,
    // Combat armor: equipped into head/body/feet slots for flat combat damage
    // reduction. The Copper set forges from plates; the Aegis set is gated on
    // the boss drops (Void Catalyst / Storm Core), so gearing up is a
    // fight -> forge -> harder-fight loop. Forged at the Forge machine.
    CopperHelm,
    CopperChest,
    CopperBoots,
    AegisHelm,
    AegisChest,
    AegisBoots,
    ForgeItem,
    Count
};

// The mining-tool classes. A block names the class that breaks it efficiently
// (BlockInfo::tool); an item names the class it IS (ItemInfo::tool). Matching
// the class AND meeting the block's tier makes a break fast and fruitful.
enum class ToolType : std::uint8_t { None, Pickaxe, Axe, Shovel };

// Which equipment slot a piece of armor occupies (ItemInfo::armorSlot). None =
// the item is not armor. The three slots index the player's m_armor array.
enum class ArmorSlot : std::uint8_t { None, Head, Body, Feet };
inline constexpr int kArmorSlots = 3; // head, body, feet

// Tool tiers, low to high. A block's required tier (BlockInfo::toolTier) gates
// its drop; a tool's tier (ItemInfo::toolTier) must meet it. 0 = no tool / not
// a tool (soft blocks, ungated).
inline constexpr int kTierWood   = 1;
inline constexpr int kTierStone  = 2;
inline constexpr int kTierCopper = 3;

// The player's hotbar is a fixed strip of assigned slots (ItemId::None = an
// empty slot). Lives here rather than in game internals because the save
// format (SaveSystem.h) serializes the slots.
inline constexpr int kHotbarSlots = 10;

struct ItemInfo {
    ItemId      id;                        // must equal the row's position (static_asserted)
    const char* name = "-";
    int         atlasTile = -1;            // icon tile for materials; -1 for placeables (see iconTile)
    bool        placeable = false;         // can it be placed in the world?
    BlockId     placesBlock = BlockId::Air; // which block it places
    BlockId     nodeBlock = BlockId::Air;  // the node that yields this raw (see nodeForRaw)
    ToolType    tool = ToolType::None;     // the mining-tool class this item IS (None = not a tool)
    int         toolTier = 0;              // tool tier (kTierWood/Stone/Copper); 0 = not a tool
    float       miningSpeed = 1.0f;        // break-speed divisor vs. the block's by-hand hardness
    ArmorSlot   armorSlot = ArmorSlot::None; // equip slot (None = not armor)
    float       armor = 0.0f;              // flat combat damage reduction, 0..1 (armor only)
};

struct ItemStack {
    ItemId id    = ItemId::None;
    int    count = 0;
};

const ItemInfo& itemInfo(ItemId id);

inline const char* itemName(ItemId id) { return itemInfo(id).name; }
inline ToolType    itemTool(ItemId id)        { return itemInfo(id).tool; }
inline int         itemTier(ItemId id)        { return itemInfo(id).toolTier; }
inline float       itemMiningSpeed(ItemId id) { return itemInfo(id).miningSpeed; }
inline ArmorSlot   itemArmorSlot(ItemId id)   { return itemInfo(id).armorSlot; }
inline float       itemArmor(ItemId id)       { return itemInfo(id).armor; }

// The atlas tile to draw for this item in UI. Placeable items borrow their
// block's side tile so icons always match the world; materials own an icon.
int iconTile(ItemId id);

// What a block yields when mined ({None,0} if nothing).
ItemStack blockDrop(BlockId id);

// The resource-node block that yields this raw item (Air if `id` is not a
// mineable raw). Used by the Miner's filter: put a raw in its input and it
// mines only that node type.
BlockId nodeForRaw(ItemId id);
