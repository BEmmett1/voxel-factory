#pragma once

#include <cstdint>

// The rain/clear state machine. Gameplay gates on `raining`; visuals ease
// through `intensity`, advanced per frame while the sim tick flips phases.
// `raining` + `timer` are persisted by SaveSystem (public fields bind into
// SaveData's reference bundle); `intensity` rebuilds each run. The rain MESH
// stays on the render side — this is the state, not the streaks.
struct Weather {
    bool  raining = false;
    float timer = 120.0f;    // seconds left in the current phase
    float intensity = 0.0f;  // smoothed 0..1 visual ease toward `raining`

    // One 20 Hz step: count the phase down; on expiry flip rain/clear and
    // roll the next duration from the world seed.
    void tick(std::uint32_t worldSeed);

    // Per-frame ease of `intensity` toward the phase bool.
    void frameEase(float dt);

    // F4 dev key: summon (effectively endless) rain / clear the sky now.
    void forceToggle();

private:
    std::uint32_t m_rolls = 0; // phase-duration RNG counter
};
