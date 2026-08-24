// The Alchemy Circle multiblock: tier detection, rotation-invariant necklace
// matching, and pattern consumption. Pure functions over the world + machine
// map so the sim, the panel UI, and any future blueprint tooling all read the
// circle the same way.

#include "game/AlchemyCircle.h"

#include "VoxelGameInternal.h"
#include "game/World.h"

#include <algorithm>

using namespace vg;

namespace AlchemyCircle {

namespace {

    // A ring cell counts only when the BLOCK is a Pedestal and it has a
    // Machine entity. The two can disagree for a frame while an edit is in
    // flight, and a half-registered pedestal must never contribute items.
    const Machine* pedestalAt(const World& world, const MachineMap& machines,
                              const glm::ivec3& cell) {
        if (world.getBlock(cell.x, cell.y, cell.z) != BlockId::Pedestal) return nullptr;
        const auto it = machines.find(cell);
        return it == machines.end() ? nullptr : &it->second;
    }

    // A 4-slot pattern addresses the CARDINALS, which are the even ring
    // indices; an 8-slot pattern addresses every slot.
    int slotStride(std::size_t patternSize) {
        return patternSize == 4 ? 2 : 1;
    }

    // Does `ring` satisfy `pattern` when the pattern is rotated by `rot`
    // pattern-steps? A {None, 0} pattern slot demands an EMPTY pedestal;
    // otherwise the pedestal must hold that item, at least that many.
    bool matchesAt(const std::array<ItemStack, kRingSlots>& ring,
                   const std::vector<ItemStack>& pattern, int rot) {
        const int stride = slotStride(pattern.size());
        const int n = static_cast<int>(pattern.size());
        for (int i = 0; i < n; ++i) {
            const ItemStack& want = pattern[static_cast<std::size_t>(i)];
            const int slot = (((i + rot) % n) * stride) % kRingSlots;
            const ItemStack& got = ring[static_cast<std::size_t>(slot)];
            if (want.id == ItemId::None) {
                if (got.id != ItemId::None) return false; // slot must be bare
            } else if (got.id != want.id || got.count < want.count) {
                return false;
            }
        }
        // A 4-slot pattern laid on a full ring leaves the diagonals unused:
        // they must be empty too, or a Greater circle would quietly eat items
        // the recipe never asked for.
        if (stride == 2) {
            for (int s = 1; s < kRingSlots; s += 2) {
                if (ring[static_cast<std::size_t>(s)].id != ItemId::None) return false;
            }
        }
        return true;
    }

} // namespace

Tier tierAt(const World& world, const MachineMap& machines, const glm::ivec3& core) {
    int cardinals = 0, diagonals = 0;
    for (int i = 0; i < kRingSlots; ++i) {
        if (!pedestalAt(world, machines, slotPos(core, i))) continue;
        if (i % 2 == 0) ++cardinals; else ++diagonals;
    }
    if (cardinals == 4 && diagonals == 4) return Tier::Greater;
    if (cardinals == 4) return Tier::Lesser;
    return Tier::None;
}

std::array<ItemStack, kRingSlots> ringContents(const World& world,
                                               const MachineMap& machines,
                                               const glm::ivec3& core) {
    std::array<ItemStack, kRingSlots> out{};
    for (int i = 0; i < kRingSlots; ++i) {
        const Machine* p = pedestalAt(world, machines, slotPos(core, i));
        if (!p) continue;
        // A pedestal is a one-item-TYPE holder, so the first non-empty entry
        // is the whole story.
        for (int k = 1; k < static_cast<int>(itemCount()); ++k) {
            const ItemId id = static_cast<ItemId>(k);
            const int c = p->input.count(id);
            if (c > 0) { out[static_cast<std::size_t>(i)] = {id, c}; break; }
        }
    }
    return out;
}

Match findMatch(const std::array<ItemStack, kRingSlots>& ring,
                const Inventory& coreInput, Tier tier, bool energized, int only) {
    if (tier == Tier::None) return {};
    const auto& all = circleRecipes();
    for (std::size_t r = 0; r < all.size(); ++r) {
        if (only >= 0 && static_cast<int>(r) != only) continue;
        const CircleRecipe& rec = all[r];
        // Eight-slot patterns are the Greater circle's reward, and they only
        // run while it is actually powered.
        if (rec.ring.size() == kRingSlots && (tier != Tier::Greater || !energized)) continue;
        if (rec.ring.size() != 4 && rec.ring.size() != kRingSlots) continue;
        if (rec.center.id != ItemId::None &&
            !coreInput.has(rec.center.id, rec.center.count)) continue;
        for (int rot = 0; rot < static_cast<int>(rec.ring.size()); ++rot) {
            if (matchesAt(ring, rec.ring, rot)) return {&rec, rot};
        }
    }
    return {};
}

float craftSeconds(const CircleRecipe& recipe, Tier tier, bool energized) {
    const bool fast = tier == Tier::Greater && energized;
    return recipe.seconds * (fast ? 1.0f : kLesserCircleSlowdown);
}

void consume(const World& world, MachineMap& machines, const glm::ivec3& core,
             const Match& match, Inventory& coreInput) {
    if (!match) return;
    const std::vector<ItemStack>& pattern = match.recipe->ring;
    const int stride = slotStride(pattern.size());
    const int n = static_cast<int>(pattern.size());
    for (int i = 0; i < n; ++i) {
        const ItemStack& want = pattern[static_cast<std::size_t>(i)];
        if (want.id == ItemId::None) continue;
        const int slot = (((i + match.rotation) % n) * stride) % kRingSlots;
        const auto it = machines.find(slotPos(core, slot));
        if (it == machines.end()) continue;
        if (world.getBlock(slotPos(core, slot).x, slotPos(core, slot).y,
                           slotPos(core, slot).z) != BlockId::Pedestal) continue;
        it->second.input.remove(want.id, want.count);
    }
    if (match.recipe->center.id != ItemId::None) {
        coreInput.remove(match.recipe->center.id, match.recipe->center.count);
    }
}

} // namespace AlchemyCircle
