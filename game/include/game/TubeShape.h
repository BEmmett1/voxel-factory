#pragma once

#include "game/Belt.h"
#include "game/Block.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>

// Which arms a Conduit or a Wire grows, as a pure function of its neighbours.
//
// Free functions over the registries, the MachineSystem / WorldEdit / Collision
// precedent, and for the usual reason: a rule that lives inside the mesher can
// only be checked by looking at the screen, and "does a corner grow the right
// two arms" is exactly the question --selftest should be asking. The mesher
// supplies the six neighbour block ids it already has in hand and gets back a
// bitmask over kShapeFaceDirs.
//
// The two rules are deliberately different, because the two blocks connect for
// different reasons:
//
//   * A WIRE connects to whatever the power solver would flood into, so it
//     calls PowerSystem::isPowerNode -- literally the same predicate. The
//     picture therefore cannot disagree with the network, which matters because
//     a wire that LOOKS connected and is not would be a bug the player has no
//     way to diagnose.
//
//   * A CONDUIT connects along the path items actually take: out the way it
//     faces, back to a machine it pulls from, and sideways to any belt aiming
//     into it. Two belts running side by side share no arm, because nothing
//     passes between them.
namespace TubeShape {

    using BeltMap = std::unordered_map<glm::ivec3, Belt, IVec3Hash>;

    // The six neighbouring block ids, in kShapeFaceDirs order.
    using Neighbours = std::array<BlockId, 6>;

    // Index into kShapeFaceDirs for a unit cardinal, or -1. A Belt's facing is
    // always one of the six, so the -1 is defensive rather than expected.
    int faceIndex(const glm::ivec3& dir);

    // Bit i set => draw the arm on kShapeFaceDirs[i].
    //
    // The OUT arm is drawn ALWAYS, even into open air, because it is what
    // replaced the top-face arrow: a conduit has to state which way it moves
    // things whether or not anything is there to receive them. Every other arm
    // means a real connection.
    std::uint8_t conduitArms(const glm::ivec3& pos, const Belt& self,
                             const BeltMap& belts, const Neighbours& neighbours);

    // Symmetric, so two adjacent wires each reach for the other.
    std::uint8_t wireArms(const Neighbours& neighbours);

} // namespace TubeShape
