#include "game/Block.h"

#include "game/BlockShape.h"
#include "game/Item.h"
#include "game/Machine.h"

#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using B = BlockId;
    using I = ItemId;
    using T = ToolType;

    // One row per BlockId, in enum order — the static_asserts below make a
    // missing, extra, or misplaced row a compile error, so this can't silently
    // drift from the enum. Omitted fields take BlockInfo's defaults (solid,
    // not a machine/source/node, no glow, no drop).
    constexpr BlockInfo kBlocks[] = {
        {.id = B::Air, .key = "core:air",
         .name = "Air", .solid = false, .fullCube = false,
         .shape = ShapeId::Empty},
        // (Every other row keeps the default ShapeId::FullCube.)
        {.id = B::Grass, .key = "core:grass",
         .name = "Grass", .color = {0.30f, 0.62f, 0.26f},
         .drop = {I::GrassItem, 1}, .tiles = {0, 1, 2}, .hardness = 0.75f, .tool = T::Shovel,
         .provides = SoilKind::Soil},
        {.id = B::Dirt, .key = "core:dirt",
         .name = "Dirt", .color = {0.45f, 0.31f, 0.18f},
         .drop = {I::DirtItem, 1}, .tiles = {2, 2, 2}, .hardness = 0.75f, .tool = T::Shovel,
         .provides = SoilKind::Soil},
        {.id = B::Stone, .key = "core:stone",
         .name = "Stone", .color = {0.50f, 0.50f, 0.53f},
         .drop = {I::Stone, 1}, .tiles = {3, 3, 3},
         .hardness = 4.0f, .tool = T::Pickaxe, .toolTier = kTierWood},
        // Equipment / machines (machines show a distinct lid tile on top). Your
        // own placed gear stays retrievable by hand: soft, ungated.
        {.id = B::Generator, .key = "core:generator",
         .name = "Generator", .fullCube = false, .color = {0.86f, 0.45f, 0.12f},
         .machine = true, .drop = {I::GeneratorItem, 1}, .tiles = {16, 17, 17}, .hardness = 0.5f, .shape = ShapeId::Generator},
        // Wire and Conduit are the two blocks whose whole FUNCTION is being
        // thin, so a painted cube was the one place the art contradicted the
        // mechanic. Both are hubs with six arms drawn per neighbour
        // (kConnectParts); fullCube = false is what lets you see between them,
        // and costs the usual three things: they no longer occlude, no longer
        // keep rain out, and no longer stop grass spreading -- all correct for
        // something you can see daylight through.
        {.id = B::Wire, .key = "core:wire",
         .name = "Wire", .fullCube = false, .color = {0.82f, 0.72f, 0.20f},
         .drop = {I::WireItem, 1}, .tiles = {18, 18, 18}, .hardness = 0.5f,
         .shape = ShapeId::WireHub},
        {.id = B::Belt, .key = "core:belt",
         .name = "Conduit", .fullCube = false, .color = {0.22f, 0.22f, 0.26f},
         .drop = {I::Conduit, 1}, .tiles = {19, 19, 19}, .hardness = 0.5f,
         .shape = ShapeId::ConduitHub},
        {.id = B::Grinder, .key = "core:grinder",
         .name = "Grinder", .fullCube = false, .color = {0.45f, 0.45f, 0.48f},
         .machine = true, .drop = {I::GrinderItem, 1}, .tiles = {21, 22, 22}, .hardness = 0.5f, .shape = ShapeId::Grinder},
        // The first shaped block: a real 3D model rather than a painted cube,
        // so it no longer fills its cell (does not occlude, does not keep rain
        // out) and you collide with the basin and legs instead of the whole
        // block. Its tiles stay for the item icon and the fallback atlas.
        {.id = B::Cauldron, .key = "core:cauldron",
         .name = "Cauldron", .fullCube = false,
         .color = {0.18f, 0.18f, 0.22f},
         .machine = true, .drop = {I::CauldronItem, 1}, .tiles = {23, 24, 24},
         .hardness = 0.5f, .shape = ShapeId::BrewingCauldron},
        {.id = B::Infuser, .key = "core:infuser",
         .name = "Infuser", .fullCube = false,
         .color = {0.40f, 0.62f, 0.60f},
         .machine = true, .drop = {I::InfuserItem, 1}, .tiles = {25, 26, 26},
         .hardness = 0.5f, .shape = ShapeId::ArcaneInfuser},
        {.id = B::Alembic, .key = "core:alembic",
         .name = "Alembic", .fullCube = false,
         .color = {0.72f, 0.58f, 0.28f},
         .machine = true, .drop = {I::AlembicItem, 1}, .tiles = {27, 28, 28},
         .hardness = 0.5f, .shape = ShapeId::AlchemicalAlembic},
        {.id = B::Distiller, .key = "core:distiller",
         .name = "Distiller", .fullCube = false, .color = {0.58f, 0.30f, 0.55f},
         .machine = true, .drop = {I::DistillerItem, 1}, .tiles = {29, 30, 30}, .hardness = 0.5f, .shape = ShapeId::Distiller},
        {.id = B::Transmuter, .key = "core:transmuter",
         .name = "Transmuter", .fullCube = false, .color = {0.85f, 0.75f, 0.35f},
         .machine = true, .drop = {I::TransmuterItem, 1}, .tiles = {31, 32, 32}, .hardness = 0.5f, .shape = ShapeId::Transmuter},
        {.id = B::Miner, .key = "core:miner",
         .name = "Miner", .fullCube = false,
         .color = {0.35f, 0.40f, 0.46f},
         .machine = true, .drop = {I::MinerItem, 1}, .tiles = {33, 34, 34},
         .hardness = 0.5f, .shape = ShapeId::AugerMiningRig},
        // Resource nodes -> raw materials. Mineral/metal nodes gate behind a
        // pickaxe; the herb (a plant) and sand (loose) break by hand.
        // Nodes and sources read as OBJECTS sitting on the world rather than
        // as terrain, which a painted cube could never do -- and the sources
        // are the island's landmarks, the thing you navigate by on the way out
        // past the plateau.
        {.id = B::HerbBush, .key = "core:herb_bush",
         .name = "Herb Bush", .fullCube = false, .color = {0.20f, 0.55f, 0.22f},
         .node = true, .drop = {I::Herb, 1}, .tiles = {48, 48, 48}, .hardness = 0.4f,
         .shape = ShapeId::HerbBush},
        {.id = B::CrystalNode, .key = "core:crystal_node",
         .name = "Crystal Node", .fullCube = false, .color = {0.55f, 0.45f, 0.85f},
         .node = true, .drop = {I::Crystal, 1}, .tiles = {49, 49, 49},
         .hardness = 5.0f, .tool = T::Pickaxe, .toolTier = kTierStone,
         .shape = ShapeId::VioletCrystalCluster},
        {.id = B::CopperOre, .key = "core:copper_ore",
         .name = "Copper Ore", .color = {0.70f, 0.45f, 0.30f},
         .node = true, .drop = {I::CopperOre, 1}, .tiles = {50, 50, 50},
         .hardness = 5.0f, .tool = T::Pickaxe, .toolTier = kTierStone},
        {.id = B::SandNode, .key = "core:sand_node",
         .name = "Sand", .color = {0.85f, 0.78f, 0.55f},
         .node = true, .drop = {I::Sand, 1}, .tiles = {51, 51, 51}, .hardness = 0.6f, .tool = T::Shovel},
        {.id = B::EssenceVent, .key = "core:essence_vent",
         .name = "Essence Vent", .fullCube = false, .color = {0.60f, 0.28f, 0.72f},
         .node = true, .drop = {I::Essence, 1}, .tiles = {53, 53, 53},
         .hardness = 5.0f, .tool = T::Pickaxe, .toolTier = kTierStone,
         .shape = ShapeId::DarkStoneVent},
        // Sources glow and grow their node nearby; mining one drops its
        // placeable item (relocatable).
        // All six sources are modelled now. They share a silhouette and differ
        // by stone, crown and colour, which is what lets them work as the
        // landmarks you steer by from across the island.
        {.id = B::SourceHerb, .key = "core:source_herb",
         .name = "Herb Source", .fullCube = false, .color = {0.30f, 0.95f, 0.30f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::HerbBush,
         .drop = {I::HerbSourceItem, 1}, .tiles = {54, 54, 54}, .hardness = 0.5f,
         .shape = ShapeId::MossyShrineStandingStone},
        {.id = B::SourceCrystal, .key = "core:source_crystal",
         .name = "Crystal Source", .fullCube = false, .color = {0.75f, 0.55f, 1.00f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::CrystalNode,
         .drop = {I::CrystalSourceItem, 1}, .tiles = {55, 55, 55}, .hardness = 0.5f,
         .shape = ShapeId::RunedStandingStone},
        {.id = B::SourceCopper, .key = "core:source_copper",
         .name = "Copper Source", .fullCube = false, .color = {1.00f, 0.55f, 0.25f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::CopperOre,
         .drop = {I::CopperSourceItem, 1}, .tiles = {56, 56, 56}, .hardness = 0.5f,
         .shape = ShapeId::VerdigrisStandingStone},
        {.id = B::SourceSand, .key = "core:source_sand",
         .name = "Sand Source", .fullCube = false, .color = {1.00f, 0.92f, 0.55f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::SandNode,
         .drop = {I::SandSourceItem, 1}, .tiles = {57, 57, 57}, .hardness = 0.5f,
         .shape = ShapeId::SandSource},
        {.id = B::SourceEssence, .key = "core:source_essence",
         .name = "Essence Source", .fullCube = false, .color = {0.85f, 0.35f, 1.00f},
         .emissive = 0.6f, .source = true, .spawnsNode = B::EssenceVent,
         .drop = {I::EssenceSourceItem, 1}, .tiles = {59, 59, 59}, .hardness = 0.5f,
         .shape = ShapeId::EssenceSource},
        // An open timber frame: the one block whose whole point is that you
        // can see and climb THROUGH it, so a solid cube was telling the
        // opposite of the truth.
        {.id = B::Scaffold, .key = "core:scaffold",
         .name = "Scaffold", .fullCube = false, .color = {0.68f, 0.62f, 0.48f},
         .drop = {I::ScaffoldItem, 1}, .tiles = {4, 4, 4}, .hardness = 0.6f,
         .shape = ShapeId::TimberScaffoldFrame},
        // Forestry. Leaves drop nothing here: the chance sapling drop is
        // rolled at the mining site, not in this table. A log splits into two
        // wood (fuel AND structure).
        {.id = B::Sapling, .key = "core:sapling",
         .name = "Sapling", .fullCube = false, .color = {0.45f, 0.72f, 0.28f},
         .drop = {I::SaplingItem, 1}, .tiles = {5, 5, 5}, .hardness = 0.2f,
         .needsSoil = SoilKind::Soil, .treeSize = 1,
         .shape = ShapeId::TreeSapling},
        {.id = B::Log, .key = "core:log",
         .name = "Log", .color = {0.45f, 0.33f, 0.18f},
         .drop = {I::Wood, 2}, .tiles = {6, 7, 6},
         .hardness = 3.0f, .tool = T::Axe, .toolTier = kTierWood},
        {.id = B::Leaves, .key = "core:leaves",
         .name = "Leaves", .color = {0.18f, 0.50f, 0.16f},
         .tiles = {8, 8, 8}, .hardness = 0.2f},
        {.id = B::RainBarrel, .key = "core:rain_barrel",
         .name = "Rain Barrel", .fullCube = false, .color = {0.40f, 0.28f, 0.15f},
         .machine = true, .drop = {I::RainBarrelItem, 1},
         .tiles = {52, 58, 58}, .hardness = 0.5f, .shape = ShapeId::RainBarrel}, // open water top, stave sides
        {.id = B::VoidStone, .key = "core:void_stone",
         .name = "Voidstone", .color = {0.24f, 0.16f, 0.36f},
         .tiles = {60, 60, 60}, // arena ground; deliberately no drop
         .hardness = 4.0f, .tool = T::Pickaxe, .toolTier = kTierWood},
        // Source fusion: the hybrid node + the source that grows it. The
        // Resonant Source rides every data-driven source path (grows via
        // updateSources, drops a relocatable item, glows) with no new sim
        // code; the node is harvested like any other.
        {.id = B::ResonantNode, .key = "core:resonant_node",
         .name = "Resonant Node", .fullCube = false, .color = {0.95f, 0.55f, 0.95f},
         .node = true, .drop = {I::Resonance, 1}, .tiles = {61, 61, 61},
         // Gated at IRON: the game's premium raw is what gives the tier above
         // copper something to be for.
         .hardness = 6.0f, .tool = T::Pickaxe, .toolTier = kTierIron,
         .shape = ShapeId::BicolourCrystalNode},
        {.id = B::ResonantSource, .key = "core:resonant_source",
         .name = "Resonant Source", .fullCube = false, .color = {1.00f, 0.60f, 1.00f},
         .emissive = 0.7f, .source = true, .spawnsNode = B::ResonantNode,
         .drop = {I::ResonantSourceItem, 1}, .tiles = {62, 62, 62}, .hardness = 0.5f,
         .shape = ShapeId::ResonantSource},
        // Composter machine: composts plant matter into renewable Dirt. Uses
        // spare machine atlas tiles (35 top, 36 side).
        {.id = B::Composter, .key = "core:composter",
         .name = "Composter", .fullCube = false, .color = {0.36f, 0.25f, 0.14f},
         .machine = true, .drop = {I::ComposterItem, 1}, .tiles = {35, 36, 36}, .hardness = 0.5f, .shape = ShapeId::Composter},
        // Forge machine: block-crafts weapons/armor. Uses spare machine atlas
        // tiles (37 top, 38 side).
        {.id = B::Forge, .key = "core:forge",
         .name = "Forge", .fullCube = false, .color = {0.30f, 0.22f, 0.24f},
         .machine = true, .drop = {I::ForgeItem, 1}, .tiles = {37, 38, 38}, .hardness = 0.5f, .shape = ShapeId::Forge},
        // Press machine: forms the shared parts tier. Uses spare machine atlas
        // tiles (39 top, 40 side).
        {.id = B::Press, .key = "core:press",
         .name = "Press", .fullCube = false, .color = {0.42f, 0.44f, 0.52f},
         .machine = true, .drop = {I::PressItem, 1}, .tiles = {39, 40, 40}, .hardness = 0.5f, .shape = ShapeId::Press},
        // The Alchemy Circle. Both halves are machines so they get a Machine
        // entity (buffers + progress) and ride the existing save records; the
        // Core glows faintly so a built circle reads as alive at night. Uses
        // spare machine atlas tiles (41/42 core, 43/44 pedestal).
        {.id = B::RuneCore, .key = "core:rune_core",
         .name = "Rune Core", .fullCube = false, .color = {0.34f, 0.28f, 0.46f},
         .emissive = 0.25f, .machine = true, .drop = {I::RuneCoreItem, 1},
         .tiles = {41, 42, 42}, .hardness = 0.5f,
         .shape = ShapeId::RuneCore},
        {.id = B::Pedestal, .key = "core:pedestal",
         .name = "Pedestal", .fullCube = false, .color = {0.58f, 0.56f, 0.62f},
         .machine = true, .drop = {I::PedestalItem, 1}, .tiles = {43, 44, 44},
         .hardness = 0.5f, .shape = ShapeId::MossyRunePedestal},
        // ---- The recipe overhaul: four powered machines (atlas row 8) ----
        // The Furnace glows: it is the only machine whose "on" state is a
        // fire, and a lit furnace should read across the factory floor at
        // night the way an energized network does.
        {.id = B::Furnace, .key = "core:furnace",
         .name = "Furnace", .fullCube = false, .color = {0.46f, 0.38f, 0.33f},
         .emissive = 0.20f, .machine = true, .drop = {I::FurnaceItem, 1},
         .tiles = {128, 129, 129}, .hardness = 0.5f, .shape = ShapeId::Furnace},
        {.id = B::Sifter, .key = "core:sifter",
         .name = "Sifter", .fullCube = false, .color = {0.61f, 0.52f, 0.36f},
         .machine = true, .drop = {I::SifterItem, 1}, .tiles = {130, 131, 131},
         .hardness = 0.5f, .shape = ShapeId::Sifter},
        {.id = B::Glassblower, .key = "core:glassblower",
         .name = "Glassblower", .fullCube = false, .color = {0.36f, 0.44f, 0.49f},
         .machine = true, .drop = {I::GlassblowerItem, 1}, .tiles = {132, 133, 133},
         .hardness = 0.5f, .shape = ShapeId::Glassblower},
        {.id = B::Compactor, .key = "core:compactor",
         .name = "Compactor", .fullCube = false, .color = {0.49f, 0.48f, 0.45f},
         .machine = true, .drop = {I::CompactorItem, 1}, .tiles = {134, 135, 135},
         .hardness = 0.5f, .shape = ShapeId::Compactor},
        // ---- The manual tier (atlas tiles 136-161) -----------------------
        // Thirteen rows of pure data. Each is soft (hardness 0.5) and drops
        // itself, like every other machine, so relocating your hand-cranked
        // starter kit stays free.
        {.id = B::Bloomery, .key = "core:bloomery",
         .name = "Bloomery", .fullCube = false, .color = {0.44f, 0.36f, 0.31f},
         .emissive = 0.18f, .machine = true, .drop = {I::BloomeryItem, 1},
         .tiles = {136, 137, 137}, .hardness = 0.5f, .shape = ShapeId::Bloomery},
        {.id = B::Sieve, .key = "core:sieve",
         .name = "Sieve", .fullCube = false, .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::SieveItem, 1}, .tiles = {138, 139, 139},
         .hardness = 0.5f, .shape = ShapeId::Sieve},
        {.id = B::Blowpipe, .key = "core:blowpipe",
         .name = "Blowpipe", .fullCube = false, .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::BlowpipeItem, 1}, .tiles = {140, 141, 141},
         .hardness = 0.5f, .shape = ShapeId::Blowpipe},
        {.id = B::Tamper, .key = "core:tamper",
         .name = "Tamper", .fullCube = false, .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::TamperItem, 1}, .tiles = {142, 143, 143},
         .hardness = 0.5f, .shape = ShapeId::Tamper},
        {.id = B::Mortar, .key = "core:mortar",
         .name = "Mortar", .fullCube = false, .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::MortarItem, 1}, .tiles = {144, 145, 145},
         .hardness = 0.5f, .shape = ShapeId::Mortar},
        {.id = B::HandPress, .key = "core:hand_press",
         .name = "Hand Press", .fullCube = false, .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::HandPressItem, 1}, .tiles = {146, 147, 147},
         .hardness = 0.5f, .shape = ShapeId::HandPress},
        {.id = B::Anvil, .key = "core:anvil",
         .name = "Anvil", .fullCube = false, .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::AnvilItem, 1}, .tiles = {148, 149, 149},
         .hardness = 0.5f, .shape = ShapeId::Anvil},
        {.id = B::CompostHeap, .key = "core:compost_heap",
         .name = "Compost Heap", .fullCube = false, .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::CompostHeapItem, 1}, .tiles = {150, 151, 151},
         .hardness = 0.5f, .shape = ShapeId::CompostHeap},
        {.id = B::MixingBowl, .key = "core:mixing_bowl",
         .name = "Mixing Bowl", .fullCube = false, .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::MixingBowlItem, 1}, .tiles = {152, 153, 153},
         .hardness = 0.5f, .shape = ShapeId::MixingBowl},
        {.id = B::InfusionStand, .key = "core:infusion_stand",
         .name = "Infusion Stand", .fullCube = false, .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::InfusionStandItem, 1}, .tiles = {154, 155, 155},
         .hardness = 0.5f, .shape = ShapeId::InfusionStand},
        {.id = B::Still, .key = "core:still",
         .name = "Still", .fullCube = false, .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::StillItem, 1}, .tiles = {156, 157, 157},
         .hardness = 0.5f, .shape = ShapeId::Still},
        {.id = B::HandDistiller, .key = "core:hand_distiller",
         .name = "Hand Distiller", .fullCube = false, .color = {0.50f, 0.38f, 0.23f},
         .machine = true, .drop = {I::HandDistillerItem, 1}, .tiles = {158, 159, 159},
         .hardness = 0.5f, .shape = ShapeId::HandDistiller},
        {.id = B::HandTransmuter, .key = "core:hand_transmuter",
         .name = "Hand Transmuter", .fullCube = false, .color = {0.44f, 0.43f, 0.41f},
         .machine = true, .drop = {I::HandTransmuterItem, 1}, .tiles = {160, 161, 161},
         .hardness = 0.5f, .shape = ShapeId::HandTransmuter},
        // ---- Bulk storage -------------------------------------------------
        {.id = B::StorageCrate, .key = "core:storage_crate",
         .name = "Storage Crate", .fullCube = false, .color = {0.55f, 0.40f, 0.22f},
         .machine = true, .drop = {I::StorageCrateItem, 1}, .tiles = {192, 193, 193},
         .hardness = 0.5f, .shape = ShapeId::StorageCrate}, // lid on top, slatted sides
        // ---- Farming ------------------------------------------------------
        // Worked ground. Drops Dirt, so tilling is not a way to duplicate soil,
        // and it must SURVIVE a harvest -- the Harvester resets a cell to
        // stage 0 and never untills, or an automated field would need
        // re-tilling by hand forever. Softer than Dirt: it has been broken up.
        {.id = B::TilledSoil, .key = "core:tilled_soil",
         .name = "Tilled Soil", .fullCube = false, .color = {0.36f, 0.24f, 0.13f},
         .drop = {I::DirtItem, 1}, .tiles = {9, 10, 2}, .hardness = 0.5f,
         .tool = T::Shovel, .provides = SoilKind::Tilled,
         .shape = ShapeId::TilledSoil},
        // Crops: crossed planes, so they do not fill their cell (no occlusion,
        // no keeping the rain off the field below). They stay SOLID, which is
        // what lets you aim at one and break it -- walking through wheat is a
        // later change that splits ray boxes from physics boxes, and today's
        // Sapling is a whole solid cube, so a crop is already strictly better.
        // Pull one early and you get the seed back; only the ripe stage is
        // worth anything.
        {.id = B::HerbCrop0, .key = "core:herb_crop_0",
         .name = "Herb Seedling", .fullCube = false, .color = {0.42f, 0.66f, 0.30f},
         .drop = {I::HerbSeed, 1}, .tiles = {11, 11, 11}, .hardness = 0.15f,
         .needsSoil = SoilKind::Tilled, .shape = ShapeId::HerbCrop0},
        {.id = B::HerbCrop1, .key = "core:herb_crop_1",
         .name = "Herb Sprout", .fullCube = false, .color = {0.42f, 0.68f, 0.30f},
         .drop = {I::HerbSeed, 1}, .tiles = {11, 11, 11}, .hardness = 0.15f,
         .needsSoil = SoilKind::Tilled, .shape = ShapeId::HerbCrop1},
        {.id = B::HerbCrop2, .key = "core:herb_crop_2",
         .name = "Herb Plant", .fullCube = false, .color = {0.44f, 0.70f, 0.32f},
         .drop = {I::HerbSeed, 1}, .tiles = {11, 11, 11}, .hardness = 0.15f,
         .needsSoil = SoilKind::Tilled, .shape = ShapeId::HerbCrop2},
        {.id = B::HerbCrop3, .key = "core:herb_crop_3",
         .name = "Ripe Herb", .fullCube = false, .color = {0.52f, 0.72f, 0.36f},
         .drop = {I::Herb, 2}, .tiles = {12, 12, 12}, .hardness = 0.15f,
         .needsSoil = SoilKind::Tilled, .shape = ShapeId::HerbCrop3},
        {.id = B::Harvester, .key = "core:harvester",
         .name = "Harvester", .fullCube = false, .color = {0.62f, 0.58f, 0.30f},
         .machine = true, .drop = {I::HarvesterItem, 1}, .tiles = {13, 14, 14},
         .hardness = 0.5f, .shape = ShapeId::Harvester},
        {.id = B::Irrigator, .key = "core:irrigator",
         .name = "Irrigator", .fullCube = false, .color = {0.42f, 0.58f, 0.72f},
         .machine = true, .drop = {I::IrrigatorItem, 1}, .tiles = {15, 45, 45},
         .hardness = 0.5f, .shape = ShapeId::Irrigator},
        // Tilled soil fed compost. Drops Dirt like the tilled ground it came
        // from, for the same reason: enriching must not be a way to duplicate
        // soil either. It has no item of its own -- you make it in place with
        // compost, the way you make Tilled Soil in place with the hoe.
        {.id = B::RichSoil, .key = "core:rich_soil",
         .name = "Rich Soil", .fullCube = false, .color = {0.30f, 0.21f, 0.13f},
         .drop = {I::DirtItem, 1}, .tiles = {194, 195, 2}, .hardness = 0.5f,
         .tool = T::Shovel, .provides = SoilKind::Rich,
         .shape = ShapeId::RichSoil},
        // Two saplings bound together with compost. Grows the size-2 tree
        // (5 logs, 74 leaves) in the same 45 seconds, which is what turns the
        // sapling SURPLUS -- a tree returns ~8 for the one that made it -- into
        // more wood per plot rather than more saplings you cannot place.
        {.id = B::SaplingGrafted, .key = "core:sapling_grafted",
         .name = "Grafted Sapling", .fullCube = false, .color = {0.38f, 0.66f, 0.24f},
         .drop = {I::GraftedSaplingItem, 1}, .tiles = {196, 196, 196},
         .hardness = 0.2f, .needsSoil = SoilKind::Soil, .treeSize = 2,
         .shape = ShapeId::GraftedSapling},
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

    // Keys are the identity that outlives the ordinal, so a duplicate or an
    // empty one is a silent aliasing bug in every consumer at once: a save's
    // id table would map two blocks onto one, and two mods claiming the same
    // key would overwrite each other. Cheap to prove here, impossible to
    // diagnose later. (Block and item keys live in separate namespaces --
    // "core:stone" is legitimately both a block and an item.)
    static_assert([] {
        for (std::size_t i = 0; i < std::size(kBlocks); ++i) {
            const std::string_view a{kBlocks[i].key};
            if (a.empty()) return false;
            for (std::size_t j = i + 1; j < std::size(kBlocks); ++j) {
                if (a == std::string_view{kBlocks[j].key}) return false;
            }
        }
        return true;
    }(), "kBlocks keys must be unique and non-empty");

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
            for (const MachineTraits& t : kMachineTraitSeed) {
                if (t.block == b.id) hasTraits = true;
            }
            if (b.machine != hasTraits) return false;
        }
        return true;
    }(), "kMachineTraitSeed must have one row per machine block (and only machine blocks)");

    // The registry proper: seeded from kBlocks above, grown by content packs.
    //
    // Every static_assert in this file still guards kBlocks, because kBlocks is
    // still a compile-time table -- moving the REGISTRY to runtime storage cost
    // none of that. What a loaded row gets instead is content::validate(),
    // which re-asks the same questions of the whole table.
    //
    // A function-local static rather than a namespace-scope one: this is read
    // from other translation units' code, and a Meyers singleton has no
    // initialization-order hazard. blockInfo() was already an out-of-line call,
    // so the lookup costs what it always did.
    std::vector<BlockInfo>& blockTable() {
        static std::vector<BlockInfo> table(std::begin(kBlocks), std::end(kBlocks));
        return table;
    }

} // namespace

const BlockInfo& blockInfo(BlockId id) {
    return blockTable()[static_cast<std::size_t>(id)];
}

std::size_t blockCount() {
    return blockTable().size();
}

const std::vector<BlockInfo>& blockRows() {
    return blockTable();
}

void restoreBlocks(std::vector<BlockInfo> rows) {
    blockTable() = std::move(rows);
}

BlockId addBlock(const BlockInfo& row) {
    const auto id = static_cast<BlockId>(blockTable().size());
    blockTable().push_back(row);
    blockTable().back().id = id; // the row's id IS its position, always
    return id;
}
