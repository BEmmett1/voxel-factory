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
            // Hand every buffered item back so nothing is lost -- including the
            // fuel slot, or the charcoal in a broken Furnace burns for nobody.
            for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                r.returned.add(id, it->second.input.count(id));
                r.returned.add(id, it->second.output.count(id));
                r.returned.add(id, it->second.fuel.count(id));
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
    if (blockInfo(r.broken).treeSize > 0) regs.saplings.erase(pos);
    if (CropSystem::isCrop(r.broken)) regs.crops.erase(pos);

    world.setBlock(pos.x, pos.y, pos.z, BlockId::Air);
    return r;
}

PlaceResult placeBlock(World& world, const Registries& regs, const glm::ivec3& pos,
                       BlockId id, const glm::ivec3& beltFacing) {
    PlaceResult r;
    if (isSolid(world.getBlock(pos.x, pos.y, pos.z))) return r; // cell taken

    // Plants are picky about what they sit on: a sapling wants soil, a crop
    // wants ground that has been worked. Both are one registry field now, so a
    // content pack can add a third plant without touching this file.
    if (blockInfo(id).needsSoil != SoilKind::None &&
        !soilAccepts(world.getBlock(pos.x, pos.y - 1, pos.z), id)) {
        return r;
    }

    world.setBlock(pos.x, pos.y, pos.z, id);
    if (isMachine(id)) {
        Machine m;
        m.type = id;
        regs.machines[pos] = m;
    }
    if (isSource(id)) regs.sources[pos] = 0.0f;        // starts growing a patch
    // Any sapling, of any size: the BLOCK says which tree it becomes, so the
    // registry stays a plain pos -> float and a second one needed no field.
    if (blockInfo(id).treeSize > 0) regs.saplings[pos] = 0.0f; // starts the grow timer
    if (CropSystem::isCrop(id)) regs.crops[pos] = 0.0f;    // starts ripening
    if (id == BlockId::Belt) {
        Belt b;
        b.facing = beltFacing;
        regs.belts[pos] = b;
    }

    r.placed = true;
    r.powerChanged = PowerSystem::isPowerNode(id);
    return r;
}

bool rotateBelt(World& world, MachineSystem::BeltMap& belts, const glm::ivec3& pos,
                bool reverse) {
    const auto it = belts.find(pos);
    if (it == belts.end()) return false;
    static const glm::ivec3 kCycle[6] = {
        {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    int cur = 0;
    for (int i = 0; i < 6; ++i) {
        if (it->second.facing == kCycle[i]) { cur = i; break; }
    }
    // Six one-way steps meant a belt that landed one notch past where you
    // wanted it cost five presses. Reversing costs one branch and caps the
    // worst case at two.
    it->second.facing = kCycle[(cur + (reverse ? 5 : 1)) % 6];
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

bool tillSoil(World& world, const glm::ivec3& aimed) {
    const BlockId under = world.getBlock(aimed.x, aimed.y, aimed.z);
    // Only plain soil works: tilling already-tilled ground is a no-op rather
    // than a deny, and nothing else is ground.
    if (blockInfo(under).provides != SoilKind::Soil) return false;
    // A field needs open sky above it to be worth anything, and more to the
    // point tilling under a placed block would strand it on soil it no longer
    // sits on.
    if (isSolid(world.getBlock(aimed.x, aimed.y + 1, aimed.z))) return false;

    world.setBlock(aimed.x, aimed.y, aimed.z, BlockId::TilledSoil);
    return true;
}

bool enrichSoil(World& world, const glm::ivec3& aimed) {
    // Tilled ground only, and tested by BLOCK rather than by `provides < Rich`:
    // this is one rung of a ladder you climb with the hoe first and compost
    // second, and saying so directly is what makes enriching already-rich soil
    // a silent no-op instead of an accident.
    if (world.getBlock(aimed.x, aimed.y, aimed.z) != BlockId::TilledSoil) return false;
    // Same reason tillSoil wants a clear cell: enriching under a placed block
    // would strand it on ground it no longer sits on.
    if (isSolid(world.getBlock(aimed.x, aimed.y + 1, aimed.z))) return false;

    world.setBlock(aimed.x, aimed.y, aimed.z, BlockId::RichSoil);
    return true;
}

} // namespace WorldEdit
