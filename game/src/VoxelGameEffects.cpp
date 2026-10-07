// The Alchemy Circle's ritual effect. Pure presentation: the sim reports each
// finished ritual (MachineSystem::CircleCompletion) and this file spends the
// report on particles, a sound and a result icon. Nothing here is saved, and
// nothing here can change what a circle makes.
//
// Two phases. While a STARTED circle crafts, motes stream from each loaded
// pedestal into the core, faster as progress fills (the pedestal items lift
// and brighten too -- that half lives in buildCargoMesh, with the icons). At
// the finish, quick and punchy: the ingredients rush in, a flash, a ring of
// sparks bursting outward, a column of light, and the result popping up over
// the core before it drops into the output.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/AlchemyCircle.h"
#include "game/BlockShape.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

using namespace vg;

namespace {

    float frand(std::minstd_rand& rng, float lo, float hi) {
        return lo + (hi - lo) * std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    }

    glm::vec3 tierColor(bool greater) {
        return greater ? kRitualGreaterColor : kRitualLesserColor;
    }

    // Where a circle cell's floating item sits (buildCargoMesh's resting
    // height, without the bob): the point motes leave from and fly into.
    glm::vec3 itemAnchor(const World& world, const glm::ivec3& cell) {
        const float top = blockBounds(world.getBlock(cell.x, cell.y, cell.z)).hi.y;
        return glm::vec3(cell) + glm::vec3(0.5f, top + kCircleItemLift, 0.5f);
    }

} // namespace

