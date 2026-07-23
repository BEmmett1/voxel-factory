#include "game/WorldEdit.h"

#include "game/PowerSystem.h"
#include "game/World.h"

namespace WorldEdit {

BreakResult breakBlock(World& world, const Registries& regs, const glm::ivec3& pos) {
    BreakResult r;
    r.broken = world.getBlock(pos.x, pos.y, pos.z);
    r.drop = blockDrop(r.broken);
    r.brokeLeaves = (r.broken == BlockId::Leaves);
    r.powerChanged = PowerSystem::isPowerNode(r.broken);

    if (isMachine(r.broken)) {
        const auto it = regs.machines.find(pos);
        if (it != regs.machines.end()) {
            // Hand every buffered item back so nothing is lost.
            for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                r.returned.add(id, it->second.input.count(id));
                r.returned.add(id, it->second.output.count(id));
            }
            regs.machines.erase(it);
        }
    }
    if (r.broken == BlockId::Belt) {
        const auto it = regs.belts.find(pos);
        if (it != regs.belts.end()) {
            if (it->second.item != ItemId::None) r.returned.add(it->second.item, 1);
            regs.belts.erase(it);
        }
    }
    if (isSource(r.broken)) regs.sources.erase(pos);   // its item drops instead
    if (r.broken == BlockId::Sapling) regs.saplings.erase(pos);

    world.setBlock(pos.x, pos.y, pos.z, BlockId::Air);
    return r;
}

PlaceResult placeBlock(World& world, const Registries& regs, const glm::ivec3& pos,
                       BlockId id, const glm::ivec3& beltFacing) {
    PlaceResult r;
    if (isSolid(world.getBlock(pos.x, pos.y, pos.z))) return r; // cell taken

    // Saplings only take root in soil.
    if (id == BlockId::Sapling) {
        const BlockId under = world.getBlock(pos.x, pos.y - 1, pos.z);
        if (under != BlockId::Grass && under != BlockId::Dirt) return r;
    }

    world.setBlock(pos.x, pos.y, pos.z, id);
    if (isMachine(id)) {
        Machine m;
        m.type = id;
        regs.machines[pos] = m;
    }
    if (isSource(id)) regs.sources[pos] = 0.0f;        // starts growing a patch
    if (id == BlockId::Sapling) regs.saplings[pos] = 0.0f; // starts the grow timer
    if (id == BlockId::Belt) {
        Belt b;
        b.facing = beltFacing;
        regs.belts[pos] = b;
    }

    r.placed = true;
    r.powerChanged = PowerSystem::isPowerNode(id);
    return r;
}

bool rotateBelt(World& world, MachineSystem::BeltMap& belts, const glm::ivec3& pos) {
    const auto it = belts.find(pos);
    if (it == belts.end()) return false;
    static const glm::ivec3 kCycle[6] = {
        {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    int cur = 0;
    for (int i = 0; i < 6; ++i) {
        if (it->second.facing == kCycle[i]) { cur = i; break; }
    }
    it->second.facing = kCycle[(cur + 1) % 6];
    // No block changed, but the arrow UVs did: queue a remesh.
    world.markDirtyAt(pos.x, pos.y, pos.z);
    return true;
}

bool fuseSources(World& world, const Registries& regs, const glm::ivec3& aimed) {
    const BlockId a = world.getBlock(aimed.x, aimed.y, aimed.z);
    if (!isSource(a)) return false;

    static const glm::ivec3 kNeighbors[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::ivec3& d : kNeighbors) {
        const glm::ivec3 n = aimed + d;
        const BlockId b = world.getBlock(n.x, n.y, n.z);
        if (!isSource(b) || b == a) continue; // need a DIFFERENT source

        // Consume both parents into one Resonant Source at the aimed cell.
        regs.sources.erase(aimed);
        regs.sources.erase(n);
        world.setBlock(n.x, n.y, n.z, BlockId::Air);
        world.setBlock(aimed.x, aimed.y, aimed.z, BlockId::ResonantSource);
        regs.sources[aimed] = 0.0f; // starts growing its patch
        return true;
    }
    return false;
}

} // namespace WorldEdit
