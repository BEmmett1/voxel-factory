// Island generation: coastline, hills, the demo lines, source scatter, and
// the one starting tree. Runs once per fresh game (no save to load).

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

using namespace vg;

namespace {

    // Smooth value noise in [0,1]: bilinear interpolation of hashed lattice
    // values with a smoothstep fade. Used for the island heightmap + coastline.
    float valueNoise(float x, float z, std::uint32_t seed) {
        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(x0);
        const float fz = z - static_cast<float>(z0);
        const float sx = fx * fx * (3.0f - 2.0f * fx);
        const float sz = fz * fz * (3.0f - 2.0f * fz);

        auto lattice = [seed](int a, int b) {
            return static_cast<float>(hash2(a, b, seed) % 1024u) / 1023.0f;
        };
        const float a = lattice(x0, z0);
        const float b = lattice(x0 + 1, z0);
        const float c = lattice(x0, z0 + 1);
        const float d = lattice(x0 + 1, z0 + 1);
        return glm::mix(glm::mix(a, b, sx), glm::mix(c, d, sx), sz);
    }

} // namespace

void VoxelGame::buildWorld() {
    // Fresh island layout every launch.
    m_worldSeed = static_cast<std::uint32_t>(SDL_GetPerformanceCounter());

    const int extent = kWorldChunks * CHUNK_SIZE;
    const float cx = extent * 0.5f;
    const float cz = extent * 0.5f;

    // A floating island: irregular coastline, gentle hills toward the center,
    // and an underside that tapers so it reads as a landmass adrift in the sky.
    for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dz = static_cast<float>(z) - cz;
            const float dist = std::sqrt(dx * dx + dz * dz);

            // Irregular coastline: wobble the radial distance with
            // low-frequency noise so the circle grows bays and headlands.
            const float wobble = (valueNoise(x * 0.06f, z * 0.06f, m_worldSeed) - 0.5f) * 14.0f;
            const float coastDist = dist + wobble;
            if (coastDist > kIslandRadius) continue; // open air beyond the coast

            // Gentle hills, rising toward the interior.
            const float inland = 1.0f - glm::clamp(coastDist / kIslandRadius, 0.0f, 1.0f);
            const float hills = valueNoise(x * 0.09f, z * 0.09f, m_worldSeed ^ 0x9e3779b9u);
            int surfaceY = kSurfaceY + static_cast<int>(inland * 4.0f + hills * 3.0f);

            // Flatten the spawn plateau, blending into the terrain at its rim.
            if (dist < kPlateauRadius) {
                surfaceY = kPlateauY;
            } else if (dist < kPlateauRadius + 5.0f) {
                const float t = (dist - kPlateauRadius) / 5.0f;
                surfaceY = static_cast<int>(glm::mix(static_cast<float>(kPlateauY),
                                                     static_cast<float>(surfaceY), t) + 0.5f);
            }

            // Tapered underside: thicker toward the center.
            const int thickness = 3 + static_cast<int>(inland * 8.0f + hills * 2.0f);
            const int bottomY = surfaceY - thickness + 1;

            for (int y = bottomY; y <= surfaceY; ++y) {
                BlockId b = BlockId::Stone;
                if (y == surfaceY) b = BlockId::Grass;
                else if (y >= surfaceY - 2) b = BlockId::Dirt;
                m_world->setBlock(x, y, z, b);
            }
        }
    }

    // Automation demo on the plateau: generator -> wire -> grinder, then a
    // conduit line auto-carries the grinder's ground herb into a cauldron.
    const int cxi = static_cast<int>(cx);
    const int dy = kPlateauY + 1;              // on top of the plateau grass
    const int dzRow = static_cast<int>(cz) - 4; // demo row, just north of center

    m_world->setBlock(cxi - 5, dy, dzRow, BlockId::Generator);
    registerMachine({cxi - 5, dy, dzRow}, BlockId::Generator);
    m_machines[{cxi - 5, dy, dzRow}].input.add(ItemId::Wood, kDemoFuelWood);
    for (int x = cxi - 4; x <= cxi; ++x) {
        m_world->setBlock(x, dy, dzRow, BlockId::Wire);
    }
    m_world->setBlock(cxi + 1, dy, dzRow, BlockId::Grinder);
    registerMachine({cxi + 1, dy, dzRow}, BlockId::Grinder);
    m_machines[{cxi + 1, dy, dzRow}].input.add(ItemId::Herb, 20); // fuel for the demo

    for (int x = cxi + 2; x <= cxi + 4; ++x) {
        m_world->setBlock(x, dy, dzRow, BlockId::Belt);
        registerBelt({x, dy, dzRow}, {1, 0, 0}); // carry items toward +x
    }
    m_world->setBlock(cxi + 5, dy, dzRow, BlockId::Cauldron);
    registerMachine({cxi + 5, dy, dzRow}, BlockId::Cauldron);

    // Wire spur alongside the belts so the cauldron is powered too (belts are
    // not power nodes, so the network can't reach it through them).
    for (int x = cxi + 1; x <= cxi + 5; ++x) {
        m_world->setBlock(x, dy, dzRow - 1, BlockId::Wire);
    }

    // A rain barrel feeds the cauldron water whenever the sky opens up --
    // rain is the island's only water.
    m_world->setBlock(cxi + 7, dy, dzRow, BlockId::RainBarrel);
    registerMachine({cxi + 7, dy, dzRow}, BlockId::RainBarrel);
    m_world->setBlock(cxi + 6, dy, dzRow, BlockId::Belt);
    registerBelt({cxi + 6, dy, dzRow}, {-1, 0, 0}); // carry toward the cauldron

    // Mining demo on the plateau's south side: a herb source grows a patch,
    // a powered miner harvests it, and belts carry the herb away.
    const int mz = static_cast<int>(cz) + 3;
    m_world->setBlock(cxi - 7, dy, mz, BlockId::SourceHerb);
    m_sources[{cxi - 7, dy, mz}] = 0.0f;
    m_world->setBlock(cxi - 5, dy, mz, BlockId::Miner);
    registerMachine({cxi - 5, dy, mz}, BlockId::Miner);
    m_world->setBlock(cxi - 4, dy, mz, BlockId::Generator);
    registerMachine({cxi - 4, dy, mz}, BlockId::Generator);
    m_machines[{cxi - 4, dy, mz}].input.add(ItemId::Wood, kDemoFuelWood);
    for (int i = 1; i <= 2; ++i) {
        m_world->setBlock(cxi - 5, dy, mz - i, BlockId::Belt);
        registerBelt({cxi - 5, dy, mz - i}, {0, 0, -1}); // carry toward the demo row
    }

    // Scatter glowing resource sources across the island (seeded random),
    // keeping the plateau clear. Each will grow a patch of its node type.
    const BlockId sourceTypes[5] = {BlockId::SourceHerb, BlockId::SourceCopper,
                                    BlockId::SourceSand, BlockId::SourceCrystal,
                                    BlockId::SourceEssence};
    const int sourceCounts[5] = {4, 4, 3, 3, 2};

    for (int t = 0; t < 5; ++t) {
        int placed = 0;
        for (int attempt = 0; attempt < 500 && placed < sourceCounts[t]; ++attempt) {
            const std::uint32_t h = hash2(t * 977 + attempt, attempt * 131 + 7,
                                          m_worldSeed ^ 0xABCD1234u);
            const int x = static_cast<int>(h % static_cast<std::uint32_t>(extent));
            const int z = static_cast<int>((h >> 10) % static_cast<std::uint32_t>(extent));

            const float ddx = static_cast<float>(x) - cx;
            const float ddz = static_cast<float>(z) - cz;
            // Push sources to the island's outer band: reaching them is the
            // logistics problem belts and miners exist to solve.
            if (std::sqrt(ddx * ddx + ddz * ddz) < kSourceMinRadius) continue;

            // Needs a grass surface with air above.
            int gy = -1;
            for (int y = kSurfaceY + 8; y >= kSurfaceY - 2; --y) {
                if (m_world->getBlock(x, y, z) == BlockId::Grass &&
                    m_world->getBlock(x, y + 1, z) == BlockId::Air) {
                    gy = y;
                    break;
                }
            }
            if (gy < 0) continue;

            m_world->setBlock(x, gy + 1, z, sourceTypes[t]);
            m_sources[{x, gy + 1, z}] = 0.0f;
            ++placed;
        }
    }

    // One grown tree near the middle seeds forestry: chopping its leaves is
    // the only starting supply of saplings. Seeded random spot on plateau
    // grass, clear of the demo rows and the spawn point.
    const glm::vec3 spawn = spawnFeet();
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::uint32_t h = hash2(attempt * 53 + 11, attempt * 197 + 3,
                                      m_worldSeed ^ 0x07EE5EEDu);
        const int tx = cxi + static_cast<int>(h % 17u) - 8;
        const int tz = static_cast<int>(cz) + static_cast<int>((h >> 8) % 17u) - 8;
        if (std::abs(tz - dzRow) <= 1 || std::abs(tz - mz) <= 1) continue;
        if (std::abs(tx - static_cast<int>(spawn.x)) <= 1 &&
            std::abs(tz - static_cast<int>(spawn.z)) <= 1) continue;

        const int ty = kPlateauY + 1;
        if (m_world->getBlock(tx, ty - 1, tz) != BlockId::Grass) continue;
        bool clear = true;
        for (const TreeCell& c : treeCells()) {
            if (m_world->getBlock(tx + c.offset.x, ty + c.offset.y,
                                  tz + c.offset.z) != BlockId::Air) {
                clear = false;
                break;
            }
        }
        if (!clear) continue;

        placeTree(*m_world, {tx, ty, tz});
        break;
    }

    // Pre-grow each patch a little so raws are minable immediately.
    for (int round = 0; round < 3; ++round) {
        for (auto& [pos, timer] : m_sources) timer = kSourceSpawnSeconds;
        updateSources();
    }
}
