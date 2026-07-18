#pragma once

#include <glm/glm.hpp>

class World;
struct Settings;
namespace engine {
    class Audio;
    class Camera;
    class Input;
}

// The player's body: mouse look, walking physics (momentum, gravity, jump,
// axis-separated move-and-slide against the voxel grid), fall damage, and the
// hardcore death rule (0 hp or falling off the island). Owns velocities and
// health; the eye position lives on the engine camera, and tools/inventory/UI
// stay with the game. `health` is a public field so SaveData binds straight
// to it (the Weather precedent).
class PlayerController {
public:
    float health = 0.0f; // hearts; seeded from kMaxHealth at start/load

    struct MoveResult {
        bool died = false;    // hit 0 hp or the void: body respawned on the plateau
        bool fellOff = false; // the void flavor of death (picks the title line)
    };

    // One frame of look + walk, moving the camera. On death the body and
    // health reset here; the pack wipe and messaging are the caller's.
    MoveResult move(float dt, engine::Input& input, engine::Camera& camera,
                    const World& world, const Settings& settings,
                    engine::Audio& audio);

    // Hurt the player (fall damage internally; future combat externally).
    void damage(float amount, engine::Audio& audio);

private:
    glm::vec3 m_velXZ{0.0f};      // horizontal velocity (accel/friction; y unused)
    float     m_velY = 0.0f;      // vertical velocity (gravity/jump)
    bool      m_grounded = false; // standing on something this frame?
};
