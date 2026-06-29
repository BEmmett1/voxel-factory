#include "engine/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace engine {

    namespace {
        constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
    }

    glm::vec3 Camera::front() const {
        const float y = glm::radians(yaw);
        const float p = glm::radians(pitch);
        return glm::normalize(glm::vec3{
            std::cos(y) * std::cos(p),
            std::sin(p),
            std::sin(y) * std::cos(p)});
    }

    glm::vec3 Camera::right() const {
        return glm::normalize(glm::cross(front(), kWorldUp));
    }

    glm::vec3 Camera::up() const {
        return glm::normalize(glm::cross(right(), front()));
    }

    glm::mat4 Camera::view() const {
        return glm::lookAt(position, position + front(), kWorldUp);
    }

    glm::mat4 Camera::projection() const {
        return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    }

    void Camera::addLook(float dYaw, float dPitch) {
        yaw += dYaw;
        pitch = std::clamp(pitch + dPitch, -89.0f, 89.0f);
    }

} // namespace engine
