#include "engine/Clock.h"

#include <SDL3/SDL.h>

#include <cstdint>

namespace engine {

    Clock::Clock() : m_last(SDL_GetPerformanceCounter()) {}

    float Clock::tick() {
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const std::uint64_t freq = SDL_GetPerformanceFrequency();
        const float dt = static_cast<float>(static_cast<double>(now - m_last) / static_cast<double>(freq));
        m_last = now;
        return dt;
    }

} // namespace engine
