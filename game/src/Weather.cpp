#include "game/Weather.h"

#include "VoxelGameInternal.h"

#include <glm/glm.hpp>

#include <cstdint>

using namespace vg;

// Rain comes and goes on seeded random phases. Gameplay gates on the boolean;
// visuals ease through `intensity` (see frameEase).
void Weather::tick(std::uint32_t worldSeed) {
    timer -= kTickSeconds;
    if (timer > 0.0f) return;
    raining = !raining;
    const float lo = raining ? kRainMinSeconds : kClearMinSeconds;
    const float hi = raining ? kRainMaxSeconds : kClearMaxSeconds;
    const std::uint32_t h = hash2(311, 977, worldSeed + m_rolls++);
    timer = lo + (hi - lo) * static_cast<float>(h % 1024u) / 1023.0f;
}

void Weather::frameEase(float dt) {
    const float target = raining ? 1.0f : 0.0f;
    const float step = dt / kRainFadeSeconds;
    intensity += glm::clamp(target - intensity, -step, step);
}

void Weather::forceToggle() {
    raining = !raining;
    timer = raining ? 9999.0f : kClearMinSeconds;
}
