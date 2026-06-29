#pragma once

#include <glm/glm.hpp>

namespace engine {

    // Perspective fly/FPS camera. Orientation is yaw/pitch in degrees; the rest
    // of the engine reads view()/projection() each frame.
    class Camera {
    public:
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        float yaw   = -90.0f; // degrees; -90 looks down -Z
        float pitch = 0.0f;   // degrees; clamped to (-89, 89)

        float fovDegrees = 70.0f;
        float nearPlane  = 0.1f;
        float farPlane   = 500.0f;
        float aspect     = 16.0f / 9.0f;

        glm::vec3 front() const;
        glm::vec3 right() const;
        glm::vec3 up() const;

        glm::mat4 view() const;
        glm::mat4 projection() const;

        // Apply a mouse-look delta (in degrees) with pitch clamping.
        void addLook(float dYaw, float dPitch);
    };

} // namespace engine
