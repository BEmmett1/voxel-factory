#pragma once

#include "game/Inventory.h"
#include "game/Machine.h"
#include "game/Belt.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

class World;

// Everything that defines a game in progress, as references into VoxelGame's
// state. save() reads through them; load() writes into them (the caller must
// pass them empty/fresh).
struct SaveData {
    World& world;
    Inventory& inventory;
    std::unordered_map<glm::ivec3, Machine, IVec3Hash>& machines;
    std::unordered_map<glm::ivec3, Belt, IVec3Hash>& belts;
    std::unordered_map<glm::ivec3, float, IVec3Hash>& sources;
    std::unordered_map<glm::ivec3, float, IVec3Hash>& saplings;
    bool& weatherRaining;
    float& weatherTimer;
    float& bucketFill;
    glm::vec3& camPos;
    float& camYaw;
    float& camPitch;
    std::uint32_t& worldSeed;
    std::uint32_t& sourceRng;
    int& selectedSlot;
    // Appended in v10. Loading an older save leaves the caller's default
    // untouched (full health), so existing worlds survive the version bump.
    float& health;
    // Appended in v12: the player-assigned hotbar slots (None = empty).
    // Pre-v12 saves keep the caller's default (vg::kDefaultHotbar).
    std::array<ItemId, kHotbarSlots>& hotbar;
};

namespace SaveSystem {
    // Binary format, versioned; load() returns false (leaving the caller to
    // regenerate) on missing file, wrong version, or any corruption.
    bool save(const std::string& path, const SaveData& d);
    bool load(const std::string& path, SaveData& d);
}
