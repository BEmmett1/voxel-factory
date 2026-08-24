#pragma once

#include "game/Block.h"
#include "game/CropSystem.h"
#include "game/HashIVec3.h"
#include "game/Item.h"
#include "game/MachineSystem.h"

#include <glm/glm.hpp>

#include <unordered_map>

class World;

// Placing or breaking a block is never JUST a setBlock: machines, belts,
// sources, and saplings keep registry entries in sync with the grid, and
// power nodes invalidate the network. WorldEdit owns those side effects in
// one place — free functions over the world + registries (the PowerSystem
// precedent). What belongs to the player — inventory, sounds, UI, the
// leaf-sapling pity roll — stays with the caller, driven by returned facts.
namespace WorldEdit {

    // The derived state that must stay in sync with the block grid.
    struct Registries {
        MachineSystem::MachineMap& machines;
        MachineSystem::BeltMap&    belts;
        std::unordered_map<glm::ivec3, float, IVec3Hash>& sources;
        std::unordered_map<glm::ivec3, float, IVec3Hash>& saplings;
        CropSystem::CropMap& crops;
    };

    struct BreakResult {
        BlockId   broken = BlockId::Air;
        ItemStack drop{};          // the block's own yield (blockDrop)
        Inventory returned;        // machine buffers / belt cargo handed back
        bool powerChanged = false; // a power node left the grid: re-solve
        bool brokeLeaves = false;  // caller rolls the sapling chance
    };

    // Remove the block at `pos`: unregister whatever it was (buffered and
    // carried items are collected into `returned`, so nothing is lost),
    // clear growth timers, set Air.
    BreakResult breakBlock(World& world, const Registries& regs, const glm::ivec3& pos);

    struct PlaceResult {
        bool placed = false;
        bool powerChanged = false; // a power node joined the grid: re-solve
    };

    // Place `id` at `pos` if the world allows it (cell not solid; a plant only
    // takes root on ground offering at least its `needsSoil`), registering
    // whatever it is. Belts face
    // `beltFacing`. Player-side rules — stock, not-inside-the-player — are
    // the caller's to check first; a world-side refusal is a silent no-op.
    PlaceResult placeBlock(World& world, const Registries& regs, const glm::ivec3& pos,
                           BlockId id, const glm::ivec3& beltFacing);

    // Re-aim the conduit at `pos`, cycling its facing through the six
    // cardinals (and queueing the arrow remesh). `reverse` steps the other way,
    // which is what keeps overshooting by one from costing five presses.
    // False = no belt there.
    bool rotateBelt(World& world, MachineSystem::BeltMap& belts, const glm::ivec3& pos,
                    bool reverse = false);

    // Source fusion: if `aimed` is a source with an orthogonally-adjacent
    // source of a DIFFERENT type, consume both (partner cell -> Air) and
    // transmute `aimed` into a Resonant Source, keeping the source registry
    // in sync. Returns false (no change) if there is no valid pair. The
    // caller owns the catalyst spend + the sound.
    bool fuseSources(World& world, const Registries& regs, const glm::ivec3& aimed);

    // Till `aimed` into Tilled Soil if it is ground a hoe can work and nothing
    // is sitting on it. The fuseSources shape: a tool RMB transmuting the cell
    // it points at. False = not workable, and the caller plays the deny. The
    // caller owns the sound; there is nothing to spend, because a hoe is a tool
    // and nothing in this game has durability.
    bool tillSoil(World& world, const glm::ivec3& aimed);

    // Enrich `aimed` from Tilled Soil into Rich Soil -- one rung further up the
    // same ladder, same shape as tillSoil. False = not workable, and the caller
    // plays the deny. UNLIKE tillSoil the caller must SPEND its compost, and
    // only on a true return: compost is a plain material, not a tool.
    bool enrichSoil(World& world, const glm::ivec3& aimed);

} // namespace WorldEdit
