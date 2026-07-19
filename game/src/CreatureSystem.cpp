// The entity layer: ambient wanderers and the boss. Loads Blockbench models
// per species, steps everyone in the ACTIVE dimension at 20 Hz (gravity + the
// shared box collision; bosses aggro, chase, and strike), and draws skinned
// through the entity shader. Deliberately NOT persisted -- creatures are
// transient; save records arrive with the full entity layer.

#include "game/CreatureSystem.h"

#include "VoxelGameInternal.h"
#include "game/Raycast.h"
#include "game/World.h"
#include "engine/Audio.h"
#include "engine/Camera.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>

using namespace vg;

namespace {

    // One row per SpeciesId, in enum order (the kBlocks/kItems discipline).
    constexpr CreatureSpecies kSpecies[] = {
        {.id = SpeciesId::TestCreature, .model = kCreatureModel,
         .kind = CreatureKind::Wanderer, .name = "",
         .scale = kCreatureScale, .halfW = kCreatureHalfW, .height = kCreatureHeight,
         .hp = kCreatureHealth, .walkSpeed = kCreatureWalkSpeed},
        {.id = SpeciesId::VoidWarden, .model = kBossModel,
         .kind = CreatureKind::Boss, .name = "VOID WARDEN",
         .scale = kBossScale, .halfW = kBossHalfW, .height = kBossHeight,
         .hp = kBossHealth, .walkSpeed = kBossWalkSpeed,
         .aggroRadius = kBossAggroRadius, .strikeRange = kBossStrikeRange,
         .damage = kBossDamage, .strikeCooldown = kBossStrikeCooldown,
         .drop = ItemId::VoidCatalyst},
        {.id = SpeciesId::Tempest, .model = kTempestModel,
         .kind = CreatureKind::Boss, .name = "THE TEMPEST",
         .scale = kTempestScale, .halfW = kTempestHalfW, .height = kTempestHeight,
         .hp = kTempestHealth, .walkSpeed = kTempestWalkSpeed,
         .aggroRadius = kTempestAggroRadius, .strikeRange = kTempestStrikeRange,
         .damage = kTempestDamage, .strikeCooldown = kTempestStrikeCooldown,
         .drop = ItemId::StormCore},
    };

    static_assert(std::size(kSpecies) == static_cast<std::size_t>(SpeciesId::Count),
                  "kSpecies needs exactly one row per SpeciesId");

    constexpr bool speciesInEnumOrder() {
        for (std::size_t i = 0; i < std::size(kSpecies); ++i) {
            if (kSpecies[i].id != static_cast<SpeciesId>(i)) return false;
        }
        return true;
    }
    static_assert(speciesInEnumOrder(), "kSpecies rows must be in SpeciesId order");

    const CreatureSpecies& speciesOf(SpeciesId id) {
        return kSpecies[static_cast<std::size_t>(id)];
    }

    // Hash roll in [0, 1) from a creature's decision counter.
    float roll01(std::uint32_t& counter, std::uint32_t salt) {
        return static_cast<float>(hash2(static_cast<int>(counter++), 7, salt) % 1024u) /
               1024.0f;
    }

    // Shortest-arc wrap of an angle difference into [-180, 180).
    float wrapDeg(float a) {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f) a += 360.0f;
        return a - 180.0f;
    }

    // Ray vs AABB slab test; on hit, tOut is the entry distance (>= 0).
    bool rayAabb(const glm::vec3& o, const glm::vec3& d, const glm::vec3& lo,
                 const glm::vec3& hi, float& tOut) {
        float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; ++i) {
            if (std::abs(d[i]) < 1e-8f) {
                if (o[i] < lo[i] || o[i] > hi[i]) return false;
                continue;
            }
            float t0 = (lo[i] - o[i]) / d[i];
            float t1 = (hi[i] - o[i]) / d[i];
            if (t0 > t1) std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMin > tMax) return false;
        }
        tOut = tMin;
        return true;
    }

} // namespace

