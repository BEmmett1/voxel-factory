#pragma once

#include "game/Belt.h"
#include "game/CropSystem.h"
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

    // Does this machine use `item` as an input, AND is there room for another?
    // A machine locked to a specific recipe only accepts that recipe's inputs
    // (so belts can't overfill it with ingredients it will never consume).
    //
    // The room half is what makes a belt line congest: beltStep already leaves
    // an item sitting on a belt whose target refuses it, so capacity needs no
    // belt code of its own.
    bool machineAccepts(const Machine& mac, ItemId item);

    // Per-item-type capacity of this machine's buffers. Most machines take the
    // vg:: defaults; a Storage crate and a Pedestal have their own. Exposed
    // because the panel marks a full line, and "full" has to mean the same
    // thing there as it does to a belt.
    int inputCap(const Machine& mac);
    int outputCap(const Machine& mac);

    // Where this machine's fuel lives: its own `fuel` buffer when it has one,
    // otherwise `input` (a Generator has no recipes to confuse fuel with).
    // Reading fuel through here means no burner needs a special case.
    const Inventory& fuelBuffer(const Machine& mac);
    Inventory&       fuelBuffer(Machine& mac);

    // Which buffer an arriving `item` should land in. Only belts need this: a
    // belt has no hands, so something must decide whether the wood rolling into
    // a Furnace is feedstock or firewood (ingredient wins -- the machine is
    // there to make the thing). A player dragging onto a cell has already said
    // which, and never consults this.
    const Inventory& bufferFor(const Machine& mac, ItemId item);
    Inventory&       bufferFor(Machine& mac, ItemId item);

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
    //
    // `crops` is here for the Harvester alone, which replants the cell it
    // reaped and so has to hand the new seedling its growth timer -- the same
    // registry-sync duty WorldEdit does for a hand-placed one. Passed rather
    // than reached for, because MachineSystem takes its state as parameters.
    void tickPowered(World& world, MachineMap& machines, const PowerState& power,
                     std::uint32_t seed, std::uint32_t& rngCounter,
                     CropSystem::CropMap& crops);

    // Advance conduits one step: deliver into accepting machines ahead, hop
    // items belt -> belt (snapshot + claims prevent chaining/merging), pull
    // from machine outputs behind.
    void beltStep(BeltMap& belts, MachineMap& machines);

} // namespace MachineSystem
