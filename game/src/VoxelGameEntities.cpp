// The test creature: the first brick of the entity layer. Loads a Blockbench
// model (game/assets/models/creature.bbmodel), spawns one wanderer on the
// plateau, steps it in onTick (gravity + the shared box collision), and draws
// it skinned through the entity shader. Deliberately NOT persisted -- a fresh
// creature spawns each launch; save records arrive with the full entity layer.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

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

} // namespace

void VoxelGame::loadCreatureAssets() {
    const char* base = SDL_GetBasePath(); // owned by SDL, do not free
    const std::string dir = base ? base : "";

    // Content, not core: a missing/broken model means a creatureless game
    // with a log line, never a crash (same philosophy as atlas + sounds).
    if (!engine::loadBbModel(dir + kCreatureModel, m_creatureModel,
                             kMaxEntityBones)) {
        SDL_Log("Entities: creature model unavailable -- running without it "
                "(regenerate with: python tools/make_test_model.py)");
        return;
    }

    if (!m_creatureModel.texture.rgba.empty()) {
        m_creatureTex.createFromPixels(m_creatureModel.texture.width,
                                       m_creatureModel.texture.height,
                                       m_creatureModel.texture.rgba.data());
    } else {
        // Loud-but-alive fallback: a magenta/black checker.
        const unsigned char checker[16] = {255, 0, 255, 255, 25, 25, 25, 255,
                                           25, 25, 25, 255, 255, 0, 255, 255};
        m_creatureTex.createFromPixels(2, 2, checker);
    }

    m_creatureMesh.upload(m_creatureModel.vertexData, {3, 3, 2, 1});

    if (!m_entityShader.loadFromFiles(dir + "shaders/entity.vert",
                                      dir + "shaders/entity.frag")) {
        SDL_Log("Entities: entity shader failed to load -- running without "
                "creatures");
        return;
    }
    m_entityShader.use();
    m_entityShader.setInt("uTex", 0);

    m_creatureReady = true;
}

void VoxelGame::spawnTestCreature() {
    if (!m_creatureReady) return;

    // A few blocks from the player spawn, snapped down onto solid ground
    // (works on a modified saved island too). No ground = no creature.
    glm::vec3 feet = spawnFeet() + glm::vec3(4.0f, 0.0f, -3.0f);
    const int x = static_cast<int>(std::floor(feet.x));
    const int z = static_cast<int>(std::floor(feet.z));
    for (int y = kPlateauY + 8; y >= 0; --y) {
        if (isSolid(m_world->getBlock(x, y, z))) {
            feet.y = static_cast<float>(y + 1);
            Creature c;
            c.pos = c.prevPos = c.home = c.target = feet;
            c.anim = m_creatureModel.findAnimation("idle");
            m_creatures.push_back(c);
            return;
        }
    }
    SDL_Log("Entities: no ground at the creature spawn -- skipped");
}

void VoxelGame::updateCreatures() {
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

        // --- Physics: gravity + the player's axis-separated move-and-slide. ---
        c.vel.x = wishVel.x;
        c.vel.z = wishVel.z;
        c.vel.y = std::max(c.vel.y - kGravity * dt, -kTerminalVel);

        bool blockedX = false, blockedZ = false;
        glm::vec3 next = c.pos;
        next.x += c.vel.x * dt;
        if (!boxCollides(*m_world, next, kCreatureHalfW, kCreatureHeight)) c.pos.x = next.x;
        else blockedX = true;
        next = c.pos;
        next.z += c.vel.z * dt;
        if (!boxCollides(*m_world, next, kCreatureHalfW, kCreatureHeight)) c.pos.z = next.z;
        else blockedZ = true;

        c.grounded = false;
        next = c.pos;
        next.y += c.vel.y * dt;
        if (!boxCollides(*m_world, next, kCreatureHalfW, kCreatureHeight)) {
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
        const int want = m_creatureModel.findAnimation(moving ? "walk" : "idle");
        if (want != c.anim) {
            c.anim = want;
            c.animTime = 0.0f;
        }
    }

    m_sinceTick = 0.0f;
}

void VoxelGame::renderCreatures() {
    if (!m_creatureReady || m_creatures.empty()) return;

    m_entityShader.use();
    m_entityShader.setMat4("uProj", camera().projection());
    m_entityShader.setMat4("uView", camera().view());
    m_entityShader.setVec3("uLightDir", kLightDir);
    m_entityShader.setFloat("uRainDim", m_rainIntensity * kRainDimMax);
    m_creatureTex.bind(0);

    // Blend between the last two ticks so 20 Hz movement renders smoothly.
    const float alpha = std::min(m_sinceTick / kTickSeconds, 1.0f);

    for (const Creature& c : m_creatures) {
        engine::evaluateBbPose(m_creatureModel, c.anim, c.animTime, m_boneScratch);
        m_boneScratch.resize(kMaxEntityBones, glm::mat4(1.0f)); // pad to uBones[]

        const glm::vec3 p = glm::mix(c.prevPos, c.pos, alpha);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), p);
        model = glm::rotate(model, glm::radians(c.yaw), {0.0f, 1.0f, 0.0f});
        model = glm::scale(model, glm::vec3(kCreatureScale));
        m_entityShader.setMat4("uModel", model);
        m_entityShader.setMat4Array("uBones", m_boneScratch.data(), kMaxEntityBones);
        m_creatureMesh.draw();
    }
}
