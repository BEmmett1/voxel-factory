#pragma once

#include "game/Inventory.h"
#include "game/Drop.h"
#include "game/PlayerController.h"
#include "game/Weather.h"
#include "game/WorldEdit.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
    // Appended in v15: total seconds of active play (drives the slot-picker
    // cards). Older saves keep the caller's default (0).
    double& playtime;
    // Appended in v16: physical ground items (mining yields not yet collected,
    // a death-scattered pack). Only Overworld drops are written; older saves
    // load with the caller's default (empty). load() clears then fills it.
    std::vector<DroppedItem>& drops;
    // Appended in v18: equipped armor (head/body/feet; None = empty). Older
    // saves keep the caller's default (all empty).
    std::array<ItemId, kArmorSlots>& armor;
};

// A small sidecar (`<save>.meta`) written next to each slot's save file so the
// main menu's slot picker can list every slot — timestamp, playtime, progress —
// WITHOUT loading (and version-gating) the full save. Versioned independently
// of the save format; a missing/foreign meta just yields an "unknown" card.
struct SlotMeta {
    std::uint32_t saveVersion = 0;    // the save format this slot was written by
    std::uint64_t unixTime = 0;       // wall-clock time of the last save
    std::uint32_t playtimeSeconds = 0;
    std::uint8_t  bossProgress = 0;   // bit0 = Void Warden, bit1 = Tempest
};

namespace SaveSystem {
    // Binary format, versioned; load() returns false (leaving the caller to
    // regenerate) on missing file, wrong version, or any corruption. A
    // successful save() also (best-effort) refreshes the `<path>.meta` sidecar.
    bool save(const std::string& path, const SaveData& d);
    bool load(const std::string& path, SaveData& d);

    // Translate a PRE-v20 Machine::selectedRecipe (a position in the recipe
    // list as it stood then) into a runtime index today, by way of the key it
    // named at the time. -1 = AUTO, which is also where a recipe that has
    // since been deleted lands.
    //
    // Exposed only so --selftest can pin it: it is the one piece of the load
    // path that reads a format nothing can write any more, so a regression
    // here would silently mis-lock every old save rather than fail loudly.
    int legacyRecipeIndex(std::uint32_t version, BlockId machine, std::int32_t saved);

    // The sidecar path for a save path ("<path>.meta").
    std::string metaPath(const std::string& savePath);
    // Read a slot's sidecar. Returns false (out left default) if absent/foreign.
    bool readMeta(const std::string& savePath, SlotMeta& out);
}
