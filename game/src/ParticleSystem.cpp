// Glowing additive particles. See ParticleSystem.h for why additive and why
// the depth buffer is read but never written.

#include "engine/GL.h" // must precede other GL-touching headers

#include "game/ParticleSystem.h"

#include "engine/Camera.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace {
    // Enough for several rituals at once with room to spare; one ritual spends
    // a little over a hundred.
    constexpr std::size_t kMaxParticles = 2048;
}

bool ParticleSystem::init(const std::string& shaderDir) {
    m_ready = m_shader.loadFromFiles(shaderDir + "particle.vert", shaderDir + "particle.frag");
    if (!m_ready) SDL_Log("Particles disabled: could not load the particle shaders");
    m_live.reserve(kMaxParticles);
    return m_ready;
}

void ParticleSystem::emit(Particle p) {
    p.maxLife = std::max(p.life, 1e-3f);
    if (m_live.size() < kMaxParticles) {
        m_live.push_back(p);
        return;
    }
    m_live[m_nextRecycle] = p;
    m_nextRecycle = (m_nextRecycle + 1) % kMaxParticles;
}

void ParticleSystem::update(float dt) {
    if (dt <= 0.0f) return;
    for (Particle& p : m_live) {
        p.life -= dt;
        if (p.homing) {
            const glm::vec3 to = p.target - p.pos;
            const float d = glm::length(to);
            // Arrived: done. The step test stops a fast mote overshooting and
            // orbiting the target forever.
            if (d < 0.08f || d < glm::length(p.vel) * dt) { p.life = 0.0f; continue; }
            p.vel += (to / d) * (p.homingPull * dt);
        }
        p.vel.y -= p.gravity * dt;
        p.vel *= std::max(0.0f, 1.0f - p.drag * dt);
        p.pos += p.vel * dt;
    }
    m_live.erase(std::remove_if(m_live.begin(), m_live.end(),
                                [](const Particle& p) { return p.life <= 0.0f; }),
                 m_live.end());
    if (m_nextRecycle >= m_live.size()) m_nextRecycle = 0;
}

void ParticleSystem::render(const engine::Camera& cam) {
    if (!m_ready || m_live.empty()) return;

    const glm::vec3 right = cam.right();
    const glm::vec3 up = cam.up();
    m_scratch.clear();
    m_scratch.reserve(m_live.size() * 6 * 8);
    for (const Particle& p : m_live) {
        const float t = 1.0f - p.life / p.maxLife; // 0 at birth, 1 at death
        const float size = p.size + (p.sizeEnd - p.size) * t;
        // Bright at birth, gone at death. Squared so a spark reads as a flash
        // that dies, not a light that dims.
        const float a = (1.0f - t) * (1.0f - t);
        const glm::vec3 rx = right * (size * 0.5f);
        const glm::vec3 ry = up * (size * 0.5f);
        const glm::vec3 c[4] = {p.pos - rx - ry, p.pos + rx - ry,
                                p.pos + rx + ry, p.pos - rx + ry};
        const glm::vec2 uv[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int k : {0, 1, 2, 0, 2, 3}) {
            m_scratch.insert(m_scratch.end(), {c[k].x, c[k].y, c[k].z, uv[k].x, uv[k].y,
                                               p.color.r * a, p.color.g * a, p.color.b * a});
        }
    }
    m_mesh.upload(m_scratch, {3, 2, 3}, GL_DYNAMIC_DRAW);

    m_shader.use();
    m_shader.setMat4("uView", cam.view());
    m_shader.setMat4("uProj", cam.projection());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // additive: colour is pre-multiplied by fade
    glDepthMask(GL_FALSE);
    m_mesh.draw();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
