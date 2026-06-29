#include "game/PowerSystem.h"

#include <array>
#include <vector>

namespace PowerSystem {

    namespace {
        constexpr int kGeneratorOutput = 10;
        constexpr int kMachineDemand = 5;

        const std::array<glm::ivec3, 6> kNeighbors = {{
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        }};

        int flatIndex(int x, int y, int z) {
            return x + CHUNK_SIZE * (y + CHUNK_SIZE * z);
        }
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

    PowerState solve(const Chunk& chunk) {
        PowerState state;
        std::array<bool, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> visited{};

        std::vector<glm::ivec3> stack;
        std::vector<glm::ivec3> component;

        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    if (!isPowerNode(chunk.get(x, y, z))) continue;
                    if (visited[flatIndex(x, y, z)]) continue;

                    // Flood-fill one connected network of power nodes.
                    component.clear();
                    stack.clear();
                    stack.push_back({x, y, z});
                    visited[flatIndex(x, y, z)] = true;

                    int totalProduction = 0;
                    int totalDemand = 0;

                    while (!stack.empty()) {
                        const glm::ivec3 c = stack.back();
                        stack.pop_back();
                        component.push_back(c);

                        const BlockId id = chunk.get(c.x, c.y, c.z);
                        totalProduction += production(id);
                        totalDemand += demand(id);

                        for (const glm::ivec3& n : kNeighbors) {
                            const int nx = c.x + n.x, ny = c.y + n.y, nz = c.z + n.z;
                            if (nx < 0 || nx >= CHUNK_SIZE || ny < 0 || ny >= CHUNK_SIZE ||
                                nz < 0 || nz >= CHUNK_SIZE)
                                continue;
                            if (!isPowerNode(chunk.get(nx, ny, nz))) continue;
                            if (visited[flatIndex(nx, ny, nz)]) continue;
                            visited[flatIndex(nx, ny, nz)] = true;
                            stack.push_back({nx, ny, nz});
                        }
                    }

                    // A network lights up when it actually produces power and
                    // production covers demand.
                    const bool powered = totalProduction > 0 && totalProduction >= totalDemand;
                    if (powered) {
                        for (const glm::ivec3& c : component) {
                            state.setEnergized(c.x, c.y, c.z, true);
                        }
                    }
                }
            }
        }

        return state;
    }

} // namespace PowerSystem
