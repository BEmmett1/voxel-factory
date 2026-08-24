#include "game/PowerSystem.h"

#include "game/World.h"
#include "game/Chunk.h"

#include <array>
#include <unordered_set>
#include <vector>

namespace PowerSystem {

    namespace {
        const std::array<glm::ivec3, 6> kNeighbors = {{
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        }};
    } // namespace

    bool isPowerNode(BlockId id) {
        // Wire conducts; a machine joins its network if it draws or produces
        // power. Machines that run unpowered (traits demand 0, no output —
        // e.g. the Rain Barrel) stay out and must not conduct or glow.
        if (id == BlockId::Wire) return true;
        if (!isMachine(id)) return false;
        const MachineTraits& t = machineTraits(id);
        return t.demand > 0 || t.powerOutput > 0;
    }

    int demand(BlockId id) {
        return isMachine(id) ? machineTraits(id).demand : 0;
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
                            if (isMachine(id)) {
                                const MachineTraits& t = machineTraits(id);
                                const auto mit = machines.find(c);
                                // A switched-off machine is electrically
                                // absent: it neither asks for power nor makes
                                // any. It still CONDUCTS (the flood fill above
                                // runs on block ids), so idling one can never
                                // split a network and black out everything
                                // downstream of it.
                                const bool on = mit == machines.end() || mit->second.enabled;
                                if (t.powerOutput > 0 && on) {
                                    generators.push_back(c);
                                    // Only a burning generator produces.
                                    if (mit != machines.end() && mit->second.progress > 0.0f) {
                                        totalProduction += t.powerOutput;
                                    }
                                }
                                if (on) totalDemand += t.demand;
                            }

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
                                // An off machine stays dark on a live network,
                                // which costs nothing extra: the energized set
                                // already drives the emissive glow AND the
                                // shape animation, so excluding it here is the
                                // whole of "it looks switched off".
                                const auto mit = machines.find(c);
                                if (mit != machines.end() && !mit->second.enabled) continue;
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
