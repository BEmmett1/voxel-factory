// Crop growth: the sapling registry pattern with a stage counter on it.
// See CropSystem.h for why this is free functions rather than another
// VoxelGame::update* member.

#include "game/CropSystem.h"

#include "VoxelGameInternal.h"
#include "game/World.h"

#include <cstdlib>
#include <vector>

using namespace vg;

namespace CropSystem {

namespace {
    // The stages are contiguous by construction: kBlocks is static_asserted
    // into BlockId order, so HerbCrop0..HerbCrop3 are consecutive ordinals and
    // a stage is arithmetic rather than a lookup.
    constexpr int kFirst = static_cast<int>(BlockId::HerbCrop0);
    constexpr int kLast = static_cast<int>(BlockId::HerbCrop3);
} // namespace

bool isCrop(BlockId id) {
    const int o = static_cast<int>(id);
    return o >= kFirst && o <= kLast;
}

int stageOf(BlockId id) {
    return isCrop(id) ? static_cast<int>(id) - kFirst : -1;
}

BlockId cropAtStage(int stage) {
    if (stage < 0) stage = 0;
    if (stage > kLast - kFirst) stage = kLast - kFirst;
    return static_cast<BlockId>(kFirst + stage);
}

bool isRipe(BlockId id) { return id == BlockId::HerbCrop3; }

void tick(World& world, CropMap& crops, bool rainy,
          const std::vector<glm::ivec3>& wetSources) {
    std::vector<glm::ivec3> done;

    // Cheap because the list is the RUNNING irrigators, not every machine --
    // usually empty, rarely more than a handful.
    const auto irrigated = [&](const glm::ivec3& p) {
        for (const glm::ivec3& s : wetSources) {
            const glm::ivec3 d = p - s;
            if (std::abs(d.x) <= kIrrigateRadius && std::abs(d.z) <= kIrrigateRadius &&
                std::abs(d.y) <= 3) {
                return true;
            }
        }
        return false;
    };

    for (auto& [pos, timer] : crops) {
        const BlockId here = world.getBlock(pos.x, pos.y, pos.z);
        if (!isCrop(here)) {
            done.push_back(pos); // harvested, broken, or never ours
            continue;
        }
        // Ripe is the end of the line. It waits to be picked, and until it is,
        // this cell costs nothing but the map entry -- which is also what
        // stops a mature field from burning tick time forever.
        if (isRipe(here)) continue;

        // Rain and irrigation share ONE multiplier on purpose: a machine that
        // buys weather independence must not also stack into a third rate.
        const bool wet = rainy || irrigated(pos);
        timer += kTickSeconds * (wet ? kRainGrowthMult : 1.0f);
        if (timer < kCropStageSeconds) continue;

        // Soil can be dug out from under a planted crop, and a crop with
        // nothing to stand in dies rather than quietly ripening in mid-air.
        if (!soilAccepts(world.getBlock(pos.x, pos.y - 1, pos.z), here)) {
            world.setBlock(pos.x, pos.y, pos.z, BlockId::Air);
            done.push_back(pos);
            continue;
        }

        // Reset rather than subtract: a stage's cost is then independent of
        // its predecessors, so retuning one does not shift the whole ladder.
        timer = 0.0f;
        world.setBlock(pos.x, pos.y, pos.z, cropAtStage(stageOf(here) + 1));
    }

    for (const glm::ivec3& p : done) crops.erase(p);
}

} // namespace CropSystem
