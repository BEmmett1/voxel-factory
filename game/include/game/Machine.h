#pragma once

#include "game/Block.h"
#include "game/Inventory.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

// Runtime state for a placed machine block. Items wait in `input`, finished
// goods collect in `output`, and `progress` counts seconds into the current
// craft. The input/output buffers are belt-ready (Step 5 will fill/drain them).
struct Machine {
    BlockId   type = BlockId::Air;
    Inventory input;
    Inventory output;
    float     progress = 0.0f;   // seconds into the active recipe; a Generator
                                 // stores its remaining burn seconds here
    float     burnLeft = 0.0f;   // seconds of fuel left (burnsFuel Processors);
                                 // a Generator keeps its burn in `progress`
    bool      crafting = false;  // had a valid powered recipe this tick
    float     craftTime = 1.0f;  // seconds of the active recipe (for the bar)
    int       selectedRecipe = -1; // RUNTIME index into this type's recipe list;
                                 // -1 = auto. Saved as the recipe's KEY, not as
                                 // this index -- see Recipes.h.

    // Miner only, transient (not saved; re-acquired after load): the node
    // being drilled, so the reach isn't re-scanned every tick.
    glm::ivec3 target{0};
    bool       hasTarget = false;
    int        rescanCooldown = 0; // ticks until the next idle scan
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
};

// What burns, and for how long. A shared registry rather than a per-machine
// field, so "add a better fuel" is one row here instead of a change at every
// burner: anything with a row is valid fuel in a Generator AND in a fuel-fired
// Processor, and the machine's own fuelMult decides how well it uses it.
struct FuelInfo {
    ItemId item;
    float  seconds; // base burn time for one item
};

inline constexpr FuelInfo kFuels[] = {
    {ItemId::Stick, 5.0f},
    {ItemId::SaplingItem, 5.0f},
    {ItemId::Wood, 20.0f},
    {ItemId::Charcoal, 60.0f},
};

// Base burn seconds for one of `item`; 0 = not a fuel.
inline constexpr float fuelSeconds(ItemId item) {
    for (const FuelInfo& f : kFuels) {
        if (f.item == item) return f.seconds;
    }
    return 0.0f;
}

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
    bool        burnsFuel = false;       // consumes kFuels items to run
    float       fuelMult = 1.0f;         // burn-time multiplier (efficiency)
    ItemId      collects = ItemId::None; // what a Collector gathers
    int         collectCap = 0;          // Collector stops when output holds this many
    float       collectSeconds = 0.0f;   // seconds per collected item

    // Whose recipe list to run. Air = its own. This is what makes the MANUAL
    // tier free: a hand-cranked twin points at its powered counterpart and
    // inherits every recipe, so a recipe is authored exactly once and the two
    // tiers can never drift apart.
    BlockId     recipeGroup = BlockId::Air;
    // Multiplies every recipe's time. The manual tier's whole cost.
    float       speedMult = 1.0f;
};

// How much slower the hand-cranked tier is than its powered twin. The whole
// price of playing before electricity, and deliberately the same bargain the
// Lesser Alchemy Circle makes (kLesserCircleSlowdown): a manual machine WILL
// build you the powered one, it will just make you wait for it. Lives here
// rather than with the other tuning knobs because it is machine data, like the
// rest of this table.
inline constexpr float kManualSlowdown = 3.0f;

