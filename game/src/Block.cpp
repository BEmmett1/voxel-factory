#include "game/Block.h"

#include "game/Item.h"
#include "game/Machine.h"

#include <cstddef>
#include <iterator>

namespace {

    using B = BlockId;
    using I = ItemId;

    // One row per BlockId, in enum order — the static_asserts below make a
    // missing, extra, or misplaced row a compile error, so this can't silently
    // drift from the enum. Omitted fields take BlockInfo's defaults (solid,
    // not a machine/source/node, no glow, no drop).
    constexpr BlockInfo kBlocks[] = {
        {.id = B::Air, .name = "Air", .solid = false},
        {.id = B::Grass, .name = "Grass", .color = {0.30f, 0.62f, 0.26f},
         .drop = {I::GrassItem, 1}, .tiles = {0, 1, 2}},
        {.id = B::Dirt, .name = "Dirt", .color = {0.45f, 0.31f, 0.18f},
         .drop = {I::DirtItem, 1}, .tiles = {2, 2, 2}},
        {.id = B::Stone, .name = "Stone", .color = {0.50f, 0.50f, 0.53f},
         .drop = {I::Stone, 1}, .tiles = {3, 3, 3}},
        // Equipment / machines (machines show a distinct lid tile on top).
        {.id = B::Generator, .name = "Generator", .color = {0.86f, 0.45f, 0.12f},
         .machine = true, .drop = {I::GeneratorItem, 1}, .tiles = {16, 17, 17}},
        {.id = B::Wire, .name = "Wire", .color = {0.82f, 0.72f, 0.20f},
         .drop = {I::WireItem, 1}, .tiles = {18, 18, 18}},
        {.id = B::Belt, .name = "Conduit", .color = {0.22f, 0.22f, 0.26f},
         .drop = {I::Conduit, 1}, .tiles = {19, 19, 19}},
        {.id = B::Grinder, .name = "Grinder", .color = {0.45f, 0.45f, 0.48f},
         .machine = true, .drop = {I::GrinderItem, 1}, .tiles = {21, 22, 22}},
        {.id = B::Cauldron, .name = "Cauldron", .color = {0.18f, 0.18f, 0.22f},
         .machine = true, .drop = {I::CauldronItem, 1}, .tiles = {23, 24, 24}},
        {.id = B::Infuser, .name = "Infuser", .color = {0.40f, 0.62f, 0.60f},
         .machine = true, .drop = {I::InfuserItem, 1}, .tiles = {25, 26, 26}},
        {.id = B::Alembic, .name = "Alembic", .color = {0.72f, 0.58f, 0.28f},
         .machine = true, .drop = {I::AlembicItem, 1}, .tiles = {27, 28, 28}},
        {.id = B::Distiller, .name = "Distiller", .color = {0.58f, 0.30f, 0.55f},
         .machine = true, .drop = {I::DistillerItem, 1}, .tiles = {29, 30, 30}},
        {.id = B::Transmuter, .name = "Transmuter", .color = {0.85f, 0.75f, 0.35f},
         .machine = true, .drop = {I::TransmuterItem, 1}, .tiles = {31, 32, 32}},
        {.id = B::Miner, .name = "Miner", .color = {0.35f, 0.40f, 0.46f},
         .machine = true, .drop = {I::MinerItem, 1}, .tiles = {33, 34, 34}},
        // Resource nodes -> raw materials.
        {.id = B::HerbBush, .name = "Herb Bush", .color = {0.20f, 0.55f, 0.22f},
         .node = true, .drop = {I::Herb, 1}, .tiles = {48, 48, 48}},
        {.id = B::CrystalNode, .name = "Crystal Node", .color = {0.55f, 0.45f, 0.85f},
         .node = true, .drop = {I::Crystal, 1}, .tiles = {49, 49, 49}},
        {.id = B::CopperOre, .name = "Copper Ore", .color = {0.70f, 0.45f, 0.30f},
         .node = true, .drop = {I::CopperOre, 1}, .tiles = {50, 50, 50}},
        {.id = B::SandNode, .name = "Sand", .color = {0.85f, 0.78f, 0.55f},
         .node = true, .drop = {I::Sand, 1}, .tiles = {51, 51, 51}},
        {.id = B::EssenceVent, .name = "Essence Vent", .color = {0.60f, 0.28f, 0.72f},
         .node = true, .drop = {I::Essence, 1}, .tiles = {53, 53, 53}},
        // Sources glow and grow their node nearby; mining one drops its
        // placeable item (relocatable).
        {.id = B::SourceHerb, .name = "Herb Source", .color = {0.30f, 0.95f, 0.30f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::HerbBush,
         .drop = {I::HerbSourceItem, 1}, .tiles = {54, 54, 54}},
        {.id = B::SourceCrystal, .name = "Crystal Source", .color = {0.75f, 0.55f, 1.00f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::CrystalNode,
         .drop = {I::CrystalSourceItem, 1}, .tiles = {55, 55, 55}},
        {.id = B::SourceCopper, .name = "Copper Source", .color = {1.00f, 0.55f, 0.25f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::CopperOre,
         .drop = {I::CopperSourceItem, 1}, .tiles = {56, 56, 56}},
        {.id = B::SourceSand, .name = "Sand Source", .color = {1.00f, 0.92f, 0.55f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::SandNode,
         .drop = {I::SandSourceItem, 1}, .tiles = {57, 57, 57}},
        {.id = B::SourceEssence, .name = "Essence Source", .color = {0.85f, 0.35f, 1.00f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::EssenceVent,
         .drop = {I::EssenceSourceItem, 1}, .tiles = {59, 59, 59}},
        {.id = B::Scaffold, .name = "Scaffold", .color = {0.68f, 0.62f, 0.48f},
         .drop = {I::ScaffoldItem, 1}, .tiles = {4, 4, 4}},
        // Forestry. Leaves drop nothing here: the chance sapling drop is
        // rolled at the mining site, not in this table. A log splits into two
        // wood (fuel AND structure).
        {.id = B::Sapling, .name = "Sapling", .color = {0.45f, 0.72f, 0.28f},
         .drop = {I::SaplingItem, 1}, .tiles = {5, 5, 5}},
        {.id = B::Log, .name = "Log", .color = {0.45f, 0.33f, 0.18f},
         .drop = {I::Wood, 2}, .tiles = {6, 7, 6}},
        {.id = B::Leaves, .name = "Leaves", .color = {0.18f, 0.50f, 0.16f},
         .tiles = {8, 8, 8}},
        {.id = B::RainBarrel, .name = "Rain Barrel", .color = {0.40f, 0.28f, 0.15f},
         .machine = true, .drop = {I::RainBarrelItem, 1},
         .tiles = {52, 58, 58}}, // open water top, stave sides
    };

    static_assert(std::size(kBlocks) == static_cast<std::size_t>(BlockId::Count),
                  "kBlocks needs exactly one row per BlockId");

    constexpr bool blocksInEnumOrder() {
        for (std::size_t i = 0; i < std::size(kBlocks); ++i) {
            if (kBlocks[i].id != static_cast<BlockId>(i)) return false;
        }
        return true;
    }
    static_assert(blocksInEnumOrder(), "kBlocks rows must be in BlockId enum order");

    // The machine traits registry (Machine.h) and the machine flags here must
    // name exactly the same blocks — a machine without a traits row (or a
    // traits row for a non-machine) is a compile error, not a runtime surprise.
    static_assert([] {
        for (const BlockInfo& b : kBlocks) {
            bool hasTraits = false;
            for (const MachineTraits& t : kMachineTraits) {
                if (t.block == b.id) hasTraits = true;
            }
            if (b.machine != hasTraits) return false;
        }
        return true;
    }(), "kMachineTraits must have one row per machine block (and only machine blocks)");

} // namespace

const BlockInfo& blockInfo(BlockId id) {
    return kBlocks[static_cast<std::size_t>(id)];
}
