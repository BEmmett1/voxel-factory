#include "game/TubeShape.h"

#include "game/BlockShape.h"
#include "game/PowerSystem.h"

namespace TubeShape {

    int faceIndex(const glm::ivec3& dir) {
        for (int i = 0; i < kFaceCount; ++i) {
            if (kShapeFaceDirs[i] == dir) return i;
        }
        return -1;
    }

    std::uint8_t conduitArms(const glm::ivec3& pos, const Belt& self,
                             const BeltMap& belts, const Neighbours& neighbours) {
        std::uint8_t mask = 0;

        // Out the way it faces, unconditionally: this arm is the direction
        // indicator, not a connection claim.
        const int out = faceIndex(self.facing);
        if (out >= 0) mask |= static_cast<std::uint8_t>(1u << out);

        for (int i = 0; i < kFaceCount; ++i) {
            const glm::ivec3 d = kShapeFaceDirs[i];

            // Behind us: beltStep pulls out of a machine sitting there, so that
            // is a real path for items and earns an arm. A machine anywhere
            // else is not connected to this belt at all -- it neither feeds it
            // nor takes from it -- so it gets nothing.
            if (d == -self.facing && isMachine(neighbours[i])) {
                mask |= static_cast<std::uint8_t>(1u << i);
                continue;
            }

            // Any neighbouring belt aiming INTO this cell feeds it, whichever
            // side it comes from. That is what makes a junction read as a
            // junction: three lanes merging draw three arms without anything
            // knowing the word "junction".
            const auto it = belts.find(pos + d);
            if (it != belts.end() && it->second.facing == -d) {
                mask |= static_cast<std::uint8_t>(1u << i);
            }
        }
        return mask;
    }

    std::uint8_t wireArms(const Neighbours& neighbours) {
        std::uint8_t mask = 0;
        for (int i = 0; i < kFaceCount; ++i) {
            if (PowerSystem::isPowerNode(neighbours[i])) {
                mask |= static_cast<std::uint8_t>(1u << i);
            }
        }
        return mask;
    }

} // namespace TubeShape
