#pragma once

#include "game/HashIVec3.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"

#include <glm/glm.hpp>

#include <array>
#include <unordered_map>
#include <vector>

class World;

// The Alchemy Circle multiblock: a Rune Core with Pedestal blocks on the eight
// ring cells at radius 2 (a 5x5 footprint, roomy enough that belts can reach
// the pedestals from outside). Free functions over the world + machine map --
// the MachineSystem / WorldEdit precedent, never VoxelGame&.
//
// A recipe is a NECKLACE, not a Minecraft 3x3 bitmap: the ring slots are
// matched rotation-invariantly, so where you start laying a pattern never
// matters, while a slot's COUNT does. That is what lets two recipes with the
// same ingredients stay distinct -- Conduit is two plates on ONE pedestal,
// the Wrench is one plate on each of two OPPOSITE pedestals.
namespace AlchemyCircle {

    using MachineMap = std::unordered_map<glm::ivec3, Machine, IVec3Hash>;

    inline constexpr int kRingSlots = 8;

    // Ring cell offsets from the Rune Core, clockwise from north. The four
    // CARDINALS are the even indices, so a Lesser circle is exactly the even
    // slots and a 4-slot recipe indexes straight into them.
    inline constexpr std::array<glm::ivec3, kRingSlots> kRingOffsets = {{
        { 0, 0, -2}, { 2, 0, -2}, { 2, 0,  0}, { 2, 0,  2},
        { 0, 0,  2}, {-2, 0,  2}, {-2, 0,  0}, {-2, 0, -2},
    }};

    // How much of the ring is actually built. Pedestal COUNT is the tier.
    enum class Tier {
        None,    // not even the four cardinals -- an inert core
        Lesser,  // the 4 cardinal pedestals: runs UNPOWERED and slow
        Greater, // all 8: draws power, runs faster, unlocks 8-slot patterns
    };

    // Which tier the circle around `core` currently forms. A cell counts only
    // when it is a Pedestal block AND has a Machine entity (the two can drift
    // apart for a frame while an edit is in flight).
    Tier tierAt(const World& world, const MachineMap& machines, const glm::ivec3& core);

    // The item each ring slot holds, indexed like kRingOffsets. A missing or
    // empty pedestal reads as {None, 0}.
    std::array<ItemStack, kRingSlots> ringContents(const World& world,
                                                   const MachineMap& machines,
                                                   const glm::ivec3& core);

    // A recipe matched against a laid-out circle, plus the rotation it matched
    // at -- the tick needs that to know which pedestal to draw each slot from.
    struct Match {
        const CircleRecipe* recipe = 0;
        int                 rotation = 0; // slots shifted clockwise
        explicit operator bool() const { return recipe != 0; }
    };

    // First recipe the current contents satisfy. `tier` gates which patterns
    // are eligible (8-slot patterns need Greater) and `energized` gates the
    // Greater tier's extra reach: an unpowered Greater circle still runs the
    // 4-slot patterns, just slowly. `only` (>= 0) restricts the search to one
    // recipe index -- the panel's MAKE lock.
    Match findMatch(const std::array<ItemStack, kRingSlots>& ring,
                    const Inventory& coreInput, Tier tier, bool energized,
                    int only = -1);

    // Seconds this circle takes to run `recipe`, after the tier's speed.
    float craftSeconds(const CircleRecipe& recipe, Tier tier, bool energized);

    // Remove a matched pattern's items from the pedestals and the core's own
    // buffer. Call only with a Match that findMatch just returned.
    void consume(const World& world, MachineMap& machines, const glm::ivec3& core,
                 const Match& match, Inventory& coreInput);

    // The world position of ring slot `slot` (0..7) for a core at `core`.
    inline glm::ivec3 slotPos(const glm::ivec3& core, int slot) {
        return core + kRingOffsets[static_cast<std::size_t>(slot)];
    }

} // namespace AlchemyCircle
