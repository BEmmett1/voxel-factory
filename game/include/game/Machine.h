#pragma once

#include "game/Block.h"
#include "game/Inventory.h"
#include "game/Recipes.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

// Runtime state for a placed machine block. Ingredients wait in `input`,
// finished goods collect in `output`, and `progress` counts seconds into the
// current craft.
struct Machine {
    BlockId   type = BlockId::Air;
    Inventory input;
    Inventory output;
    // Fuel, kept apart from the ingredients. Only machines that both burn fuel
    // AND run recipes use this (see usesFuelSlot) -- a Generator has no recipes
    // to confuse fuel with, so it burns straight out of `input`.
    Inventory fuel;
    float     progress = 0.0f;   // seconds into the active recipe; a Generator
                                 // stores its remaining burn seconds here
    float     burnLeft = 0.0f;   // seconds of fuel left (burnsFuel Processors);
                                 // a Generator keeps its burn in `progress`
    bool      crafting = false;  // had a valid powered recipe this tick
    float     craftTime = 1.0f;  // seconds of the active recipe (for the bar)
    int       selectedRecipe = -1; // RUNTIME index into this type's recipe list;
                                 // -1 = auto. Saved as the recipe's KEY, not as
                                 // this index -- see Recipes.h.

    // The master switch (save v24). OFF means FROZEN: the machine does no work,
    // draws no power, produces none, and goes dark -- but it keeps its buffers,
    // keeps conducting (so switching one off can never split a network), and
    // still accepts deliveries and gives up its output. That last part is what
    // makes it a logistics tool rather than a wall: an idled machine fills to
    // its input cap and the feed line backs up from there on its own, with no
    // special case anywhere.
    bool      enabled = true;

    // Miner only, transient (not saved; re-acquired after load): the node
    // being drilled, so the reach isn't re-scanned every tick.
    glm::ivec3 target{0};
    bool       hasTarget = false;
    int        rescanCooldown = 0; // ticks until the next idle scan

    // Transient (not saved -- recomputed by the first tick after a load): the
    // craft is ready but its product has nowhere to go, so progress is HELD and
    // the inputs are untouched. Purely a signal for the UI; the sim re-derives
    // it every tick.
    bool       jammed = false;

    // Hand-cranked tier only, transient (not saved -- a half-turned handle is
    // not worth persisting, and a fresh load simply starts the turn again):
    // how far round the current rotation the player is, and the work they have
    // banked but the tick has not yet spent.
    int        crankStep = 0;      // quarter-turns done, 0..3
    float      crankBanked = 0.0f; // seconds of progress waiting for the tick
};

// What a machine does each tick. Behavior code lives in per-kind dispatch
// (the sim tick, machineAccepts, the panel UI, the power solve) — call sites
// switch on the kind from the traits row, never on BlockIds.
enum class MachineKind : std::uint8_t {
    Processor, // runs its MachineRecipe list, gated on network power
    Generator, // burns fuel into network power; progress = burn seconds left
    Collector, // fills its output from the environment; runs unpowered
    Miner,     // harvests nearby resource nodes, gated on network power
    RuneCore,  // reads the ring of Pedestals around it and runs CircleRecipes
    Pedestal,  // passive one-item-type holder; a ring slot for the Rune Core
    Storage,   // bulk stockpile; belts both fill and drain it
    // Reaps ripe crops in reach and REPLANTS the cell at stage 0. Its own kind
    // rather than a Miner variant for exactly that reason: a Miner leaves Air,
    // and a field that harvested itself into bare soil would need re-sowing by
    // hand forever, which is the opposite of automation.
    Harvester,
};

// Enum spellings for the content pack format -- see kToolNames in Item.h.
inline constexpr const char* kKindNames[] = {
    "processor", "generator", "collector", "miner", "rune_core", "pedestal",
    "storage", "harvester",
};
static_assert(std::size(kKindNames) == 8,
              "kKindNames needs one name per MachineKind");

// What burns, and for how long. A shared registry rather than a per-machine
// field, so "add a better fuel" is one row here instead of a change at every
// burner: anything with a row is valid fuel in a Generator AND in a fuel-fired
// Processor, and the machine's own fuelMult decides how well it uses it.
struct FuelInfo {
    ItemId item;
    float  seconds; // base burn time for one item
};

