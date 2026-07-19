#pragma once

#include <cstdint>

// Where the player is. The Overworld is the home island and the ONLY place
// the factory simulation runs (machines, belts, power, growth, weather
// effects) — its registries are Overworld-semantic. The BossArena is a
// transient fight dimension: regenerated on every visit, never saved, no
// automation, no block edits.
enum class DimensionId : std::uint8_t {
    Overworld = 0,
    BossArena,
    Count
};