inline constexpr MachineTraits kMachineTraits[] = {
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
    {.block = BlockId::Furnace, .burnsFuel = true},
    {.block = BlockId::Sifter},
    {.block = BlockId::Glassblower},
    {.block = BlockId::Compactor},

    // The MANUAL tier. Each row is the whole machine: no power (demand 0),
    // its powered twin's recipes (recipeGroup), and kManualSlowdown times
    // longer to do them. The Bloomery still needs fuel -- you cannot hand
    // crank a fire -- and burns it less efficiently than a real Furnace.
    {.block = BlockId::Bloomery, .demand = 0, .burnsFuel = true, .fuelMult = 0.6f,
     .recipeGroup = BlockId::Furnace, .speedMult = kManualSlowdown},
    {.block = BlockId::Sieve, .demand = 0,
     .recipeGroup = BlockId::Sifter, .speedMult = kManualSlowdown},
    {.block = BlockId::Blowpipe, .demand = 0,
     .recipeGroup = BlockId::Glassblower, .speedMult = kManualSlowdown},
    {.block = BlockId::Tamper, .demand = 0,
     .recipeGroup = BlockId::Compactor, .speedMult = kManualSlowdown},
    {.block = BlockId::Mortar, .demand = 0,
     .recipeGroup = BlockId::Grinder, .speedMult = kManualSlowdown},
    {.block = BlockId::HandPress, .demand = 0,
     .recipeGroup = BlockId::Press, .speedMult = kManualSlowdown},
    {.block = BlockId::Anvil, .demand = 0,
     .recipeGroup = BlockId::Forge, .speedMult = kManualSlowdown},
    {.block = BlockId::CompostHeap, .demand = 0,
     .recipeGroup = BlockId::Composter, .speedMult = kManualSlowdown},
    {.block = BlockId::MixingBowl, .demand = 0,
     .recipeGroup = BlockId::Cauldron, .speedMult = kManualSlowdown},
    {.block = BlockId::InfusionStand, .demand = 0,
     .recipeGroup = BlockId::Infuser, .speedMult = kManualSlowdown},
    {.block = BlockId::Still, .demand = 0,
     .recipeGroup = BlockId::Alembic, .speedMult = kManualSlowdown},
    {.block = BlockId::HandDistiller, .demand = 0,
     .recipeGroup = BlockId::Distiller, .speedMult = kManualSlowdown},
    {.block = BlockId::HandTransmuter, .demand = 0,
     .recipeGroup = BlockId::Transmuter, .speedMult = kManualSlowdown},
};

static_assert([] {
    for (std::size_t i = 0; i < std::size(kMachineTraits); ++i) {
        for (std::size_t j = i + 1; j < std::size(kMachineTraits); ++j) {
            if (kMachineTraits[i].block == kMachineTraits[j].block) return false;
        }
    }
    return true;
}(), "kMachineTraits has a duplicate row");

namespace detail {
    inline constexpr auto kMachineTraitIndex = [] {
        std::array<std::int8_t, static_cast<std::size_t>(BlockId::Count)> idx{};
        for (auto& v : idx) v = -1;
        for (std::size_t i = 0; i < std::size(kMachineTraits); ++i) {
            idx[static_cast<std::size_t>(kMachineTraits[i].block)] =
                static_cast<std::int8_t>(i);
        }
        return idx;
    }();
}

// A recipeGroup must name a real machine that is itself ungrouped, so
// recipeGroupFor() stays a single hop and two manual twins can never chain
// into a cycle.
static_assert([] {
    for (const MachineTraits& t : kMachineTraits) {
        if (t.recipeGroup == BlockId::Air) continue;
        const std::int8_t at =
            detail::kMachineTraitIndex[static_cast<std::size_t>(t.recipeGroup)];
        if (at < 0) return false;
        if (kMachineTraits[at].recipeGroup != BlockId::Air) return false;
    }
    return true;
}(), "a kMachineTraits recipeGroup must point at an ungrouped machine block");

// The traits row for a machine block. Only valid when isMachine(id) — every
// call site is naturally guarded (machines are looked up via the machine map
// or behind an isMachine() check).
inline const MachineTraits& machineTraits(BlockId id) {
    return kMachineTraits[detail::kMachineTraitIndex[static_cast<std::size_t>(id)]];
}

// Whose recipe rows this machine runs: itself, unless its row delegates to a
// powered counterpart. One hop only -- a manual twin never points at another
// manual twin, and this keeps the lookup a plain lookup.
inline BlockId recipeGroupFor(BlockId id) {
    const BlockId group = machineTraits(id).recipeGroup;
    return group == BlockId::Air ? id : group;
}