inline constexpr FuelInfo kFuelSeed[] = {
    {ItemId::Stick, 5.0f},
    {ItemId::SaplingItem, 5.0f},
    {ItemId::Wood, 20.0f},
    {ItemId::Charcoal, 60.0f},
};

// Base burn seconds for one of `item`; 0 = not a fuel. Reads the RUNTIME fuel
// table (kFuelSeed above is its seed), so a pack can add a fuel.
float fuelSeconds(ItemId item);

// Every fuel, compiled and loaded.
const std::vector<FuelInfo>& fuelRows();

// Startup only, like the other registries -- see blockCount() in Block.h.
void addFuel(const FuelInfo& row);
void restoreFuels(std::vector<FuelInfo> rows); // rollback

// Static per-machine-type properties: one registry row per machine block,
// like kBlocks/kItems. Block.cpp cross-static_asserts this table against the
// BlockInfo machine flags, so a machine without a row (or a row for a
// non-machine) is a compile error. A new standard recipe machine is just a
// Processor row; genuinely new behavior is a new kind plus one case in each
// dispatch switch.
struct MachineTraits {
    BlockId     block;
    MachineKind kind = MachineKind::Processor;
    int         demand = 5;              // power drawn from its network (0 = runs unpowered)
    int         powerOutput = 0;         // power produced while burning (Generator)
    bool        burnsFuel = false;       // consumes kFuelSeed items to run
    float       fuelMult = 1.0f;         // burn-time multiplier (efficiency)
    ItemId      collects = ItemId::None; // what a Collector gathers
    int         collectCap = 0;          // Collector stops when output holds this many
    float       collectSeconds = 0.0f;   // seconds per collected item

    // Whose recipe list to run. Air = its own. This is what makes the MANUAL
    // tier free: a hand-cranked twin points at its powered counterpart and
    // inherits every recipe, so a recipe is authored exactly once and the two
    // tiers can never drift apart.
    BlockId     recipeGroup = BlockId::Air;
    // Multiplies every recipe's time. How much WORK a manual craft costs.
    float       speedMult = 1.0f;
    // The manual tier: progress comes from the player turning the handle, not
    // from the clock. A cranked machine with full buffers and nobody at it
    // does nothing at all -- see tickPowered.
    bool        handCranked = false;
};

// How much more work the hand-cranked tier is than its powered twin. This is
// no longer a wall-clock stretch: a cranked machine does not advance on its
// own, so this multiplies the number of rotations the player owes. The Lesser
// Alchemy Circle still makes the older, gentler bargain (kLesserCircleSlowdown
// -- slow but self-running), because a ritual circle is not a handle. Lives
// here rather than with the other tuning knobs because it is machine data,
// like the rest of this table.
inline constexpr float kManualSlowdown = 3.0f;