void CreatureSystem::loadAssets(const std::string& dir) {
    // Content, not core: a missing/broken model means that species never
    // spawns, with a log line -- never a crash (the atlas/sounds philosophy).
    for (std::size_t i = 0; i < std::size(kSpecies); ++i) {
        SpeciesAssets& a = m_assets[i];
        if (!engine::loadBbModel(dir + kSpecies[i].model, a.model, kMaxEntityBones)) {
            SDL_Log("Entities: model '%s' unavailable -- species disabled "
                    "(regenerate with the tools/ scripts)", kSpecies[i].model);
            continue;
        }
        if (!a.model.texture.rgba.empty()) {
            a.texture.createFromPixels(a.model.texture.width, a.model.texture.height,
                                       a.model.texture.rgba.data());
        } else {
            // Loud-but-alive fallback: a magenta/black checker.
            const unsigned char checker[16] = {255, 0, 255, 255, 25, 25, 25, 255,
                                               25, 25, 25, 255, 255, 0, 255, 255};
            a.texture.createFromPixels(2, 2, checker);
        }
        a.mesh.upload(a.model.vertexData, {3, 3, 2, 1});
        a.ready = true;
    }

    if (!m_shader.loadFromFiles(dir + "shaders/entity.vert",
                                dir + "shaders/entity.frag")) {
        SDL_Log("Entities: entity shader failed to load -- running without "
                "creatures");
        return;
    }
    m_shader.use();
    m_shader.setInt("uTex", 0);
    m_shaderReady = true;
}

void CreatureSystem::spawn(SpeciesId species, DimensionId dim, const World& world,
                           const glm::vec3& feetHint) {
    const std::size_t si = static_cast<std::size_t>(species);
    if (!m_shaderReady || !m_assets[si].ready) return;

    // At the hint's column, snapped down onto solid ground (scan starts a few
    // blocks above the hint so a slightly buried hint still finds the top).
    glm::vec3 feet = feetHint;
    const int x = static_cast<int>(std::floor(feet.x));
    const int z = static_cast<int>(std::floor(feet.z));
    for (int y = static_cast<int>(std::floor(feet.y)) + 8; y >= 0; --y) {
        if (isSolid(world.getBlock(x, y, z))) {
            feet.y = static_cast<float>(y + 1);
            Creature c;
            c.species = species;
            c.dim = dim;
            c.pos = c.prevPos = c.home = c.target = feet;
            c.anim = m_assets[si].model.findAnimation("idle");
            c.hp = speciesOf(species).hp;
            m_creatures.push_back(c);
            return;
        }
    }
    SDL_Log("Entities: no ground at a spawn hint -- skipped");
}

void CreatureSystem::clearDimension(DimensionId dim) {
    m_creatures.erase(
        std::remove_if(m_creatures.begin(), m_creatures.end(),
                       [dim](const Creature& c) { return c.dim == dim; }),
        m_creatures.end());
}

CreatureSystem::MeleeResult CreatureSystem::tryMeleeAttack(
    const World& world, engine::Audio& audio,
    const glm::vec3& origin, const glm::vec3& dir, DimensionId active) {
    MeleeResult r;

    // A wall between us and the creature blocks the swing.
    float tBlock = kReach;
    const RaycastHit aim = raycastVoxel(world, origin, dir, kReach);
    if (aim.hit) {
        float t = kReach;
        if (rayAabb(origin, dir, glm::vec3(aim.block), glm::vec3(aim.block) + 1.0f, t)) {
            tBlock = t;
        }
    }

    int   best = -1;
    float bestT = tBlock;
    for (int i = 0; i < static_cast<int>(m_creatures.size()); ++i) {
        const Creature& c = m_creatures[i];
        if (c.dim != active) continue;
        const CreatureSpecies& sp = speciesOf(c.species);
        const glm::vec3 lo = c.pos - glm::vec3(sp.halfW, 0.0f, sp.halfW);
        const glm::vec3 hi = c.pos + glm::vec3(sp.halfW, sp.height, sp.halfW);
        float t = 0.0f;
        if (rayAabb(origin, dir, lo, hi, t) && t <= kReach && t < bestT) {
            bestT = t;
            best = i;
        }
    }
    if (best < 0) return r;
    r.hit = true;

    Creature& c = m_creatures[best];
    const CreatureSpecies& sp = speciesOf(c.species);
    c.hp -= kSwordDamage;
    c.hurtFlash = 1.0f;
    const glm::vec3 center = c.pos + glm::vec3(0.0f, sp.height * 0.5f, 0.0f);
    if (c.hp <= 0.0f) {
        // Down: a lower-pitched thud marks the kill. Bosses hand the caller
        // their unique drop; ambient kinds still have none.
        audio.playAt("hit", center, kHurtVolume, 0.7f);
        if (sp.kind == CreatureKind::Boss) {
            r.bossDied = true;
            r.drop = sp.drop;
            r.bossSpecies = c.species;
            r.bossName = sp.name;
        }
        m_creatures.erase(m_creatures.begin() + best);
        return r;
    }
    audio.playAt("hit", center, kHurtVolume);

    // Shove it away from the player and send it moving.
    glm::vec3 away = c.pos - origin;
    away.y = 0.0f;
    away = (glm::dot(away, away) > 1e-6f) ? glm::normalize(away)
                                          : glm::vec3(0.0f, 0.0f, 1.0f);
    c.knock = away * kKnockback;
    c.vel.y += kKnockUp;
    if (sp.kind == CreatureKind::Wanderer) {
        c.target = c.pos + away * kCreatureWanderRadius; // flee
        c.walking = true;
    } // a Boss shrugs off the shove and keeps coming
    return r;
}

