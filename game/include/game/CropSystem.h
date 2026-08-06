#pragma once

#include "game/Block.h"
#include "game/HashIVec3.h"

#include <glm/glm.hpp>

#include <unordered_map>

class World;

// Crops ripen the way saplings do -- a `pos -> seconds` side registry ticked at
// 20 Hz, faster in the rain, validated against the block still being there --
// but they live HERE rather than in VoxelGame::updateSaplings, as free
// functions over (World&, CropMap&). Two reasons, and only the second is
// aesthetic: growth is the half of farming with real arithmetic in it (four
// stages, two rate multipliers, an irrigation radius), and a member of the
// GL-owning VoxelGame cannot be exercised by the headless --selftest at all.
// The MachineSystem / WorldEdit / DropSystem precedent.
namespace CropSystem {

    // pos -> seconds accrued toward the NEXT stage. Reset on each advance
    // rather than accumulated, so a stage's cost is independent of its
    // predecessors and re-tuning one does not shift the rest.
    using CropMap = std::unordered_map<glm::ivec3, float, IVec3Hash>;

    // Is `id` one of the crop stages, and which one? `stageOf` returns -1 for
    // anything that is not a crop. The stages are contiguous BlockIds by
    // construction (kBlocks is static_asserted into enum order), so this is
    // arithmetic rather than a table -- but it is written once, here, because
    // the mesher, the Harvester and the save all ask the same question.
    bool isCrop(BlockId id);
    int  stageOf(BlockId id);
    BlockId cropAtStage(int stage);

    // The last stage: what a Harvester takes and the only one worth anything.
    bool isRipe(BlockId id);

    // Advance every registered crop one tick, growing faster while `rainy`.
    // Stale entries (the block was broken, or was never a crop) are dropped.
    // A ripe crop stops accruing: it waits to be picked.
    void tick(World& world, CropMap& crops, bool rainy);

} // namespace CropSystem
