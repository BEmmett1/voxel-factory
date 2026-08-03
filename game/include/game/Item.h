#pragma once

#include "game/Block.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

// Things the player can hold in their inventory. Distinct from BlockId: some
// items are placeable (they place a block when used), most are materials.
//
// Ordinals are an ENCODING, not an identity -- see the matching note on
// BlockId. An inventory is still serialized by enum position, but a save now
// carries the key table those positions refer to (v22), so an item that moves
// is translated on load instead of silently becoming its neighbour. The old
// APPEND-ONLY rule is retired; the kItems registry is still static_asserted
// against this order, and keys must stay unique and stable.
enum class ItemId : std::uint16_t {
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
    // The shared parts tier. Every machine now reaches MachineFrame through
    // these, and every one of them comes out of the Press -- the tech tree
    // deepens through automation rather than through the hand-craft menu.
    CopperRod,
    Gear,
    MachineCasing,
    EtchedPlate,
    PressItem,
    // The Alchemy Circle's own two parts. These stay hand-craftable on
    // purpose: the Circle is what most hand recipes moved ONTO, so it must be
    // reachable from the survival tier or the tech tree deadlocks.
    RuneCoreItem,
    PedestalItem,
    // ---- The recipe overhaul: smelting, sifting, and iron ----
    // Charcoal is the second fuel (see kFuels in Machine.h) and the reason a
    // Furnace burns wood into something rather than just burning wood.
    Charcoal,
    // Sifted from sand. Nuggets are the ONLY route to iron, which makes the
    // Sifter permanent rather than an early-game curiosity: sand throughput is
    // the ceiling on how fast the factory can build more factory.
    IronNugget,
    CopperNugget,
    // Iron is the STRUCTURAL metal -- casings, gears, frames, and the tool and
    // armor tier above copper. Copper keeps electricity (wire, etched plates).
    IronIngot,
    IronPlate,
    IronRod,
    IronPickaxe,
    IronAxe,
    IronShovel,
    IronSword,
    IronHelm,
    IronChest,
    IronBoots,
    // The four machines the overhaul added, and the thirteen hand-cranked
    // twins that let you run every one of them before electricity.
    FurnaceItem,
    SifterItem,
    GlassblowerItem,
    CompactorItem,
    BloomeryItem,
    SieveItem,
    BlowpipeItem,
    TamperItem,
    MortarItem,
    HandPressItem,
    AnvilItem,
    CompostHeapItem,
    MixingBowlItem,
    InfusionStandItem,
    StillItem,
    HandDistillerItem,
    HandTransmuterItem,
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

// Enum spellings for the content pack format (ContentPack.h). One table read
// by both the writer and the reader, so the two cannot drift -- and adding an
// enumerator without naming it here is a compile error.
inline constexpr const char* kToolNames[] = {"none", "pickaxe", "axe", "shovel"};
inline constexpr const char* kArmorSlotNames[] = {"none", "head", "body", "feet"};
static_assert(std::size(kToolNames) == 4 && std::size(kArmorSlotNames) == 4,
              "tool/armor-slot names must cover every enumerator");

// Tool tiers, low to high. A block's required tier (BlockInfo::toolTier) gates
// its drop; a tool's tier (ItemInfo::toolTier) must meet it. 0 = no tool / not
// a tool (soft blocks, ungated).
inline constexpr int kTierWood   = 1;
inline constexpr int kTierStone  = 2;
inline constexpr int kTierCopper = 3;
inline constexpr int kTierIron   = 4;

// The player's hotbar is a fixed strip of assigned slots (ItemId::None = an
// empty slot). Lives here rather than in game internals because the save
// format (SaveSystem.h) serializes the slots.
inline constexpr int kHotbarSlots = 10;

struct ItemInfo {
    ItemId      id;                        // must equal the row's position (static_asserted)
    // Stable identity, independent of the ordinal and the display name --
    // see the matching field on BlockInfo (Block.h) for why. Unique within
    // kItems (static_asserted).
    const char* key = "";
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
    // Melee damage per swing, in creature hearts. 0 = not a weapon. A weapon
    // never mines: LMB swings it. Data rather than an ItemId check, so a new
    // sword tier is a registry row like everything else.
    float       weaponDamage = 0.0f;
};

struct ItemStack {
    ItemId id    = ItemId::None;
    int    count = 0;
};

const ItemInfo& itemInfo(ItemId id);

// The item registry is a RUNTIME table, exactly like the block one -- see the
// note on blockCount() in Block.h for what that costs and what it buys.
// `ItemId::Count` is the number of items COMPILED IN (and the "no such item"
// sentinel); itemCount() is how many there are.
std::size_t itemCount();
const std::vector<ItemInfo>& itemRows();
ItemId addItem(const ItemInfo& row);
void restoreItems(std::vector<ItemInfo> rows); // rollback; see restoreBlocks

inline const char* itemName(ItemId id) { return itemInfo(id).name; }
inline ToolType    itemTool(ItemId id)        { return itemInfo(id).tool; }
inline int         itemTier(ItemId id)        { return itemInfo(id).toolTier; }
inline float       itemMiningSpeed(ItemId id) { return itemInfo(id).miningSpeed; }
inline ArmorSlot   itemArmorSlot(ItemId id)   { return itemInfo(id).armorSlot; }
inline float       itemArmor(ItemId id)       { return itemInfo(id).armor; }
inline float       itemWeaponDamage(ItemId id) { return itemInfo(id).weaponDamage; }
// A weapon SWINGS on LMB and never mines, so both the attack path and the
// mining path test the same predicate.
inline bool        isWeapon(ItemId id)        { return itemInfo(id).weaponDamage > 0.0f; }

// The atlas tile to draw for this item in UI. Placeable items borrow their
// block's side tile so icons always match the world; materials own an icon.
int iconTile(ItemId id);

// What a block yields when mined ({None,0} if nothing).
ItemStack blockDrop(BlockId id);

// The resource-node block that yields this raw item (Air if `id` is not a
// mineable raw). Used by the Miner's filter: put a raw in its input and it
// mines only that node type.
BlockId nodeForRaw(ItemId id);