CreatureSystem::Events CreatureSystem::update(const World& world, DimensionId active,
                                              const glm::vec3& playerFeet) {
    const float dt = kTickSeconds;
    Events ev;

    for (Creature& c : m_creatures) {
        if (c.dim != active) continue; // frozen while its dimension sleeps
        const CreatureSpecies& sp = speciesOf(c.species);
        c.prevPos = c.pos;

        // --- Intent: wander or hunt. ---
        glm::vec3 wishVel(0.0f);
        if (sp.kind == CreatureKind::Boss) {
            c.strikeTimer = std::max(0.0f, c.strikeTimer - dt);
            glm::vec3 toPlayer = playerFeet - c.pos;
            const float distXZ = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
            const float dist = glm::length(toPlayer);
            if (dist <= sp.strikeRange && c.strikeTimer <= 0.0f) {
                // Contact strike: damage plus a shove away from the warden,
                // so the fight has a hit-and-close rhythm instead of a hug.
                c.strikeTimer = sp.strikeCooldown;
                ev.damageToPlayer += sp.damage;
                glm::vec3 away = toPlayer;
                away.y = 0.0f;
                away = (glm::dot(away, away) > 1e-6f) ? glm::normalize(away)
                                                      : glm::vec3(0.0f, 0.0f, 1.0f);
                ev.playerKnock += away * kBossKnockback +
                                  glm::vec3(0.0f, kBossKnockUp, 0.0f);
            }
            if (dist <= sp.aggroRadius) {
                c.walking = distXZ > sp.strikeRange * 0.6f; // don't jitter inside the hit box
                c.target = playerFeet;
            } else {
                c.walking = false; // out of sight: the warden waits
            }
        } else if (!c.walking) {
            c.idleTimer -= dt;
            if (c.idleTimer <= 0.0f) {
                const float ang = roll01(c.wanderRolls, 11u) * glm::two_pi<float>();
                const float rad = roll01(c.wanderRolls, 23u) * kCreatureWanderRadius;
                c.target = c.home + glm::vec3(std::cos(ang) * rad, 0.0f,
                                              std::sin(ang) * rad);
                c.walking = true;
            }
        }

        if (c.walking) {
            glm::vec3 d = c.target - c.pos;
            d.y = 0.0f;
            if (glm::dot(d, d) < 0.1f) {
                if (sp.kind == CreatureKind::Wanderer) {
                    c.walking = false;
                    c.idleTimer = kCreatureIdleMin +
                        roll01(c.wanderRolls, 37u) * (kCreatureIdleMax - kCreatureIdleMin);
                }
            } else {
                d = glm::normalize(d);
                wishVel = d * sp.walkSpeed;
                // Face the way we walk (model faces -Z at yaw 0).
                const float desired = glm::degrees(std::atan2(-d.x, -d.z));
                const float delta = wrapDeg(desired - c.yaw);
                const float maxStep = kCreatureTurnRate * dt;
                c.yaw += glm::clamp(delta, -maxStep, maxStep);
            }
        }

        // --- Physics: gravity + the player's axis-separated move-and-slide.
        // Being-hit knockback rides on top of the walk velocity and decays.
        c.knock *= kKnockDecay;
        c.vel.x = wishVel.x + c.knock.x;
        c.vel.z = wishVel.z + c.knock.z;
        c.vel.y = std::max(c.vel.y - kGravity * dt, -kTerminalVel);

        bool blockedX = false, blockedZ = false;
        glm::vec3 next = c.pos;
        next.x += c.vel.x * dt;
        if (!boxCollides(world, next, sp.halfW, sp.height)) c.pos.x = next.x;
        else blockedX = true;
        next = c.pos;
        next.z += c.vel.z * dt;
        if (!boxCollides(world, next, sp.halfW, sp.height)) c.pos.z = next.z;
        else blockedZ = true;

        c.grounded = false;
        next = c.pos;
        next.y += c.vel.y * dt;
        if (!boxCollides(world, next, sp.halfW, sp.height)) {
            c.pos.y = next.y;
        } else if (c.vel.y <= 0.0f) {
            c.pos.y = std::floor(next.y) + 1.0f; // land on the block top
            c.vel.y = 0.0f;
            c.grounded = true;
        } else {
            c.vel.y = 0.0f;
        }

        // A wall on both axes means the leg is unreachable: give up, idle.
        if (c.walking && blockedX && blockedZ && sp.kind == CreatureKind::Wanderer) {
            c.walking = false;
            c.idleTimer = kCreatureIdleMin;
        }

        // Fell out of the world: quietly return home (ambient pets forgive;
        // a boss that somehow leaves the arena resets to its lair).
        if (c.pos.y < kVoidY) {
            c.pos = c.prevPos = c.home;
            c.vel = glm::vec3(0.0f);
            c.walking = false;
        }

        // --- Animation state. ---
        const bool moving = c.walking && (!blockedX || !blockedZ);
        const int want = m_assets[static_cast<std::size_t>(c.species)]
                             .model.findAnimation(moving ? "walk" : "idle");
        if (want != c.anim) {
            c.anim = want;
            c.animTime = 0.0f;
        }
    }

    m_sinceTick = 0.0f;
    return ev;
}

