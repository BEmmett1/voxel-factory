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

    // The ruin's footprint, as offsets from its origin (the centre of its north
    // row). plantRuin must stay inside this and the siting code must level
    // exactly it -- a machine outside the pad is a machine buried in a hillside.
    constexpr int kRuinMinX = -7;
    constexpr int kRuinMaxX = 7;
    constexpr int kRuinMinZ = -1; // the wire spur runs one row north of the line
    constexpr int kRuinMaxZ = 7;  // the mining bay is seven rows south of it

} // namespace

// The derelict factory: a power line feeding a grinder, a conduit run into a
// cauldron, a rain barrel plumbed in, and a mining bay south of it. Laid out
// exactly as it was on the plateau, because as a TEACHING object it works --
// what was wrong was where it stood and how much it gave away.
//
// Stocked to LIMP. It still has a couple of wood and a few herb in it, so it is
// running when you arrive and stops while you watch, which teaches what it
// needs far better than either a dead ruin or a full one.
void VoxelGame::plantRuin(const glm::ivec3& origin) {
    const int ox = origin.x, oy = origin.y, oz = origin.z;

    m_world->setBlock(ox - 5, oy, oz, BlockId::Generator);
    registerMachine({ox - 5, oy, oz}, BlockId::Generator);
    m_machines[{ox - 5, oy, oz}].input.add(ItemId::Wood, kRuinFuelWood);
    for (int x = ox - 4; x <= ox; ++x) {
        m_world->setBlock(x, oy, oz, BlockId::Wire);
    }
    m_world->setBlock(ox + 1, oy, oz, BlockId::Grinder);
    registerMachine({ox + 1, oy, oz}, BlockId::Grinder);
    m_machines[{ox + 1, oy, oz}].input.add(ItemId::Herb, kRuinHerb);

    for (int x = ox + 2; x <= ox + 4; ++x) {
        m_world->setBlock(x, oy, oz, BlockId::Belt);
        registerBelt({x, oy, oz}, {1, 0, 0}); // carry items toward +x
    }
    m_world->setBlock(ox + 5, oy, oz, BlockId::Cauldron);
    registerMachine({ox + 5, oy, oz}, BlockId::Cauldron);

    // Wire spur alongside the belts so the cauldron is powered too (belts are
    // not power nodes, so the network can't reach it through them).
    for (int x = ox + 1; x <= ox + 5; ++x) {
        m_world->setBlock(x, oy, oz - 1, BlockId::Wire);
    }

    // A rain barrel feeds the cauldron water whenever the sky opens up --
    // rain is the island's only water.
    m_world->setBlock(ox + 7, oy, oz, BlockId::RainBarrel);
    registerMachine({ox + 7, oy, oz}, BlockId::RainBarrel);
    m_world->setBlock(ox + 6, oy, oz, BlockId::Belt);
    registerBelt({ox + 6, oy, oz}, {-1, 0, 0}); // carry toward the cauldron

    // The mining bay: a herb source grows a patch, a powered miner harvests it,
    // and belts carry the herb north to the line. The source came out here with
    // the rest of it -- one five blocks from spawn contradicted the whole rule
    // that sources sit past kSourceMinRadius so reaching them is the problem.
    const int mz = oz + kRuinMaxZ;
    m_world->setBlock(ox - 7, oy, mz, BlockId::SourceHerb);
    m_sources[{ox - 7, oy, mz}] = 0.0f;
    m_world->setBlock(ox - 5, oy, mz, BlockId::Miner);
    registerMachine({ox - 5, oy, mz}, BlockId::Miner);
    m_world->setBlock(ox - 4, oy, mz, BlockId::Generator);
    registerMachine({ox - 4, oy, mz}, BlockId::Generator);
    m_machines[{ox - 4, oy, mz}].input.add(ItemId::Wood, kRuinFuelWood);
    for (int i = 1; i <= 2; ++i) {
        m_world->setBlock(ox - 5, oy, mz - i, BlockId::Belt);
        registerBelt({ox - 5, oy, mz - i}, {0, 0, -1}); // carry toward the line
    }
}

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

    const int cxi = static_cast<int>(cx);

    // ---- The ruin -------------------------------------------------------
    // This used to be a working demo line on the spawn plateau, and it made
    // the "empty starting kit" a fiction: every block of it is hardness 0.5
    // and ungated (so your own gear stays retrievable), and breakBlock hands
    // back every buffered item -- so a bare-handed player five blocks from
    // spawn collected 16 Wood, 20 Herb, seven machines and a Herb Source.
    // Sixteen wood alone is a Bucket, a Sieve and a Crate without ever owning
    // an axe, which is the tree, the tool ladder and the Bloomery skipped.
    //
    // Out past the sources it is a REWARD for walking instead, and the walk is
    // the tutorial. Stamped BEFORE the source scatter below, so the scatter's
    // existing "grass with air above" test declines to land inside it without
    // having to be told it exists.
    bool ruinPlaced = false;
    for (int attempt = 0; attempt < 400; ++attempt) {
        const std::uint32_t h = hash2(attempt * 313 + 17, attempt * 89 + 5,
                                      m_worldSeed ^ 0x2D15C0DEu);
        const int rx = static_cast<int>(h % static_cast<std::uint32_t>(extent));
        const int rz = static_cast<int>((h >> 11) % static_cast<std::uint32_t>(extent));

        const float rdx = static_cast<float>(rx) - cx;
        const float rdz = static_cast<float>(rz) - cz;
        const float rdist = std::sqrt(rdx * rdx + rdz * rdz);
        if (rdist < kRuinMinRadius) continue; // too close to be a journey
        if (rdist > kRuinMaxRadius) continue; // saves attempts; the land test decides

        // Every column of the footprint has to find land within one levelling
        // step of the anchor. Hunting for naturally flat ground out here would
        // almost always fail -- surfaceY varies by several blocks across a
        // 15-wide span -- but a ruin is a BUILT thing, so its builders levelled
        // the site, and the slack is how much levelling we let them have done.
        const auto surfaceAt = [&](int x, int z) {
            for (int y = kSurfaceY + 12; y >= kSurfaceY - 8; --y) {
                if (m_world->getBlock(x, y, z) != BlockId::Air) return y;
            }
            return -1;
        };
        const int floorY = surfaceAt(rx, rz);
        if (floorY < 0) continue;

        bool sited = true;
        for (int dz = kRuinMinZ - 1; dz <= kRuinMaxZ + 1 && sited; ++dz) {
            for (int dx = kRuinMinX - 1; dx <= kRuinMaxX + 1; ++dx) {
                const int s = surfaceAt(rx + dx, rz + dz);
                if (s < 0 || std::abs(s - floorY) > kRuinLevelSlack) {
                    sited = false;
                    break;
                }
            }
        }
        if (!sited) continue;

        // Level it: cut down to the floor and fill up to it. Both bounds are
        // driven by the slack the test above just enforced, which is what makes
        // this safe in either direction -- clearing kRuinHeadroom (> slack)
        // always tops the highest hummock, and filling slack+1 cells BELOW the
        // floor always reaches the lowest natural surface, so the pad can never
        // be left hanging over a dip with nothing under it.
        static_assert(kRuinHeadroom > kRuinLevelSlack,
                      "clearing must reach a column standing kRuinLevelSlack proud");
        for (int dz = kRuinMinZ - 1; dz <= kRuinMaxZ + 1; ++dz) {
            for (int dx = kRuinMinX - 1; dx <= kRuinMaxX + 1; ++dx) {
                const int x = rx + dx, z = rz + dz;
                for (int y = floorY + 1; y <= floorY + kRuinHeadroom; ++y) {
                    m_world->setBlock(x, y, z, BlockId::Air);
                }
                m_world->setBlock(x, floorY, z, BlockId::Grass);
                for (int y = floorY - 1; y >= floorY - kRuinLevelSlack - 1; --y) {
                    m_world->setBlock(x, y, z, BlockId::Dirt);
                }
            }
        }

        plantRuin({rx, floorY + 1, rz});
        ruinPlaced = true;
        SDL_Log("worldgen: ruin at %d,%d (%.0f from centre)", rx, rz,
                static_cast<double>(rdist));
        break;
    }
    if (!ruinPlaced) {
        // Never fatal and never a retry loop: worldgen has to finish. A ruinless
        // island is playable -- it was never a kit -- it is just quieter.
        SDL_Log("worldgen: found no level site for the ruin; skipping it");
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
    // the only starting supply of saplings, and with the ruin moved out it is
    // now the ONLY thing on the plateau besides you. Seeded random spot on
    // plateau grass, clear of the spawn point. (It used to dodge the two demo
    // rows as well; there are no demo rows here any more.)
    const glm::vec3 spawn = spawnFeet();
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::uint32_t h = hash2(attempt * 53 + 11, attempt * 197 + 3,
                                      m_worldSeed ^ 0x07EE5EEDu);
        const int tx = cxi + static_cast<int>(h % 17u) - 8;
        const int tz = static_cast<int>(cz) + static_cast<int>((h >> 8) % 17u) - 8;
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

// A boss island (the BossArena dimension): a bare voidstone disc over the
// void — no resources, no cover to farm, nothing to build with. Deterministic
// and regenerated fresh on every visit. Centered on the arena's own origin.
// The Void Warden gets a wide disc with four watch pillars; the Tempest gets
// a tighter ring with a broken rim lip — its knockback wants you off it.
void VoxelGame::buildArena(World& w, SpeciesId boss) {
    const int radius = boss == SpeciesId::Tempest ? kTempestArenaRadius : kArenaRadius;
    for (int z = -radius; z <= radius; ++z) {
        for (int x = -radius; x <= radius; ++x) {
            const int d2 = x * x + z * z;
            if (d2 > radius * radius) continue;
            // Tapered underside: thicker toward the middle.
            const int depth = d2 < (radius * radius) / 3 ? 3 : 2;
            for (int dy = 0; dy < depth; ++dy) {
                w.setBlock(x, kArenaY - dy, z, BlockId::VoidStone);
            }
        }
    }

    if (boss == SpeciesId::Tempest) {
        // A broken rim lip: cover from the wind in places, gaps in others.
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                const int d2 = x * x + z * z;
                if (d2 > radius * radius || d2 < (radius - 2) * (radius - 2)) continue;
                if ((x * 7 + z * 13) % 5 < 2) continue; // the gaps
                w.setBlock(x, kArenaY + 1, z, BlockId::VoidStone);
            }
        }
    } else {
        // Four watch pillars on the diagonals.
        for (const auto& [px, pz] : {std::pair{-7, -7}, {7, -7}, {-7, 7}, {7, 7}}) {
            for (int dy = 1; dy <= 4; ++dy) {
                w.setBlock(px, kArenaY + dy, pz, BlockId::VoidStone);
            }
        }
    }
}