inline constexpr MachineTraits kMachineTraitSeed[] = {
    {.block = BlockId::Generator, .kind = MachineKind::Generator, .demand = 0,
     .powerOutput = 10, .burnsFuel = true},
    {.block = BlockId::Grinder},
    {.block = BlockId::Cauldron},
    {.block = BlockId::Infuser},
    {.block = BlockId::Alembic},
    {.block = BlockId::Distiller},
    {.block = BlockId::Transmuter},
    {.block = BlockId::Miner, .kind = MachineKind::Miner},
    {.block = BlockId::RainBarrel, .kind = MachineKind::Collector, .demand = 0,
     .collects = ItemId::SpringWater, .collectCap = 10, .collectSeconds = 12.0f},
    {.block = BlockId::Composter}, // Processor: composts plant matter into Dirt
    {.block = BlockId::Forge},     // Processor: forges weapons/armor
    {.block = BlockId::Press},     // Processor: forms the shared parts tier
    // The Alchemy Circle. The Core is a normal power node: a Lesser (4-pedestal)
    // circle runs unpowered because its TICK ignores power, not because it is
    // off-network, so an isolated circle bootstraps you a Generator. Pedestals
    // draw nothing and so are not power nodes at all -- the ring never conducts,
    // which keeps a 5x5 footprint from silently bridging two networks.
    {.block = BlockId::RuneCore, .kind = MachineKind::RuneCore, .demand = 8},
    {.block = BlockId::Pedestal, .kind = MachineKind::Pedestal, .demand = 0},

    // ---- The recipe overhaul ------------------------------------------
    // The Furnace runs on FUEL, not electricity: demand 0 keeps it off the
    // power graph entirely (the Rain Barrel precedent), so smelting is
    // available from the first bloom of ore and generators stay a
    // convenience rather than a gate. Heat being its own resource is also
    // what gives Charcoal a job.
    {.block = BlockId::Furnace, .demand = 0, .burnsFuel = true},
    {.block = BlockId::Sifter},
    {.block = BlockId::Glassblower},
    {.block = BlockId::Compactor},

    // The MANUAL tier. Each row is the whole machine: no power (demand 0), its
    // powered twin's recipes (recipeGroup), kManualSlowdown times the work --
    // and handCranked, which is what makes it manual rather than merely slow.
    // The Bloomery is the one manual-tier machine that is NOT cranked, because
    // you cannot hand crank a fire: what does the work in a bloomery is the
    // burn, and a burn keeps going whether or not anyone is standing there.
    // So it runs on the clock like every other machine -- light it and walk
    // away -- and pays for the privilege in the other two currencies instead:
    // kManualSlowdown times as long as a Furnace, on fuel it wastes (fuelMult).
    // That leaves the crank tier meaning one coherent thing (a machine your
    // ARM drives) rather than two, and leaves the fuel tier meaning another.
    {.block = BlockId::Bloomery, .demand = 0, .burnsFuel = true, .fuelMult = 0.6f,
     .recipeGroup = BlockId::Furnace, .speedMult = kManualSlowdown},
    {.block = BlockId::Sieve, .demand = 0,
     .recipeGroup = BlockId::Sifter, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::Blowpipe, .demand = 0,
     .recipeGroup = BlockId::Glassblower, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::Tamper, .demand = 0,
     .recipeGroup = BlockId::Compactor, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::Mortar, .demand = 0,
     .recipeGroup = BlockId::Grinder, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::HandPress, .demand = 0,
     .recipeGroup = BlockId::Press, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::Anvil, .demand = 0,
     .recipeGroup = BlockId::Forge, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::CompostHeap, .demand = 0,
     .recipeGroup = BlockId::Composter, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::MixingBowl, .demand = 0,
     .recipeGroup = BlockId::Cauldron, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::InfusionStand, .demand = 0,
     .recipeGroup = BlockId::Infuser, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::Still, .demand = 0,
     .recipeGroup = BlockId::Alembic, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::HandDistiller, .demand = 0,
     .recipeGroup = BlockId::Distiller, .speedMult = kManualSlowdown, .handCranked = true},
    {.block = BlockId::HandTransmuter, .demand = 0,
     .recipeGroup = BlockId::Transmuter, .speedMult = kManualSlowdown, .handCranked = true},

    // Bulk storage. demand 0 keeps it off the power graph (the Rain Barrel and
    // Pedestal precedent), so a crate never conducts and a row of them can't
    // silently bridge two networks. Its tick migrates input -> output, which is
    // the whole trick: beltStep fills a machine's `input` and drains its
    // `output`, so one buffer swap makes a crate both feedable and drainable
    // with no belt code at all.
    {.block = BlockId::StorageCrate, .kind = MachineKind::Storage, .demand = 0},

    // Farming's automation payoff: the Miner one field over. Powered, because
    // the whole point of a farm is that it runs while you are somewhere else,
    // and the manual tier already has an answer for reaping by hand -- your
    // hands.
    {.block = BlockId::Harvester, .kind = MachineKind::Harvester},
};

// Every cranked machine is a manual twin -- but NOT every twin is cranked.
// This used to be an equivalence, which quietly forced the Bloomery to be
// hand-turned for no reason except that it shared a recipe list with the
// Furnace. The two questions are genuinely separate: `recipeGroup` asks WHOSE
// RECIPES do I run, `handCranked` asks WHO SUPPLIES THE WORK, and a bloomery
// answers "the Furnace's" and "the fire". Only the implication still has to
// hold, because a cranked machine with no recipes would be a handle attached
// to nothing.
static_assert([] {
    for (const MachineTraits& t : kMachineTraitSeed) {
        if (t.handCranked && t.recipeGroup == BlockId::Air) return false;
    }
    return true;
}(), "a handCranked machine must name a recipeGroup (a handle needs a job)");

