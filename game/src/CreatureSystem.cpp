// The test creature: the first brick of the entity layer. Loads a Blockbench
// model (game/assets/models/creature.bbmodel), spawns one wanderer on the
// plateau, steps it at 20 Hz (gravity + the shared box collision), and draws
// it skinned through the entity shader. Deliberately NOT persisted -- a fresh
// creature spawns each launch; save records arrive with the full entity layer.

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
#include <limits>

using namespace vg;

namespace {

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
    // Content, not core: a missing/broken model means a creatureless game
    // with a log line, never a crash (same philosophy as atlas + sounds).
    if (!engine::loadBbModel(dir + kCreatureModel, m_model, kMaxEntityBones)) {
        SDL_Log("Entities: creature model unavailable -- running without it "
                "(regenerate with: python tools/make_test_model.py)");
        return;
    }

    if (!m_model.texture.rgba.empty()) {
        m_texture.createFromPixels(m_model.texture.width, m_model.texture.height,
                                   m_model.texture.rgba.data());
    } else {
        // Loud-but-alive fallback: a magenta/black checker.
        const unsigned char checker[16] = {255, 0, 255, 255, 25, 25, 25, 255,
                                           25, 25, 25, 255, 255, 0, 255, 255};
        m_texture.createFromPixels(2, 2, checker);
    }

    m_mesh.upload(m_model.vertexData, {3, 3, 2, 1});

    if (!m_shader.loadFromFiles(dir + "shaders/entity.vert",
                                dir + "shaders/entity.frag")) {
        SDL_Log("Entities: entity shader failed to load -- running without "
                "creatures");
        return;
    }
    m_shader.use();
    m_shader.setInt("uTex", 0);

    m_ready = true;
}

void CreatureSystem::spawnTestCreature(const World& world, const glm::vec3& feetHint) {
    if (!m_ready) return;

    // At the hint's column, snapped down onto solid ground (works on a
    // modified saved island too). No ground = no creature.
    glm::vec3 feet = feetHint;
    const int x = static_cast<int>(std::floor(feet.x));
    const int z = static_cast<int>(std::floor(feet.z));
    for (int y = kPlateauY + 8; y >= 0; --y) {
        if (isSolid(world.getBlock(x, y, z))) {
            feet.y = static_cast<float>(y + 1);
            Creature c;
            c.pos = c.prevPos = c.home = c.target = feet;
            c.anim = m_model.findAnimation("idle");
            c.hp = kCreatureHealth;
            m_creatures.push_back(c);
            return;
        }
    }
    SDL_Log("Entities: no ground at the creature spawn -- skipped");
}

bool CreatureSystem::tryMeleeAttack(const World& world, engine::Audio& audio,
                                    const glm::vec3& origin, const glm::vec3& dir) {
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
        const glm::vec3 lo = c.pos - glm::vec3(kCreatureHalfW, 0.0f, kCreatureHalfW);
        const glm::vec3 hi = c.pos + glm::vec3(kCreatureHalfW, kCreatureHeight,
                                               kCreatureHalfW);
        float t = 0.0f;
        if (rayAabb(origin, dir, lo, hi, t) && t <= kReach && t < bestT) {
            bestT = t;
            best = i;
        }
    }
    if (best < 0) return false;

    Creature& c = m_creatures[best];
    c.hp -= kSwordDamage;
    c.hurtFlash = 1.0f;
    const glm::vec3 center = c.pos + glm::vec3(0.0f, kCreatureHeight * 0.5f, 0.0f);
    if (c.hp <= 0.0f) {
        // Down: a lower-pitched thud marks the kill. No drops yet -- boss
        // loot is the combat pillar's later answer to "why fight".
        audio.playAt("hit", center, kHurtVolume, 0.7f);
        m_creatures.erase(m_creatures.begin() + best);
        return true;
    }
    audio.playAt("hit", center, kHurtVolume);

    // Shove it away from the player and send it fleeing.
    glm::vec3 away = c.pos - origin;
    away.y = 0.0f;
    away = (glm::dot(away, away) > 1e-6f) ? glm::normalize(away)
                                          : glm::vec3(0.0f, 0.0f, 1.0f);
    c.knock = away * kKnockback;
    c.vel.y += kKnockUp;
    c.target = c.pos + away * kCreatureWanderRadius;
    c.walking = true;
    return true;
}

