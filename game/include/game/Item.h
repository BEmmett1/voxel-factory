#pragma once

#include "game/Block.h"

#include <cstdint>

// Things the player can hold in their inventory. Distinct from BlockId: some
// items are placeable (they place a block when used), most are materials.
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
    MinerItem,
    Count
};

struct ItemInfo {
    const char* name;
    int         atlasTile;   // icon tile in the texture atlas (used by the HUD later)
    bool        placeable;   // can it be placed in the world?
    BlockId     placesBlock; // which block it places (Air if not placeable)
};

struct ItemStack {
    ItemId id    = ItemId::None;
    int    count = 0;
};

const ItemInfo& itemInfo(ItemId id);

inline const char* itemName(ItemId id) { return itemInfo(id).name; }

// What a block yields when mined ({None,0} if nothing).
ItemStack blockDrop(BlockId id);
