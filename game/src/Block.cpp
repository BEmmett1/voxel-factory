#include "game/Block.h"

#include "game/BlockShape.h"
#include "game/Item.h"
#include "game/Machine.h"

#include <cstddef>
#include <iterator>

namespace {

    using B = BlockId;
    using I = ItemId;
    using T = ToolType;

    // One row per BlockId, in enum order — the static_asserts below make a
    // missing, extra, or misplaced row a compile error, so this can't silently
    // drift from the enum. Omitted fields take BlockInfo's defaults (solid,
    // not a machine/source/node, no glow, no drop).
    constexpr BlockInfo kBlocks[] = {
        {.id = B::Air, .name = "Air", .solid = false, .fullCube = false,
         .shape = ShapeId::Empty},
        // (Every other row keeps the default ShapeId::FullCube.)
        {.id = B::Grass, .name = "Grass", .color = {0.30f, 0.62f, 0.26f},
         .drop = {I::GrassItem, 1}, .tiles = {0, 1, 2}, .hardness = 0.75f, .tool = T::Shovel},
        {.id = B::Dirt, .name = "Dirt", .color = {0.45f, 0.31f, 0.18f},
         .drop = {I::DirtItem, 1}, .tiles = {2, 2, 2}, .hardness = 0.75f, .tool = T::Shovel},
        {.id = B::Stone, .name = "Stone", .color = {0.50f, 0.50f, 0.53f},
         .drop = {I::Stone, 1}, .tiles = {3, 3, 3},
         .hardness = 4.0f, .tool = T::Pickaxe, .toolTier = kTierWood},
        // Equipment / machines (machines show a distinct lid tile on top). Your
        // own placed gear stays retrievable by hand: soft, ungated.
        {.id = B::Generator, .name = "Generator", .color = {0.86f, 0.45f, 0.12f},
         .machine = true, .drop = {I::GeneratorItem, 1}, .tiles = {16, 17, 17}, .hardness = 0.5f},
        {.id = B::Wire, .name = "Wire", .color = {0.82f, 0.72f, 0.20f},
         .drop = {I::WireItem, 1}, .tiles = {18, 18, 18}, .hardness = 0.5f},
        {.id = B::Belt, .name = "Conduit", .color = {0.22f, 0.22f, 0.26f},
         .drop = {I::Conduit, 1}, .tiles = {19, 19, 19}, .hardness = 0.5f},
        {.id = B::Grinder, .name = "Grinder", .color = {0.45f, 0.45f, 0.48f},
         .machine = true, .drop = {I::GrinderItem, 1}, .tiles = {21, 22, 22}, .hardness = 0.5f},
        // The first shaped block: a real 3D model rather than a painted cube,
        // so it no longer fills its cell (does not occlude, does not keep rain
        // out) and you collide with the basin and legs instead of the whole
        // block. Its tiles stay for the item icon and the fallback atlas.
        {.id = B::Cauldron, .name = "Cauldron", .fullCube = false,
         .color = {0.18f, 0.18f, 0.22f},
         .machine = true, .drop = {I::CauldronItem, 1}, .tiles = {23, 24, 24},
         .hardness = 0.5f, .shape = ShapeId::BrewingCauldron},
        {.id = B::Infuser, .name = "Infuser", .fullCube = false,
         .color = {0.40f, 0.62f, 0.60f},
         .machine = true, .drop = {I::InfuserItem, 1}, .tiles = {25, 26, 26},
         .hardness = 0.5f, .shape = ShapeId::ArcaneInfuser},
        {.id = B::Alembic, .name = "Alembic", .fullCube = false,
         .color = {0.72f, 0.58f, 0.28f},
         .machine = true, .drop = {I::AlembicItem, 1}, .tiles = {27, 28, 28},
         .hardness = 0.5f, .shape = ShapeId::AlchemicalAlembic},
        {.id = B::Distiller, .name = "Distiller", .color = {0.58f, 0.30f, 0.55f},
         .machine = true, .drop = {I::DistillerItem, 1}, .tiles = {29, 30, 30}, .hardness = 0.5f},
        {.id = B::Transmuter, .name = "Transmuter", .color = {0.85f, 0.75f, 0.35f},
         .machine = true, .drop = {I::TransmuterItem, 1}, .tiles = {31, 32, 32}, .hardness = 0.5f},
        {.id = B::Miner, .name = "Miner", .fullCube = false,
         .color = {0.35f, 0.40f, 0.46f},
         .machine = true, .drop = {I::MinerItem, 1}, .tiles = {33, 34, 34},
         .hardness = 0.5f, .shape = ShapeId::AugerMiningRig},
        // Resource nodes -> raw materials. Mineral/metal nodes gate behind a
        // pickaxe; the herb (a plant) and sand (loose) break by hand.
        {.id = B::HerbBush, .name = "Herb Bush", .color = {0.20f, 0.55f, 0.22f},
         .node = true, .drop = {I::Herb, 1}, .tiles = {48, 48, 48}, .hardness = 0.4f},
        {.id = B::CrystalNode, .name = "Crystal Node", .color = {0.55f, 0.45f, 0.85f},
         .node = true, .drop = {I::Crystal, 1}, .tiles = {49, 49, 49},
         .hardness = 5.0f, .tool = T::Pickaxe, .toolTier = kTierStone},
        {.id = B::CopperOre, .name = "Copper Ore", .color = {0.70f, 0.45f, 0.30f},
         .node = true, .drop = {I::CopperOre, 1}, .tiles = {50, 50, 50},
         .hardness = 5.0f, .tool = T::Pickaxe, .toolTier = kTierStone},
        {.id = B::SandNode, .name = "Sand", .color = {0.85f, 0.78f, 0.55f},
         .node = true, .drop = {I::Sand, 1}, .tiles = {51, 51, 51}, .hardness = 0.6f, .tool = T::Shovel},
        {.id = B::EssenceVent, .name = "Essence Vent", .color = {0.60f, 0.28f, 0.72f},
         .node = true, .drop = {I::Essence, 1}, .tiles = {53, 53, 53},
         .hardness = 5.0f, .tool = T::Pickaxe, .toolTier = kTierStone},
        // Sources glow and grow their node nearby; mining one drops its
        // placeable item (relocatable).
        {.id = B::SourceHerb, .name = "Herb Source", .color = {0.30f, 0.95f, 0.30f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::HerbBush,
         .drop = {I::HerbSourceItem, 1}, .tiles = {54, 54, 54}, .hardness = 0.5f},
        {.id = B::SourceCrystal, .name = "Crystal Source", .color = {0.75f, 0.55f, 1.00f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::CrystalNode,
         .drop = {I::CrystalSourceItem, 1}, .tiles = {55, 55, 55}, .hardness = 0.5f},
        {.id = B::SourceCopper, .name = "Copper Source", .color = {1.00f, 0.55f, 0.25f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::CopperOre,
         .drop = {I::CopperSourceItem, 1}, .tiles = {56, 56, 56}, .hardness = 0.5f},
        {.id = B::SourceSand, .name = "Sand Source", .color = {1.00f, 0.92f, 0.55f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::SandNode,
         .drop = {I::SandSourceItem, 1}, .tiles = {57, 57, 57}, .hardness = 0.5f},
        {.id = B::SourceEssence, .name = "Essence Source", .color = {0.85f, 0.35f, 1.00f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::EssenceVent,
         .drop = {I::EssenceSourceItem, 1}, .tiles = {59, 59, 59}, .hardness = 0.5f},
        {.id = B::Scaffold, .name = "Scaffold", .color = {0.68f, 0.62f, 0.48f},
         .drop = {I::ScaffoldItem, 1}, .tiles = {4, 4, 4}, .hardness = 0.6f},
        // Forestry. Leaves drop nothing here: the chance sapling drop is
        // rolled at the mining site, not in this table. A log splits into two
        // wood (fuel AND structure).
        {.id = B::Sapling, .name = "Sapling", .color = {0.45f, 0.72f, 0.28f},
         .drop = {I::SaplingItem, 1}, .tiles = {5, 5, 5}, .hardness = 0.2f},
        {.id = B::Log, .name = "Log", .color = {0.45f, 0.33f, 0.18f},
         .drop = {I::Wood, 2}, .tiles = {6, 7, 6},
         .hardness = 3.0f, .tool = T::Axe, .toolTier = kTierWood},
        {.id = B::Leaves, .name = "Leaves", .color = {0.18f, 0.50f, 0.16f},
         .tiles = {8, 8, 8}, .hardness = 0.2f},
        {.id = B::RainBarrel, .name = "Rain Barrel", .color = {0.40f, 0.28f, 0.15f},
         .machine = true, .drop = {I::RainBarrelItem, 1},
         .tiles = {52, 58, 58}, .hardness = 0.5f}, // open water top, stave sides
        {.id = B::VoidStone, .name = "Voidstone", .color = {0.24f, 0.16f, 0.36f},
         .tiles = {60, 60, 60}, // arena ground; deliberately no drop
         .hardness = 4.0f, .tool = T::Pickaxe, .toolTier = kTierWood},
        // Source fusion: the hybrid node + the source that grows it. The
        // Resonant Source rides every data-driven source path (grows via
        // updateSources, drops a relocatable item, glows) with no new sim
        // code; the node is harvested like any other.
        {.id = B::ResonantNode, .name = "Resonant Node", .color = {0.95f, 0.55f, 0.95f},
         .node = true, .drop = {I::Resonance, 1}, .tiles = {61, 61, 61},
         // Gated at IRON: the game's premium raw is what gives the tier above
         // copper something to be for.
         .hardness = 6.0f, .tool = T::Pickaxe, .toolTier = kTierIron},
        {.id = B::ResonantSource, .name = "Resonant Source", .color = {1.00f, 0.60f, 1.00f},
         .emissive = 0.7f, .source = true, .spawnsNode = B::ResonantNode,
         .drop = {I::ResonantSourceItem, 1}, .tiles = {62, 62, 62}, .hardness = 0.5f},
        // Composter machine: composts plant matter into renewable Dirt. Uses
        // spare machine atlas tiles (35 top, 36 side).
        {.id = B::Composter, .name = "Composter", .color = {0.36f, 0.25f, 0.14f},
         .machine = true, .drop = {I::ComposterItem, 1}, .tiles = {35, 36, 36}, .hardness = 0.5f},
        // Forge machine: block-crafts weapons/armor. Uses spare machine atlas
        // tiles (37 top, 38 side).
        {.id = B::Forge, .name = "Forge", .color = {0.30f, 0.22f, 0.24f},
         .machine = true, .drop = {I::ForgeItem, 1}, .tiles = {37, 38, 38}, .hardness = 0.5f},
        // Press machine: forms the shared parts tier. Uses spare machine atlas
        // tiles (39 top, 40 side).
        {.id = B::Press, .name = "Press", .color = {0.42f, 0.44f, 0.52f},
         .machine = true, .drop = {I::PressItem, 1}, .tiles = {39, 40, 40}, .hardness = 0.5f},
        // The Alchemy Circle. Both halves are machines so they get a Machine
        // entity (buffers + progress) and ride the existing save records; the
        // Core glows faintly so a built circle reads as alive at night. Uses
        // spare machine atlas tiles (41/42 core, 43/44 pedestal).
        {.id = B::RuneCore, .name = "Rune Core", .color = {0.34f, 0.28f, 0.46f},
         .emissive = 0.25f, .machine = true, .drop = {I::RuneCoreItem, 1},
         .tiles = {41, 42, 42}, .hardness = 0.5f},
        {.id = B::Pedestal, .name = "Pedestal", .color = {0.58f, 0.56f, 0.62f},
         .machine = true, .drop = {I::PedestalItem, 1}, .tiles = {43, 44, 44},
         .hardness = 0.5f},
        // ---- The recipe overhaul: four powered machines (atlas row 8) ----
        // The Furnace glows: it is the only machine whose "on" state is a
        // fire, and a lit furnace should read across the factory floor at
        // night the way an energized network does.
        {.id = B::Furnace, .name = "Furnace", .color = {0.46f, 0.38f, 0.33f},
         .emissive = 0.20f, .machine = true, .drop = {I::FurnaceItem, 1},
         .tiles = {128, 129, 129}, .hardness = 0.5f},
        {.id = B::Sifter, .name = "Sifter", .color = {0.61f, 0.52f, 0.36f},
         .machine = true, .drop = {I::SifterItem, 1}, .tiles = {130, 131, 131},
         .hardness = 0.5f},
        {.id = B::Glassblower, .name = "Glassblower", .color = {0.36f, 0.44f, 0.49f},
         .machine = true, .drop = {I::GlassblowerItem, 1}, .tiles = {132, 133, 133},
         .hardness = 0.5f},
        {.id = B::Compactor, .name = "Compactor", .color = {0.49f, 0.48f, 0.45f},
         .machine = true, .drop = {I::CompactorItem, 1}, .tiles = {134, 135, 135},
         .hardness = 0.5f},
        // ---- The manual tier (atlas tiles 136-161) -----------------------
        // Thirteen rows of pure data. Each is soft (hardness 0.5) and drops
        // itself, like every other machine, so relocating your hand-cranked
        // starter kit stays free.
        {.id = B::Bloomery, .name = "Bloomery", .color = {0.44f, 0.36f, 0.31f},
         .emissive = 0.18f, .machine = true, .drop = {I::BloomeryItem, 1},
         .tiles = {136, 137, 137}, .hardness = 0.5f},
        {.id = B::Sieve, .name = "Sieve", .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::SieveItem, 1}, .tiles = {138, 139, 139},
         .hardness = 0.5f},
        {.id = B::Blowpipe, .name = "Blowpipe", .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::BlowpipeItem, 1}, .tiles = {140, 141, 141},
         .hardness = 0.5f},
        {.id = B::Tamper, .name = "Tamper", .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::TamperItem, 1}, .tiles = {142, 143, 143},
         .hardness = 0.5f},
        {.id = B::Mortar, .name = "Mortar", .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::MortarItem, 1}, .tiles = {144, 145, 145},
         .hardness = 0.5f},
        {.id = B::HandPress, .name = "Hand Press", .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::HandPressItem, 1}, .tiles = {146, 147, 147},
         .hardness = 0.5f},
        {.id = B::Anvil, .name = "Anvil", .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::AnvilItem, 1}, .tiles = {148, 149, 149},
         .hardness = 0.5f},
        {.id = B::CompostHeap, .name = "Compost Heap", .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::CompostHeapItem, 1}, .tiles = {150, 151, 151},
         .hardness = 0.5f},
        {.id = B::MixingBowl, .name = "Mixing Bowl", .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::MixingBowlItem, 1}, .tiles = {152, 153, 153},
         .hardness = 0.5f},
        {.id = B::InfusionStand, .name = "Infusion Stand", .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::InfusionStandItem, 1}, .tiles = {154, 155, 155},
         .hardness = 0.5f},
        {.id = B::Still, .name = "Still", .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::StillItem, 1}, .tiles = {156, 157, 157},
         .hardness = 0.5f},
        {.id = B::HandDistiller, .name = "Hand Distiller", .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::HandDistillerItem, 1}, .tiles = {158, 159, 159},
         .hardness = 0.5f},
        {.id = B::HandTransmuter, .name = "Hand Transmuter", .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::HandTransmuterItem, 1}, .tiles = {160, 161, 161},
         .hardness = 0.5f},
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

    // A block that fills its cell must be solid: `fullCube` decides what the
    // mesher may hide behind it, so a non-solid one would occlude a face the
    // player can walk and shoot straight through.
    static_assert([] {
        for (const BlockInfo& b : kBlocks) {
            if (b.fullCube && !b.solid) return false;
        }
        return true;
    }(), "fullCube implies solid");

    // Solidity and geometry must agree, or physics and rendering disagree
    // about where a block is: a solid block needs something to stand on and
    // hit, and a non-solid one must occupy nothing. This is what lets the
    // collision and raycast code trust blockBoxes() alone.
    static_assert([] {
        for (const BlockInfo& b : kBlocks) {
            if (b.solid != !blockShape(b.shape).boxes.empty()) return false;
        }
        return true;
    }(), "a block is solid exactly when its shape has collision boxes");

    // A full cube must actually fill its cell, or the mesher hides faces
    // behind a hole.
    static_assert([] {
        for (const BlockInfo& b : kBlocks) {
            if (!b.fullCube) continue;
            const ShapeAabb& s = blockShape(b.shape).bounds;
            if (s.lo != glm::vec3(0.0f) || s.hi != glm::vec3(1.0f)) return false;
        }
        return true;
    }(), "fullCube blocks must have unit-cube bounds");

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
