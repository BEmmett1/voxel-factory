#pragma once

#include "game/Inventory.h"
#include "game/PlayerController.h"
#include "game/Weather.h"
#include "game/WorldEdit.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>

class World;

// Everything that defines a game in progress, shaped by OWNER rather than as
// a flat field list: the modules that hold persisted state bind directly
// (their public fields are the saved ones), and the derived-state registries
// ride in as the same bundle WorldEdit keeps in sync with the grid. save()
// reads through the references; load() writes into them (the caller must
// pass them empty/fresh). The on-disk byte order is SaveSystem.cpp's alone —
// reshaping this struct never changes the format.
struct SaveData {
    World& world;
    Inventory& inventory;
    WorldEdit::Registries registries; // machines / belts / sources / saplings
    Weather& weather;                 // raining + timer (intensity rebuilds)
    PlayerController& player;         // health (appended in v10; older saves
                                      // keep the caller's full-health default)
    float& bucketFill;
    // Camera pose (the engine owns the camera; the pose is what persists).
    glm::vec3& camPos;
    float& camYaw;
    float& camPitch;
    std::uint32_t& worldSeed;
    std::uint32_t& sourceRng;
    int& selectedSlot;
    // Appended in v12: the player-assigned hotbar slots (None = empty).
    // Pre-v12 saves keep the caller's default (vg::kDefaultHotbar).
    std::array<ItemId, kHotbarSlots>& hotbar;
    // Appended in v13: has the Void Warden ever been defeated (progression
    // flag; the boss itself is transient). Older saves keep the default.
    bool& bossDefeated;
    // Appended in v14: the Tempest's flag (each new boss appends its own).
    bool& tempestDefeated;
};

namespace SaveSystem {
    // Binary format, versioned; load() returns false (leaving the caller to
    // regenerate) on missing file, wrong version, or any corruption.
    bool save(const std::string& path, const SaveData& d);
    bool load(const std::string& path, SaveData& d);
}