void CreatureSystem::update(const World& world) {
    const float dt = kTickSeconds;

    for (Creature& c : m_creatures) {
        c.prevPos = c.pos;

        // --- Wander decisions. ---
        if (!c.walking) {
            c.idleTimer -= dt;
            if (c.idleTimer <= 0.0f) {
                const float ang = roll01(c.wanderRolls, 11u) * glm::two_pi<float>();
                const float rad = roll01(c.wanderRolls, 23u) * kCreatureWanderRadius;
                c.target = c.home + glm::vec3(std::cos(ang) * rad, 0.0f,
                                              std::sin(ang) * rad);
                c.walking = true;
            }
        }

        glm::vec3 wishVel(0.0f);
        if (c.walking) {
            glm::vec3 d = c.target - c.pos;
            d.y = 0.0f;
            if (glm::dot(d, d) < 0.1f) {
                c.walking = false;
                c.idleTimer = kCreatureIdleMin +
                    roll01(c.wanderRolls, 37u) * (kCreatureIdleMax - kCreatureIdleMin);
            } else {
                d = glm::normalize(d);
                wishVel = d * kCreatureWalkSpeed;
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
        if (!boxCollides(world, next, kCreatureHalfW, kCreatureHeight)) c.pos.x = next.x;
        else blockedX = true;
        next = c.pos;
        next.z += c.vel.z * dt;
        if (!boxCollides(world, next, kCreatureHalfW, kCreatureHeight)) c.pos.z = next.z;
        else blockedZ = true;

        c.grounded = false;
        next = c.pos;
        next.y += c.vel.y * dt;
        if (!boxCollides(world, next, kCreatureHalfW, kCreatureHeight)) {
            c.pos.y = next.y;
        } else if (c.vel.y <= 0.0f) {
            c.pos.y = std::floor(next.y) + 1.0f; // land on the block top
            c.vel.y = 0.0f;
            c.grounded = true;
        } else {
            c.vel.y = 0.0f;
        }

        // A wall on both axes means the leg is unreachable: give up, idle.
        if (c.walking && blockedX && blockedZ) {
            c.walking = false;
            c.idleTimer = kCreatureIdleMin;
        }

        // Fell off the island: quietly return home (no penalty -- it's a pet).
        if (c.pos.y < kVoidY) {
            c.pos = c.prevPos = c.home;
            c.vel = glm::vec3(0.0f);
            c.walking = false;
        }

        // --- Animation state. ---
        const bool moving = c.walking && (!blockedX || !blockedZ);
        const int want = m_model.findAnimation(moving ? "walk" : "idle");
        if (want != c.anim) {
            c.anim = want;
            c.animTime = 0.0f;
        }
    }

    m_sinceTick = 0.0f;
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

void CreatureSystem::render(const engine::Camera& camera, float rainDim) {
    if (!m_ready || m_creatures.empty()) return;

    m_shader.use();
    m_shader.setMat4("uProj", camera.projection());
    m_shader.setMat4("uView", camera.view());
    m_shader.setVec3("uLightDir", kLightDir);
    m_shader.setFloat("uRainDim", rainDim);
    m_texture.bind(0);

    // Blend between the last two ticks so 20 Hz movement renders smoothly.
    const float alpha = std::min(m_sinceTick / kTickSeconds, 1.0f);

    for (const Creature& c : m_creatures) {
        engine::evaluateBbPose(m_model, c.anim, c.animTime, m_boneScratch);
        m_boneScratch.resize(kMaxEntityBones, glm::mat4(1.0f)); // pad to uBones[]

        const glm::vec3 p = glm::mix(c.prevPos, c.pos, alpha);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), p);
        model = glm::rotate(model, glm::radians(c.yaw), {0.0f, 1.0f, 0.0f});
        model = glm::scale(model, glm::vec3(kCreatureScale));
        m_shader.setMat4("uModel", model);
        m_shader.setFloat("uFlash", c.hurtFlash * 0.7f);
        m_shader.setMat4Array("uBones", m_boneScratch.data(), kMaxEntityBones);
        m_mesh.draw();
    }
}
