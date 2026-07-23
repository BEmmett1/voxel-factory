#include "game/PlayerController.h"

#include "VoxelGameInternal.h"
#include "game/Settings.h"
#include "game/World.h"
#include "engine/Audio.h"
#include "engine/Camera.h"
#include "engine/Input.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

using namespace vg;

namespace {

    // The player's box vs. the world (shared helper, player-sized).
    bool playerCollides(const World& w, const glm::vec3& feet) {
        return boxCollides(w, feet, kPlayerHalfW, kPlayerHeight);
    }

} // namespace

void PlayerController::shove(const glm::vec3& impulse) {
    m_velXZ.x += impulse.x;
    m_velXZ.z += impulse.z;
    m_velY += impulse.y;
}

void PlayerController::damage(float amount, engine::Audio& audio) {
    if (amount <= 0.0f) return;
    health = std::max(0.0f, health - amount);
    audio.play("hurt", kHurtVolume);
}

PlayerController::MoveResult PlayerController::move(
    float dt, engine::Input& input, engine::Camera& camera,
    const World& world, const Settings& settings, engine::Audio& audio) {
    // Mouse look.
    camera.addLook(input.mouseRelX() * settings.sensitivity,
                   -input.mouseRelY() * settings.sensitivity);

    // Walking physics: WASD on the ground plane, gravity, Space to jump.
    // There is no flight -- verticality is scaffolds, hills, and falling.
    glm::vec3 flatFront(camera.front().x, 0.0f, camera.front().z);
    if (glm::dot(flatFront, flatFront) > 1e-6f) flatFront = glm::normalize(flatFront);

    glm::vec3 wish(0.0f);
    if (input.isKeyDown(settings.key(Action::MoveForward))) wish += flatFront;
    if (input.isKeyDown(settings.key(Action::MoveBack))) wish -= flatFront;
    if (input.isKeyDown(settings.key(Action::MoveRight))) wish += camera.right();
    if (input.isKeyDown(settings.key(Action::MoveLeft))) wish -= camera.right();
    float targetSpeed = 0.0f;
    if (glm::dot(wish, wish) > 0.0f) {
        targetSpeed = kWalkSpeed;
        if (input.isKeyDown(settings.key(Action::Sprint))) targetSpeed *= kSprintMult;
        wish = glm::normalize(wish);
    }

    // Momentum: accelerate toward the wanted velocity; friction to a stop
    // when there's no input.
    const glm::vec3 targetVel = wish * targetSpeed;
    const glm::vec3 delta = targetVel - m_velXZ;
    const float deltaLen = glm::length(delta);
    if (deltaLen > 0.0001f) {
        const float rate = (targetSpeed > 0.0f) ? kAccel : kDecel;
        m_velXZ += delta * (std::min(rate * dt, deltaLen) / deltaLen);
    }

    if (m_grounded && input.isKeyDown(settings.key(Action::Jump))) {
        m_velY = kJumpSpeed;
    }
    m_velY = std::max(m_velY - kGravity * dt, -kTerminalVel);

    // Axis-separated move-and-slide against the voxel grid.
    glm::vec3 feet = camera.position - glm::vec3(0.0f, kEyeHeight, 0.0f);

    // Un-stick: if the tick begins with the box already embedded in solid
    // blocks (a node grew into us, a boss knocked us into terrain, a load
    // dropped us in geometry), pop straight up until it clears so the player
    // can never be frozen. Bounded — a pathological fully-enclosed spot leaves
    // `stuck` set, and the axis gates below then let motion through regardless
    // so the player can still walk/fall out rather than lock up.
    bool stuck = playerCollides(world, feet);
    if (stuck) {
        for (int i = 0; i < kUnstickMaxLift && playerCollides(world, feet); ++i)
            feet.y += 1.0f;
        stuck = playerCollides(world, feet);
        m_velY = 0.0f; // don't carry downward velocity while popping out
    }

    glm::vec3 next = feet;
    next.x += m_velXZ.x * dt;
    if (stuck || !playerCollides(world, next)) feet.x = next.x;
    else m_velXZ.x = 0.0f; // ran into a wall
    next = feet;
    next.z += m_velXZ.z * dt;
    if (stuck || !playerCollides(world, next)) feet.z = next.z;
    else m_velXZ.z = 0.0f;

    m_grounded = false;
    next = feet;
    next.y += m_velY * dt;
    if (stuck || !playerCollides(world, next)) {
        feet.y = next.y;
    } else if (m_velY <= 0.0f) {
        feet.y = std::floor(next.y) + 1.0f; // land: snap feet onto the block top
        // Hard landings hurt: damage scales with impact speed beyond the
        // safe threshold (~a 3-block drop).
        const float impact = -m_velY;
        if (impact > kFallSafeSpeed) {
            damage((impact - kFallSafeSpeed) * kFallDamagePerVel, audio);
        }
        m_velY = 0.0f;
        m_grounded = true;
    } else {
        m_velY = 0.0f; // bumped our head
    }

    // Death — by damage or by falling off the island — resets the body on
    // the plateau at full health. The caller applies the pack-loss penalty
    // and the message (one hardcore rule everywhere).
    MoveResult r;
    r.fellOff = feet.y < kVoidY;
    if (r.fellOff || health <= 0.0f) {
        r.died = true;
        feet = spawnFeet();
        m_velY = 0.0f;
        m_velXZ = glm::vec3(0.0f);
        health = kMaxHealth;
    }

    camera.position = feet + glm::vec3(0.0f, kEyeHeight, 0.0f);
    return r;
}