static_assert([] {
    for (std::size_t i = 0; i < std::size(kMachineTraitSeed); ++i) {
        for (std::size_t j = i + 1; j < std::size(kMachineTraitSeed); ++j) {
            if (kMachineTraitSeed[i].block == kMachineTraitSeed[j].block) return false;
        }
    }
    return true;
}(), "kMachineTraitSeed has a duplicate row");

// A recipeGroup must name a real machine that is itself ungrouped, so
// recipeGroupFor() stays a single hop and two manual twins can never chain
// into a cycle. (Over the compiled seed; content::validate() re-asks it of the
// whole runtime table, which is where a pack's row would show up.)
static_assert([] {
    for (const MachineTraits& t : kMachineTraitSeed) {
        if (t.recipeGroup == BlockId::Air) continue;
        const MachineTraits* group = nullptr;
        for (const MachineTraits& g : kMachineTraitSeed) {
            if (g.block == t.recipeGroup) group = &g;
        }
        if (!group || group->recipeGroup != BlockId::Air) return false;
    }
    return true;
}(), "a kMachineTraitSeed recipeGroup must point at an ungrouped machine block");

// The traits row for a machine block. Only valid when isMachine(id) — every
// call site is naturally guarded (machines are looked up via the machine map
// or behind an isMachine() check).
//
// Backed by a runtime table seeded from kMachineTraitSeed, with a block-id -> row
// index built beside it. That index used to be a `constexpr` array of
// `std::int8_t` sized by `BlockId::Count`, which silently capped the game at
// 127 machines; it is now a vector of int sized by blockCount(), so the cap is
// gone along with the fixed size.
const MachineTraits& machineTraits(BlockId id);

// Does this block have a traits row at all? The static_assert above pins
// `machine` and "has a row" together for compiled content, but a pack is
// checked at runtime, and a caller that is ASKING cannot assume the answer.
bool hasMachineTraits(BlockId id);

// Every machine's traits, compiled and loaded.
const std::vector<MachineTraits>& machineTraitRows();

// Startup only, like the other registries -- see blockCount() in Block.h.
void addMachineTraits(const MachineTraits& row);
void restoreMachineTraits(std::vector<MachineTraits> rows); // rollback

// Whose recipe rows this machine runs: itself, unless its row delegates to a
// powered counterpart. One hop only -- a manual twin never points at another
// manual twin, and this keeps the lookup a plain lookup.
inline BlockId recipeGroupFor(BlockId id) {
    const BlockId group = machineTraits(id).recipeGroup;
    return group == BlockId::Air ? id : group;
}

// Does this machine keep its fuel in a buffer of its own?
//
// Only when it burns fuel AND has recipes, because that is exactly when the
// two piles can be confused: a Furnace fed wood cannot otherwise tell the wood
// it is meant to CHAR from the wood it is meant to BURN. A dedicated slot makes
// the player say which, and in exchange the machine may now do both at once.
// A Generator has no recipes and so no ambiguity -- it burns straight out of
// `input`, and a second buffer would be ceremony.
//
// Derived rather than a hand-set traits field on purpose: a future fuel-fired
// Processor earns a slot by existing, a future generator tier stays slotless,
// and there is no fourteenth column to keep in sync with reality.
inline bool usesFuelSlot(BlockId id) {
    return machineTraits(id).burnsFuel && !recipesForMachine(id).empty();
}

// Is a master switch worth offering here? Everything that DOES something does:
// processors, generators, collectors, miners, circles, crates. A Pedestal is a
// passive shelf whose tick is already a `continue`, so a switch on it would be
// a control that changes nothing -- and a dead control teaches players that the
// other switches might be dead too.
inline bool hasPowerSwitch(BlockId id) {
    return machineTraits(id).kind != MachineKind::Pedestal;
}
