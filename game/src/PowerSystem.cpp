#include "game/PowerSystem.h"

#include "game/World.h"
#include "game/Chunk.h"

#include <array>
#include <unordered_set>
#include <vector>

namespace PowerSystem {

    namespace {
        constexpr int kGeneratorOutput = 10;
        constexpr int kMachineDemand = 5;

        const std::array<glm::ivec3, 6> kNeighbors = {{
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        }};
    } // namespace

    bool isPowerNode(BlockId id) {
        return id == BlockId::Generator || id == BlockId::Wire || id == BlockId::Machine;
    }

    int production(BlockId id) {
        return id == BlockId::Generator ? kGeneratorOutput : 0;
    }

    int demand(BlockId id) {
        return id == BlockId::Machine ? kMachineDemand : 0;
    }

    PowerState solve(const World& world) {
        PowerState state;
        std::unordered_set<glm::ivec3, IVec3Hash> visited;

        std::vector<glm::ivec3> stack;
        std::vector<glm::ivec3> component;

        // Seed from every power node in every loaded chunk.
        for (const auto& [coord, chunk] : world.chunks()) {
            for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
                for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                        if (!isPowerNode(chunk->get(lx, ly, lz))) continue;

                        const glm::ivec3 start = coord * CHUNK_SIZE + glm::ivec3(lx, ly, lz);
                        if (visited.count(start)) continue;

                        // Flood-fill one connected network of power nodes.
                        component.clear();
                        stack.clear();
                        stack.push_back(start);
                        visited.insert(start);

                        int totalProduction = 0;
                        int totalDemand = 0;

                        while (!stack.empty()) {
                            const glm::ivec3 c = stack.back();
                            stack.pop_back();
                            component.push_back(c);

                            const BlockId id = world.getBlock(c.x, c.y, c.z);
                            totalProduction += production(id);
                            totalDemand += demand(id);

                            for (const glm::ivec3& n : kNeighbors) {
                                const glm::ivec3 nc = c + n;
                                if (visited.count(nc)) continue;
                                if (!isPowerNode(world.getBlock(nc.x, nc.y, nc.z))) continue;
                                visited.insert(nc);
                                stack.push_back(nc);
                            }
                        }

                        const bool powered = totalProduction > 0 && totalProduction >= totalDemand;
                        if (powered) {
                            for (const glm::ivec3& c : component) {
                                state.setEnergized(c);
                            }
                        }
                    }
                }
            }
        }

        return state;
    }

} // namespace PowerSystem
