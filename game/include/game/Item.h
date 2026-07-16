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
    Count
};

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
};

struct ItemStack {
    ItemId id    = ItemId::None;
    int    count = 0;
};

const ItemInfo& itemInfo(ItemId id);

inline const char* itemName(ItemId id) { return itemInfo(id).name; }

// The atlas tile to draw for this item in UI. Placeable items borrow their
// block's side tile so icons always match the world; materials own an icon.
int iconTile(ItemId id);

// What a block yields when mined ({None,0} if nothing).
ItemStack blockDrop(BlockId id);

// The resource-node block that yields this raw item (Air if `id` is not a
// mineable raw). Used by the Miner's filter: put a raw in its input and it
// mines only that node type.
BlockId nodeForRaw(ItemId id);