void VoxelGame::updateRitualEffects(float dt) {
    // Circles are Overworld machines; in the arena there is nothing to show,
    // and a burst left over from home must not hang in the void.
    if (m_dimension != DimensionId::Overworld) {
        m_circlesDone.clear();
        m_ritualPops.clear();
        m_particles.clear();
        return;
    }
    const glm::vec3 cam = camera().position;
    const auto nearby = [&](const glm::vec3& p) {
        const glm::vec3 d = p - cam;
        return glm::dot(d, d) <= kRitualFxDist * kRitualFxDist;
    };

    // ---- The finish, once per reported completion ----------------------
    for (const MachineSystem::CircleCompletion& done : m_circlesDone) {
        const glm::vec3 centre = itemAnchor(overworld(), done.core);
        audio().playAt("ritual", centre, kRitualVolume);
        m_ritualPops.push_back({centre, done.made, 0.0f});
        if (!nearby(centre)) continue;
        const glm::vec3 col = tierColor(done.greater);

        // The ingredients rush in from the pedestals they sat on. They are
        // already gone from the pedestals, so these motes ARE them now.
        for (int s = 0; s < AlchemyCircle::kRingSlots; ++s) {
            if (done.consumed[static_cast<std::size_t>(s)] == ItemId::None) continue;
            const glm::vec3 from = itemAnchor(overworld(), AlchemyCircle::slotPos(done.core, s));
            for (int i = 0; i < kRitualConverge; ++i) {
                Particle p;
                p.pos = from + glm::vec3(frand(m_fxRng, -0.15f, 0.15f),
                                         frand(m_fxRng, -0.1f, 0.2f),
                                         frand(m_fxRng, -0.15f, 0.15f));
                p.vel = glm::normalize(centre - from) * frand(m_fxRng, 3.0f, 6.0f) +
                        glm::vec3(0.0f, frand(m_fxRng, 0.5f, 2.0f), 0.0f);
                p.homing = true;
                p.target = centre;
                p.homingPull = 60.0f;
                p.color = col * 1.3f;
                p.size = 0.16f;
                p.sizeEnd = 0.06f;
                p.life = 0.6f;
                m_particles.emit(p);
            }
        }

        // The flash: a big tier-coloured bloom with a white-hot heart. Sized
        // for DAYLIGHT -- additive light on a bright sky only reads once it
        // saturates, so everything here is brighter and larger than it would
        // need to be at night.
        {
            Particle bloom;
            bloom.pos = centre;
            bloom.color = col * 2.0f;
            bloom.size = 0.8f;
            bloom.sizeEnd = 3.6f;
            bloom.life = 0.4f;
            m_particles.emit(bloom);
            Particle heart = bloom;
            heart.color = glm::vec3(1.5f);
            heart.size = 1.8f;
            heart.sizeEnd = 0.2f;
            heart.life = 0.28f;
            m_particles.emit(heart);
        }

        // The burst: sparks thrown outward in every direction, arcing up and
        // falling back.
        for (int i = 0; i < kRitualSparks; ++i) {
            const float a = glm::two_pi<float>() *
                            (static_cast<float>(i) + frand(m_fxRng, -0.3f, 0.3f)) /
                            static_cast<float>(kRitualSparks);
            const float speed = frand(m_fxRng, 4.5f, 8.0f);
            Particle p;
            p.pos = centre;
            p.vel = glm::vec3(std::cos(a) * speed, frand(m_fxRng, 1.5f, 4.5f),
                              std::sin(a) * speed);
            p.gravity = 8.0f;
            p.drag = 2.0f;
            p.color = glm::mix(col, glm::vec3(1.0f), frand(m_fxRng, 0.1f, 0.6f)) * 1.4f;
            p.size = frand(m_fxRng, 0.22f, 0.36f);
            p.sizeEnd = 0.04f;
            p.life = frand(m_fxRng, 0.6f, 1.1f);
            m_particles.emit(p);
        }

        // The shockwave: a flat ring racing out along the ground from the
        // circle's floor, past the pedestals. Evenly spaced and all at one
        // speed, so it reads as a ring rather than as more sparks.
        const glm::vec3 floor = glm::vec3(done.core) + glm::vec3(0.5f, 0.15f, 0.5f);
        for (int i = 0; i < kRitualShockwave; ++i) {
            const float a = glm::two_pi<float>() * static_cast<float>(i) /
                            static_cast<float>(kRitualShockwave);
            Particle p;
            p.pos = floor;
            p.vel = glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 9.0f;
            p.drag = 2.8f; // coasts to a stop around three blocks out
            p.color = col * 1.5f;
            p.size = 0.45f;
            p.sizeEnd = 0.15f;
            p.life = 0.55f;
            m_particles.emit(p);
        }

        // The column: a quick shaft of light off the top of the core.
        const glm::vec3 base = glm::vec3(done.core) +
                               glm::vec3(0.5f, blockBounds(BlockId::RuneCore).hi.y, 0.5f);
        for (int i = 0; i < kRitualColumn; ++i) {
            Particle p;
            p.pos = base + glm::vec3(frand(m_fxRng, -0.2f, 0.2f), frand(m_fxRng, 0.0f, 0.4f),
                                     frand(m_fxRng, -0.2f, 0.2f));
            p.vel = glm::vec3(frand(m_fxRng, -0.3f, 0.3f), frand(m_fxRng, 7.0f, 14.0f),
                              frand(m_fxRng, -0.3f, 0.3f));
            p.drag = 1.4f;
            p.color = glm::mix(col, glm::vec3(1.0f), 0.5f) * 1.4f;
            p.size = frand(m_fxRng, 0.26f, 0.4f);
            p.sizeEnd = 0.06f;
            p.life = frand(m_fxRng, 0.5f, 0.95f);
            m_particles.emit(p);
        }
    }
    m_circlesDone.clear();

    // ---- While crafting: motes stream from each loaded pedestal ---------
    if (dt > 0.0f) {
        for (const auto& [pos, m] : m_machines) {
            if (machineTraits(m.type).kind != MachineKind::RuneCore) continue;
            if (!m.crafting || m.jammed || m.selectedRecipe < 0 || m.craftTime <= 0.0f) continue;
            const glm::vec3 centre = itemAnchor(overworld(), pos);
            if (!nearby(centre)) continue;
            const float frac = glm::clamp(m.progress / m.craftTime, 0.0f, 1.0f);
            const float rate = glm::mix(kRitualMoteRate, kRitualMoteRateEnd, frac);
            const glm::vec3 col = tierColor(
                AlchemyCircle::tierAt(overworld(), m_machines, pos) ==
                AlchemyCircle::Tier::Greater);
            const auto ring = AlchemyCircle::ringContents(overworld(), m_machines, pos);
            for (int s = 0; s < AlchemyCircle::kRingSlots; ++s) {
                if (ring[static_cast<std::size_t>(s)].id == ItemId::None) continue;
                // Whole motes this frame, with the fraction carried by chance
                // rather than by a per-pedestal accumulator.
                const float want = rate * dt;
                int n = static_cast<int>(want);
                if (frand(m_fxRng, 0.0f, 1.0f) < want - static_cast<float>(n)) ++n;
                const glm::vec3 from = itemAnchor(overworld(), AlchemyCircle::slotPos(pos, s));
                for (int i = 0; i < n; ++i) {
                    Particle p;
                    p.pos = from + glm::vec3(frand(m_fxRng, -0.2f, 0.2f),
                                             frand(m_fxRng, -0.15f, 0.15f),
                                             frand(m_fxRng, -0.2f, 0.2f));
                    // Lifted a little first, then drawn in: a shallow arc into
                    // the core, not a laser -- and not a fountain either.
                    p.vel = glm::vec3(frand(m_fxRng, -0.3f, 0.3f), frand(m_fxRng, 0.4f, 1.0f),
                                      frand(m_fxRng, -0.3f, 0.3f));
                    p.homing = true;
                    p.target = centre;
                    p.homingPull = 14.0f;
                    p.drag = 1.2f;
                    p.color = col * 1.2f;
                    p.size = 0.13f;
                    p.sizeEnd = 0.07f;
                    p.life = 2.5f; // dies on arrival long before this
                    m_particles.emit(p);
                }
            }
        }
    }

    m_particles.update(dt);
    for (RitualPop& pop : m_ritualPops) pop.age += dt;
    m_ritualPops.erase(std::remove_if(m_ritualPops.begin(), m_ritualPops.end(),
                                      [](const RitualPop& p) {
                                          return p.age >= kRitualPopSeconds;
                                      }),
                       m_ritualPops.end());
}
