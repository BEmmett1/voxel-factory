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
        return id == BlockId::Generator || id == BlockId::Wire || isMachine(id);
    }

    int demand(BlockId id) {
        // Generators are machines now (fuel buffer, panel, belts) but produce
        // rather than consume -- they must not demand power from themselves.
        if (id == BlockId::Generator) return 0;
        return isMachine(id) ? kMachineDemand : 0;
    }

    PowerState solve(const World& world,
                     const std::unordered_map<glm::ivec3, Machine, IVec3Hash>& machines,
                     std::unordered_set<glm::ivec3, IVec3Hash>* outHungryGenerators) {
        PowerState state;
        if (outHungryGenerators) outHungryGenerators->clear();
        std::unordered_set<glm::ivec3, IVec3Hash> visited;

        std::vector<glm::ivec3> stack;
        std::vector<glm::ivec3> component;
        std::vector<glm::ivec3> generators;

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
                        generators.clear();
                        stack.push_back(start);
                        visited.insert(start);

                        int totalProduction = 0;
                        int totalDemand = 0;

                        while (!stack.empty()) {
                            const glm::ivec3 c = stack.back();
                            stack.pop_back();
                            component.push_back(c);

                            const BlockId id = world.getBlock(c.x, c.y, c.z);
                            if (id == BlockId::Generator) {
                                generators.push_back(c);
                                // Only a burning generator produces.
                                const auto mit = machines.find(c);
                                if (mit != machines.end() && mit->second.progress > 0.0f) {
                                    totalProduction += kGeneratorOutput;
                                }
                            }
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
                        // Any network that wants power keeps its generators
                        // lighting fresh fuel (satisfied or not).
                        if (outHungryGenerators && totalDemand > 0) {
                            for (const glm::ivec3& g : generators) {
                                outHungryGenerators->insert(g);
                            }
                        }
                    }
                }
            }
        }

        return state;
    }

} // namespace PowerSystem
