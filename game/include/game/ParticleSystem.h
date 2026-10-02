#pragma once

#include "engine/Mesh.h"
#include "engine/Shader.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine { class Camera; }

// Glowing points: sparks, motes, flashes. The first user is the Alchemy
// Circle's ritual, but nothing here knows what a circle is -- a caller emits
// Particles and the system moves, fades and draws them.
//
// Drawn ADDITIVELY with the depth test on and depth WRITES off. Additive is
// order-independent, so the pool never has to be sorted back to front (the
// same reason the world uses alpha cutout rather than blending), and not
// writing depth means two sparks never hide each other while a wall still
// hides both. Pure presentation: never saved, never read by the simulation,
// and advanced on the caller's pause-aware dt, so pausing freezes a burst
// mid-air.
struct Particle {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec3 color{1.0f};
    float     size = 0.1f;     // quad edge in blocks, at birth
    float     sizeEnd = 0.0f;  // ...and at death (linear between)
    float     life = 1.0f;     // seconds remaining
    float     maxLife = 1.0f;  // seconds total (set by emit from `life`)
    float     gravity = 0.0f;  // blocks/s^2 downward
    float     drag = 0.0f;     // fraction of velocity lost per second
    // When set, the particle is pulled toward `target` and dies on arrival --
    // a mote flying into the centre of a circle, whatever it was thrown with.
    bool      homing = false;
    glm::vec3 target{0.0f};
    float     homingPull = 0.0f; // blocks/s^2 toward target
};

class ParticleSystem {
public:
    // Compiles the particle program. False (logged) leaves the system inert:
    // effects missing is never fatal.
    bool init(const std::string& shaderDir);

    // Adds one particle; once the pool is full an existing one is recycled
    // (round-robin, so roughly the older ones), so a burst never allocates and
    // an effect-heavy frame degrades by trimming sparks rather than by growing.
    void emit(Particle p);
    void update(float dt);
    void clear() { m_live.clear(); }
    std::size_t count() const { return m_live.size(); }

    void render(const engine::Camera& cam);

private:
    std::vector<Particle> m_live;
    std::size_t           m_nextRecycle = 0;
    std::vector<float>    m_scratch;
    engine::Mesh          m_mesh;
    engine::Shader        m_shader;
    bool                  m_ready = false;
};
