#pragma once

#include <cstdint>

namespace engine {

    // High-resolution frame timer built on SDL's performance counter.
    class Clock {
    public:
        Clock();

        // Seconds elapsed since the previous call to tick().
        float tick();

    private:
        std::uint64_t m_last;
    };

} // namespace engine