// Animation clocks tick at render rate (menus keep animating, just like the
// sim keeps ticking); the caller skips this while truly paused.
void CreatureSystem::frameAdvance(float dt) {
    m_sinceTick += dt;
    for (Creature& c : m_creatures) {
        c.animTime += dt;
        c.hurtFlash = std::max(0.0f, c.hurtFlash - dt * kFlashDecay);
    }
}

void CreatureSystem::render(const engine::Camera& camera, float rainDim,
                            DimensionId active) {
    if (!m_shaderReady || m_creatures.empty()) return;

    m_shader.use();
    m_shader.setMat4("uProj", camera.projection());
    m_shader.setMat4("uView", camera.view());
    m_shader.setVec3("uLightDir", kLightDir);
    m_shader.setFloat("uRainDim", rainDim);

    // Blend between the last two ticks so 20 Hz movement renders smoothly.
    const float alpha = std::min(m_sinceTick / kTickSeconds, 1.0f);

    for (const Creature& c : m_creatures) {
        if (c.dim != active) continue;
        SpeciesAssets& a = m_assets[static_cast<std::size_t>(c.species)];
        if (!a.ready) continue;
        a.texture.bind(0);

        engine::evaluateBbPose(a.model, c.anim, c.animTime, m_boneScratch);
        m_boneScratch.resize(kMaxEntityBones, glm::mat4(1.0f)); // pad to uBones[]

        const glm::vec3 p = glm::mix(c.prevPos, c.pos, alpha);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), p);
        model = glm::rotate(model, glm::radians(c.yaw), {0.0f, 1.0f, 0.0f});
        model = glm::scale(model, glm::vec3(speciesOf(c.species).scale));
        m_shader.setMat4("uModel", model);
        m_shader.setFloat("uFlash", c.hurtFlash * 0.7f);
        m_shader.setMat4Array("uBones", m_boneScratch.data(), kMaxEntityBones);
        a.mesh.draw();
    }
}

const CreatureSystem::Creature* CreatureSystem::firstBoss(DimensionId dim) const {
    for (const Creature& c : m_creatures) {
        if (c.dim == dim && speciesOf(c.species).kind == CreatureKind::Boss) return &c;
    }
    return nullptr;
}

bool CreatureSystem::bossAlive(DimensionId dim) const {
    return firstBoss(dim) != nullptr;
}

float CreatureSystem::bossHpFrac(DimensionId dim) const {
    const Creature* b = firstBoss(dim);
    if (!b) return 0.0f;
    const float full = speciesOf(b->species).hp;
    return full > 0.0f ? glm::clamp(b->hp / full, 0.0f, 1.0f) : 0.0f;
}

const char* CreatureSystem::bossName(DimensionId dim) const {
    const Creature* b = firstBoss(dim);
    return b ? speciesOf(b->species).name : "";
}
