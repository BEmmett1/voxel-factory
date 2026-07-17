#pragma once

#include "engine/BbModel.h"
#include "engine/Mesh.h"
#include "engine/Shader.h"
#include "engine/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class World;
namespace engine {
    class Audio;
    class Camera;
}

// The entity layer's first brick: the test creature. Owns the Blockbench
// model, its GPU resources, and the live instances; operates on World& and
// engine services passed in explicitly (the PowerSystem interface precedent —
// never VoxelGame&). Deliberately NOT persisted: a fresh creature spawns each
// launch, and save records arrive with the full entity layer.
class CreatureSystem {
public:
    // Load model + texture + entity shader from `dir` (the SDL base path).
    // Missing/broken assets leave the system inert: logged, never fatal.
    void loadAssets(const std::string& dir);

    // Spawn the one test wanderer at the hint's x/z, snapped down onto solid
    // ground near the plateau. No ground = no creature (logged).
    void spawnTestCreature(const World& world, const glm::vec3& feetHint);

    // Fixed 20 Hz step: wander decisions, gravity, move-and-slide.
    void update(const World& world);

    // Per-frame clocks: the tick-interpolation alpha, animation time, and the
    // hurt-flash fade. The caller skips this while paused, freezing them all.
    void frameAdvance(float dt);

    // Interpolated skinned draw. rainDim matches the world pass's uRainDim.
    void render(const engine::Camera& camera, float rainDim);

    // Swing the sword along the aim ray: strike the nearest creature within
    // reach, unless a solid block is in the way first. Returns true on a hit
    // (the caller then skips the mining path for this click).
    bool tryMeleeAttack(const World& world, engine::Audio& audio,
                        const glm::vec3& origin, const glm::vec3& dir);

private:
    struct Creature {
        glm::vec3 pos{0.0f}, prevPos{0.0f}; // feet; prevPos = last tick (render lerp)
        glm::vec3 vel{0.0f};
        float yaw = 0.0f;                   // degrees; 0 faces -Z like the model
        glm::vec3 home{0.0f}, target{0.0f};
        float idleTimer = 1.0f;             // counts down while standing
        bool  walking = false, grounded = false;
        int   anim = -1;                    // index into the model's animations
        float animTime = 0.0f;              // frozen while the engine is paused
        std::uint32_t wanderRolls = 0;      // hash counter for wander decisions
        float     hp = 0.0f;                // set from kCreatureHealth on spawn
        glm::vec3 knock{0.0f};              // decaying shove from being hit
        float     hurtFlash = 0.0f;         // 0..1 red tint, fades per frame
    };

    engine::BbModel        m_model;
    bool                   m_ready = false; // model + mesh + shader loaded
    engine::Mesh           m_mesh;
    engine::Texture        m_texture;
    engine::Shader         m_shader;
    std::vector<Creature>  m_creatures;
    std::vector<glm::mat4> m_boneScratch;   // reused per draw
    float m_sinceTick = 0.0f;               // seconds since last update() (render lerp)
};
