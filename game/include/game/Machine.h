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
    bool      crafting = false;  // had a valid powered recipe this tick
    float     craftTime = 1.0f;  // seconds of the active recipe (for the bar)
    int       selectedRecipe = -1; // index into this type's recipe list; -1 = auto

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
};

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
    ItemId      fuel = ItemId::None;     // what a Generator burns
    float       burnSeconds = 0.0f;      // burn time per fuel item (Generator)
    ItemId      collects = ItemId::None; // what a Collector gathers
    int         collectCap = 0;          // Collector stops when output holds this many
    float       collectSeconds = 0.0f;   // seconds per collected item
};

inline constexpr MachineTraits kMachineTraits[] = {
    {.block = BlockId::Generator, .kind = MachineKind::Generator, .demand = 0,
     .powerOutput = 10, .fuel = ItemId::Wood, .burnSeconds = 20.0f},
    {.block = BlockId::Grinder},
    {.block = BlockId::Cauldron},
    {.block = BlockId::Infuser},
    {.block = BlockId::Alembic},
    {.block = BlockId::Distiller},
    {.block = BlockId::Transmuter},
    {.block = BlockId::Miner, .kind = MachineKind::Miner},
    {.block = BlockId::RainBarrel, .kind = MachineKind::Collector, .demand = 0,
     .collects = ItemId::SpringWater, .collectCap = 10, .collectSeconds = 12.0f},
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

// The traits row for a machine block. Only valid when isMachine(id) — every
// call site is naturally guarded (machines are looked up via the machine map
// or behind an isMachine() check).
inline const MachineTraits& machineTraits(BlockId id) {
    return kMachineTraits[detail::kMachineTraitIndex[static_cast<std::size_t>(id)]];
}
