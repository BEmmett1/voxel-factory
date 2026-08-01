#pragma once

#include "game/Belt.h"
#include "game/HashIVec3.h"
#include "game/Machine.h"
#include "game/PowerSystem.h"

#include <glm/glm.hpp>

#include <unordered_map>
#include <unordered_set>

class World;

// The 20 Hz machine + conduit simulation: free functions over the world, the
// machine/belt maps, and the power state (the PowerSystem interface
// precedent — never VoxelGame&). Reactions that live outside the sim — the
// power re-solve, chunk dirtying, audio hums — stay with the caller, driven
// by tickSelfPowered's return value.
namespace MachineSystem {

    using MachineMap = std::unordered_map<glm::ivec3, Machine, IVec3Hash>;
    using BeltMap    = std::unordered_map<glm::ivec3, Belt, IVec3Hash>;

    // Does this machine use `item` as an input? A machine locked to a
    // specific recipe only accepts that recipe's inputs (so belts can't
    // overfill it with ingredients it will never consume).
    bool machineAccepts(const Machine& mac, ItemId item);

    // The first raw item in a miner's input buffer; None = unfiltered (mine
    // anything nearby). The filter item is a reference sample, never consumed.
    ItemId minerFilter(const Machine& mac);

    // Pre-pass: generators and collectors. They don't gate on network power
    // (generators CREATE it; collectors run unpowered), so they step before
    // the powered machines. Returns true when any generator's burn state
    // flipped — the caller re-solves the power network BEFORE tickPowered so
    // recipe machines see fresh power in this same tick.
    bool tickSelfPowered(const World& world, MachineMap& machines,
                         const std::unordered_set<glm::ivec3, IVec3Hash>& hungryGenerators,
                         bool raining);

    // Powered machines: miners harvest nearby nodes, processors run their
    // recipe lists; both gate on the energized set (machines whose traits row
    // demands no power — the manual and fuel-fired tiers — run regardless).
    // `seed` + `rngCounter` drive weighted recipe outputs, sharing the world's
    // saved roll sequence so a sifting line is deterministic across saves.
    void tickPowered(World& world, MachineMap& machines, const PowerState& power,
                     std::uint32_t seed, std::uint32_t& rngCounter);

    // Advance conduits one step: deliver into accepting machines ahead, hop
    // items belt -> belt (snapshot + claims prevent chaining/merging), pull
    // from machine outputs behind.
    void beltStep(BeltMap& belts, MachineMap& machines);

} // namespace MachineSystem
