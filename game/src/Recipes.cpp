#include "game/Recipes.h"

#include "game/Machine.h"

#include <cstddef>

namespace {
    using I = ItemId;

    // The equipment hand-craft chain (v2: machine-made plates). Nothing here
    // costs a Copper Plate -- plates come from a fueled Press, so the tech tree
    // bootstraps through automation.
    // Hand-crafting is a SURVIVAL TIER only. It is instant, free, and needs
    // no world state, which is exactly why it must not be able to build the
    // factory: everything past this list moved onto the Alchemy Circle
    // (kCircleRecipes below), where crafting occupies floor space, takes time,
    // and can be belt-fed. What stays here is what you need to get off the
    // ground with nothing -- plus the Circle's own two parts, or the tech tree
    // would deadlock behind a circle you cannot build.
    std::vector<Recipe> kRecipes = {
        // ---- Early-game ladder (the hard start) ----
        // The two lowest tool tiers. Wood tools come from hand-gathered sticks
        // (leaves) + pebbles (sifting dirt/grass) and gate Stone + Logs; Stone
        // tools come from mined stone + sticks and gate the ore tier (copper,
        // crystal, essence).
        {"hand/wood-pickaxe",  {{I::Stick, 2}, {I::Pebble, 3}},     {I::WoodPickaxe, 1}},
        {"hand/wood-axe",      {{I::Stick, 2}, {I::Pebble, 3}},     {I::WoodAxe, 1}},
        {"hand/stone-pickaxe", {{I::Stone, 3}, {I::Stick, 2}},      {I::StonePickaxe, 1}},
        {"hand/stone-axe",     {{I::Stone, 3}, {I::Stick, 2}},      {I::StoneAxe, 1}},
        {"hand/stone-shovel",  {{I::Stone, 2}, {I::Stick, 2}},      {I::StoneShovel, 1}},
        // Structural staples you can always fall back on.
        {"hand/scaffold",      {{I::Stone, 1}},                     {I::ScaffoldItem, 4}},
        {"hand/bucket",        {{I::Wood, 3}},                      {I::Bucket, 1}},
        // Storage stays HAND-craftable on purpose. A crate is the answer to a
        // machine whose output has filled, and outputs start filling long
        // before you own a Circle -- gating it behind one would mean meeting
        // the problem with no way to solve it.
        {"hand/storage-crate", {{I::Wood, 8}},                      {I::StorageCrateItem, 1}},
        // ---- The two bootstrap machines ----
        // Smelting, glass, vials and dirt+sand -> stone all moved onto
        // machines, so these two have to stay hand-buildable or the tree
        // deadlocks: the Rune Core costs Copper Ingots, and the only way to
        // get an ingot is now a fire.
        //
        // The Bloomery is FIRST. It is the true start of the tech tree, it
        // burns sticks (which leaves already give you) and it makes the
        // charcoal that makes everything after it faster.
        {"hand/bloomery", {{I::Stone, 8}},                          {I::BloomeryItem, 1}},
        // The Sieve is the only way into iron, and iron is every machine
        // frame -- so it must be reachable with nothing but wood and sticks.
        {"hand/sieve",    {{I::Wood, 4}, {I::Stick, 4}},            {I::SieveItem, 1}},
        // The Alchemy Circle itself -- the one piece of the factory you may
        // still build by hand, because it is the gateway to all the rest.
        {"hand/pedestal",  {{I::Stone, 4}, {I::CopperIngot, 1}},    {I::PedestalItem, 1}},
        {"hand/rune-core", {{I::Stone, 6}, {I::CopperIngot, 2}, {I::Crystal, 1}},
                                                                    {I::RuneCoreItem, 1}},
    };
}

const std::vector<Recipe>& handcraftRecipes() {
    return kRecipes;
}

namespace {
    using B = BlockId;

    // The reagent chain. Each machine type runs whichever of its recipes it has
    // the inputs for. Rows may be reordered or removed freely (saves store the
    // key), but ORDER IS STILL GAMEPLAY: AUTO runs the first row whose inputs
    // are present, so a row that is a prefix of another must come after it.
    std::vector<MachineRecipe> kMachineRecipes = {
        // Grinder: grinds raws to powder. Also crushes Stone back into Sand,
        // so Sand is renewable from Stone (which is renewable from dirt+sand).
        {"grinder/ground-herb",  B::Grinder, {{I::Herb, 1}},    {{{I::GroundHerb, 1}}},  2.0f},
        {"grinder/crystal-dust", B::Grinder, {{I::Crystal, 1}}, {{{I::CrystalDust, 1}}}, 2.0f},
        {"grinder/sand",         B::Grinder, {{I::Stone, 1}},   {{{I::Sand, 2}}},        2.0f},
        // Composter: renewable Dirt from plant matter (sticks / saplings).
        {"composter/dirt-from-sticks",  B::Composter, {{I::Stick, 3}},
                                                              {{{I::DirtItem, 2}}}, 2.5f},
        {"composter/dirt-from-sapling", B::Composter, {{I::SaplingItem, 1}},
                                                              {{{I::DirtItem, 3}}}, 3.0f},
        // Cauldron: powder + water -> solution/tincture
        {"cauldron/herbal-tincture",  B::Cauldron, {{I::GroundHerb, 1}, {I::SpringWater, 1}},
                                                              {{{I::HerbalTincture, 1}}},  3.0f},
        {"cauldron/mineral-solution", B::Cauldron, {{I::CrystalDust, 1}, {I::SpringWater, 1}},
                                                              {{{I::MineralSolution, 1}}}, 3.0f},
        // Infuser: tincture/solution -> potion
        {"infuser/healing-draught", B::Infuser, {{I::HerbalTincture, 1}, {I::Vial, 1}},
                                                              {{{I::HealingDraught, 1}}}, 4.0f},
        {"infuser/mana-vial",       B::Infuser, {{I::MineralSolution, 1}, {I::Essence, 1}},
                                                              {{{I::ManaVial, 1}}},       4.0f},
        // Alembic: potions -> elixir
        {"alembic/elixir-of-vigor", B::Alembic, {{I::HealingDraught, 1}, {I::ManaVial, 1}},
                                                              {{{I::ElixirOfVigor, 1}}},  5.0f},
        // Distiller/Transmuter: the philosopher's tier
        {"distiller/refined-elixir", B::Distiller, {{I::ElixirOfVigor, 1}, {I::Essence, 1}},
                                                              {{{I::RefinedElixir, 1}}}, 6.0f},
        {"transmuter/philosophers-catalyst", B::Transmuter,
                                     {{I::RefinedElixir, 1}, {I::CrystalDust, 1}},
                                                              {{{I::PhilosophersCatalyst, 1}}}, 8.0f},
        {"transmuter/philosophers-stone", B::Transmuter,
                                     {{I::PhilosophersCatalyst, 1}, {I::ElixirOfVigor, 1}},
                                                              {{{I::PhilosophersStone, 1}}}, 10.0f},
        // Forge: the armory. The Copper set forges from plates; the Aegis set is
        // gated on the boss drops (Void Catalyst / Storm Core), giving those
        // trophies a sink and closing the fight -> forge -> harder-fight loop.
        {"forge/copper-helm",  B::Forge, {{I::CopperPlate, 3}}, {{{I::CopperHelm, 1}}},  5.0f},
        {"forge/copper-chest", B::Forge, {{I::CopperPlate, 5}}, {{{I::CopperChest, 1}}}, 7.0f},
        {"forge/copper-boots", B::Forge, {{I::CopperPlate, 3}}, {{{I::CopperBoots, 1}}}, 5.0f},
        {"forge/aegis-helm",   B::Forge, {{I::MachineFrame, 1}, {I::VoidCatalyst, 1}},
                                                                {{{I::AegisHelm, 1}}},   8.0f},
        {"forge/aegis-chest",  B::Forge,
                    {{I::MachineFrame, 2}, {I::VoidCatalyst, 1}, {I::StormCore, 1}},
                                                                {{{I::AegisChest, 1}}}, 12.0f},
        {"forge/aegis-boots",  B::Forge, {{I::MachineFrame, 1}, {I::StormCore, 1}},
                                                                {{{I::AegisBoots, 1}}},  8.0f},
        // The iron set slots between Copper and the boss-gated Aegis, so
        // there is something to forge on the way up.
        {"forge/iron-helm",  B::Forge, {{I::IronPlate, 3}}, {{{I::IronHelm, 1}}},  6.0f},
        {"forge/iron-chest", B::Forge, {{I::IronPlate, 5}}, {{{I::IronChest, 1}}}, 8.0f},
        {"forge/iron-boots", B::Forge, {{I::IronPlate, 3}}, {{{I::IronBoots, 1}}}, 6.0f},
        // Press: the shared parts tier. Every machine reaches the Machine Frame
        // through four machine steps instead of one hand-craft, so the factory
        // has to exist before the tech tree opens up.
        //
        // Plate is FIRST on purpose. The Press is the only plate source and
        // therefore the first machine a new game builds, and AUTO runs the
        // first recipe whose inputs are present -- so with plate second, a
        // fresh Press fed the one raw the player has (ingots) would quietly
        // make Copper Rods forever. Rod costs the same single ingot and loses
        // that tie by design: plates feed casings, etched plates, tools and
        // armor, rods feed only gears. Lock the MAKE ROD row to get rods on a
        // shared Press -- the same bargain the other rows already make.
        {"press/copper-plate",  B::Press, {{I::CopperIngot, 1}}, {{{I::CopperPlate, 1}}},   3.0f},
        {"press/iron-plate",    B::Press, {{I::IronIngot, 1}},   {{{I::IronPlate, 1}}},     3.0f},
        {"press/iron-rod",      B::Press, {{I::IronIngot, 1}},   {{{I::IronRod, 2}}},       2.0f},
        {"press/gear",          B::Press, {{I::IronRod, 2}},     {{{I::Gear, 1}}},          3.0f},
        {"press/machine-casing", B::Press, {{I::IronPlate, 4}},  {{{I::MachineCasing, 1}}}, 5.0f},
        // Etched Plate stays COPPER: iron is structure, copper is the metal
        // that carries a signal. It is the one part of the frame that keeps
        // the copper line on the critical path.
        {"press/etched-plate",  B::Press, {{I::CopperPlate, 1}, {I::CrystalDust, 2}},
                                                                 {{{I::EtchedPlate, 1}}},   4.0f},
        {"press/machine-frame", B::Press,
                    {{I::MachineCasing, 1}, {I::Gear, 2}, {I::EtchedPlate, 1}},
                                                                 {{{I::MachineFrame, 1}}},  8.0f},
        // The Copper Rod survives only as a legacy part with no consumer --
        // rods are iron now. Kept so an old save's stock is not orphaned, and
        // listed LAST so AUTO never reaches for it.
        {"press/copper-rod",    B::Press, {{I::CopperIngot, 1}}, {{{I::CopperRod, 2}}},     2.0f},

        // ---- Furnace: the smelter (fuel-fired, no power) -----------------
        // Ore straight to ingot at 2:1; nuggets, which the Sifter produces
        // four at a time, at 4:1. Both routes cost the same fire.
        {"furnace/copper-ingot", B::Furnace, {{I::CopperOre, 2}},
                                                              {{{I::CopperIngot, 1}}}, 4.0f},
        {"furnace/iron-ingot",   B::Furnace, {{I::IronNugget, 4}},
                                                              {{{I::IronIngot, 1}}},   4.0f},
        {"furnace/copper-from-nuggets", B::Furnace, {{I::CopperNugget, 4}},
                                                              {{{I::CopperIngot, 1}}}, 4.0f},
        {"furnace/glass",        B::Furnace, {{I::Sand, 1}},   {{{I::Glass, 1}}},       3.0f},
        // Charring wood is what makes a Furnace pay for itself: Charcoal
        // burns 3x as long as the wood it came from, so a furnace line feeds
        // its own fire (and the generators). A Furnace keeps fuel in its own
        // buffer, so it can char wood and burn wood at the same time -- which
        // the old shared buffer could not express (see usesFuelSlot).
        {"furnace/charcoal",     B::Furnace, {{I::Wood, 2}},   {{{I::Charcoal, 1}}},    6.0f},

        // ---- Sifter: the only source of iron -----------------------------
        // A weighted roll, which is the point: sifting is a rate, not a
        // recipe. Iron has no other route into the game, and sand is
        // renewable (Sand Source, and the Grinder crushes Stone back to
        // Sand), so sand throughput is the ceiling on machine-building.
        // Weights are relative, not percentages.
        {"sifter/sand", B::Sifter, {{I::Sand, 4}},
            {{{I::IronNugget, 1}, 30.0f},
             {{I::CopperNugget, 1}, 20.0f},
             {{I::Pebble, 1}, 20.0f},
             {{I::Crystal, 1}, 5.0f},
             {{}, 25.0f}},                                                              4.0f},
        // Sifting soil for pebbles automates the hand action the early game
        // already teaches, and gives dirt a second use besides stone.
        {"sifter/soil", B::Sifter, {{I::DirtItem, 4}},
            {{{I::Pebble, 2}, 45.0f},
             {{I::Sand, 1}, 30.0f},
             {{I::Stick, 1}, 15.0f},
             {{}, 10.0f}},                                                              3.0f},
        // Farming's way in. Seeds come off the SIEVE tier deliberately: the
        // Sieve is hand-craftable (Wood + Sticks), so a field is reachable
        // before the Circle, and the wild Herb Bushes the island already grows
        // are the bootstrap. Deterministic, not a roll -- the point of farming
        // is that it scales with area, and a seed you might not get would put
        // that behind luck. One in, one out; a ripe plant yields two, and THAT
        // is the doubling.
        {"sifter/herb-seed", B::Sifter, {{I::Herb, 1}},
                                                       {{{I::HerbSeed, 1}}},           2.0f},

        // ---- Glassblower -------------------------------------------------
        // One recipe today. It exists as its own machine rather than as a
        // Furnace row because glassware is where the Conduit becomes a glass
        // tube and windows arrive -- this is the machine those land on.
        {"glassblower/vial", B::Glassblower, {{I::Glass, 1}}, {{{I::Vial, 1}}},          3.0f},

        // ---- Compactor: renewable stone ----------------------------------
        // The island is finite; this is what makes stone not be. Dirt renews
        // through the Composter, sand through the Sand Source and the
        // Grinder, so the loop closes.
        {"compactor/stone", B::Compactor, {{I::DirtItem, 4}, {I::Sand, 4}},
                                                              {{{I::Stone, 2}}},        5.0f},
    };
}

namespace {
    // ---- The Alchemy Circle ----------------------------------------------
    // A 4-slot `ring` lists the CARDINAL pedestals clockwise from north
    // (N, E, S, W); an 8-slot ring lists all eight (N, NE, E, SE, S, SW, W, NW)
    // and needs a Greater circle. `{}` means the slot must be EMPTY, which is
    // the strongest way to tell two otherwise-similar patterns apart.
    //
    // Slots match on "holds AT LEAST this many", so a belt topping a pedestal
    // up never breaks the pattern. The cost is that one pattern can be a
    // superset of another, so ORDER MATTERS: the more demanding variant is
    // listed first and AUTO takes the first match. Locking a MAKE row in the
    // panel is the escape hatch -- the same known-by-design bargain the Press
    // makes with its rows.
    std::vector<CircleRecipe> kCircleRecipes = {
        // -- Bootstrap tier: no plates, so a Lesser circle can build the
        // machines that make plates. This is the whole reason the Lesser
        // circle runs unpowered.
        {"circle/grinder", {}, {{I::CopperIngot, 3}, {}, {I::Stone, 4}, {}},
                                                           {I::GrinderItem, 1},   6.0f},
        {"circle/generator", {}, {{I::CopperIngot, 2}, {I::Stone, 2}, {I::Wood, 2}, {}},
                                                           {I::GeneratorItem, 1}, 6.0f},
        {"circle/composter", {}, {{I::Wood, 6}, {}, {I::Stick, 4}, {}},
                                                           {I::ComposterItem, 1}, 5.0f},
        {"circle/rain-barrel", {}, {{I::Wood, 6}, {}, {I::Bucket, 1}, {}},
                                                           {I::RainBarrelItem, 1}, 5.0f},
        {"circle/wire", {}, {{I::CopperIngot, 1}, {}, {}, {}},
                                                           {I::WireItem, 2},      2.0f},
        // The Press makes the plates, so it belongs to THIS tier and must cost
        // none: ingots opposite ingots, stone opposite stone. The same raw
        // price as the old ingot+plate pattern, laid out so nothing else can
        // match it -- the Grinder and Generator both require an empty west
        // pedestal, and this one fills it.
        {"circle/press", {}, {{I::CopperIngot, 2}, {I::Stone, 2}, {I::CopperIngot, 2}, {I::Stone, 2}},
                                                           {I::PressItem, 1},     8.0f},
        // The powered Furnace is the OTHER fully-occupied necklace, for the
        // same reason as the Press: nothing else can match a ring with all
        // four cardinals filled, and the Furnace is bootstrap-tier (it costs
        // no plate, because plates need ingots, which need a fire).
        // Charcoal in the pattern is the Bloomery paying forward.
        {"circle/furnace", {}, {{I::Stone, 6}, {I::Charcoal, 2}, {I::Stone, 6}, {I::Charcoal, 2}},
                                                           {I::FurnaceItem, 1},   8.0f},
        // -- The MANUAL tier. Cheap, unpowered, and three times slow; this is
        // what you build a factory WITH before you have a factory. Every
        // pattern here is stone/wood/ingot only, so a Lesser circle on a dead
        // network can lay all of them. (The Bloomery and Sieve are not here:
        // they hand-craft, because you need them BEFORE a circle.)
        {"circle/mortar", {}, {{I::Stone, 6}, {}, {}, {}},  {I::MortarItem, 1},    3.0f},
        {"circle/compost-heap", {}, {{I::Wood, 4}, {}, {}, {}},
                                                           {I::CompostHeapItem, 1}, 3.0f},
        {"circle/hand-press", {}, {{I::Stone, 4}, {I::Wood, 4}, {}, {}},
                                                           {I::HandPressItem, 1}, 4.0f},
        {"circle/anvil", {}, {{I::CopperIngot, 4}, {I::Stone, 4}, {}, {}},
                                                           {I::AnvilItem, 1},     4.0f},
        {"circle/tamper", {}, {{I::Stone, 6}, {I::Wood, 2}, {}, {}},
                                                           {I::TamperItem, 1},    4.0f},
        {"circle/blowpipe", {}, {{I::Stone, 4}, {}, {I::Wood, 2}, {}},
                                                           {I::BlowpipeItem, 1},  4.0f},
        {"circle/mixing-bowl", {}, {{I::Stone, 4}, {}, {I::Glass, 2}, {}},
                                                           {I::MixingBowlItem, 1}, 4.0f},
        {"circle/infusion-stand", {}, {{I::Wood, 4}, {}, {I::Glass, 1}, {}},
                                                           {I::InfusionStandItem, 1}, 4.0f},
        {"circle/still", {}, {{I::Wood, 4}, {}, {I::Vial, 2}, {}},
                                                           {I::StillItem, 1},     4.0f},
        {"circle/hand-distiller", {}, {{I::Stone, 4}, {}, {I::Vial, 2}, {}},
                                                           {I::HandDistillerItem, 1}, 4.0f},
        {"circle/hand-transmuter", {}, {{I::Stone, 4}, {}, {I::Crystal, 2}, {}},
                                                           {I::HandTransmuterItem, 1}, 4.0f},
        // -- Plate tier. Conduit and the Wrench cost the same two plates and
        // are told apart by ARRANGEMENT alone: both plates on one pedestal
        // versus one plate on each of two opposite pedestals. That is the
        // necklace earning its keep.
        // Pickaxe and Axe cost exactly the same; only the arrangement differs
        // (wood OPPOSITE the plates vs. wood BESIDE them).
        {"circle/copper-axe", {}, {{I::CopperPlate, 3}, {I::Wood, 2}, {}, {}},
                                                           {I::CopperAxe, 1},     4.0f},
        {"circle/copper-pickaxe", {}, {{I::CopperPlate, 3}, {}, {I::Wood, 2}, {}},
                                                           {I::CopperPickaxe, 1}, 4.0f},
        {"circle/copper-shovel", {}, {{I::CopperPlate, 2}, {}, {I::Wood, 2}, {}},
                                                           {I::CopperShovel, 1},  4.0f},
        // The Hoe is the Shovel's two items laid BESIDE each other instead of
        // opposite. It must stay listed after the Axe, which is the same
        // arrangement with a third plate and therefore a superset of it under
        // "holds at least this many" -- the documented ordering trap, and the
        // circle-shadowing check in --selftest is what enforces it.
        {"circle/copper-hoe", {}, {{I::CopperPlate, 2}, {I::Wood, 2}, {}, {}},
                                                           {I::CopperHoe, 1},     4.0f},
        {"circle/copper-sword", {}, {{I::CopperPlate, 2}, {}, {I::Wood, 1}, {}},
                                                           {I::CopperSword, 1},   4.0f},
        {"circle/conduit", {}, {{I::CopperPlate, 2}, {}, {}, {}},
                                                           {I::Conduit, 2},       3.0f},
        {"circle/wrench", {}, {{I::CopperPlate, 1}, {}, {I::CopperPlate, 1}, {}},
                                                           {I::Wrench, 1},        3.0f},
        // -- Iron tier. Identical arrangements to the copper set one stock up,
        // so the patterns you already learned keep working. Same ordering
        // trap, same fix: the more demanding variant first (pickaxe before
        // shovel before sword, each a superset of the next).
        {"circle/iron-axe", {}, {{I::IronPlate, 3}, {I::Wood, 2}, {}, {}},
                                                           {I::IronAxe, 1},       5.0f},
        {"circle/iron-pickaxe", {}, {{I::IronPlate, 3}, {}, {I::Wood, 2}, {}},
                                                           {I::IronPickaxe, 1},   5.0f},
        {"circle/iron-shovel", {}, {{I::IronPlate, 2}, {}, {I::Wood, 2}, {}},
                                                           {I::IronShovel, 1},    5.0f},
        {"circle/iron-sword", {}, {{I::IronPlate, 2}, {}, {I::Wood, 1}, {}},
                                                           {I::IronSword, 1},     5.0f},
        // -- Machine Frame tier: the alchemy chain proper. Three-ingredient
        // patterns first, so they win over their two-ingredient prefixes.
        {"circle/miner", {}, {{I::MachineFrame, 1}, {I::CopperPlate, 3}, {I::Stone, 4}, {}},
                                                           {I::MinerItem, 1},     8.0f},
        // The Harvester is the Miner's sibling and costs about the same, but in
        // IRON rather than stone -- it is a blade, and iron is the structural
        // metal. Three ingredients, so it sits with the rest of this tier
        // ahead of any two-ingredient pattern it would otherwise shadow.
        {"circle/harvester", {}, {{I::MachineFrame, 1}, {I::IronPlate, 3}, {I::CopperHoe, 1}, {}},
                                                           {I::HarvesterItem, 1}, 8.0f},
        {"circle/distiller", {}, {{I::MachineFrame, 1}, {I::Glass, 2}, {I::Crystal, 1}, {}},
                                                           {I::DistillerItem, 1}, 8.0f},
        {"circle/transmuter", {}, {{I::MachineFrame, 1}, {I::Crystal, 2}, {I::Essence, 1}, {}},
                                                           {I::TransmuterItem, 1}, 8.0f},
        {"circle/infuser", {}, {{I::MachineFrame, 1}, {I::Glass, 1}, {I::Vial, 1}, {}},
                                                           {I::InfuserItem, 1},   6.0f},
        {"circle/cauldron", {}, {{I::MachineFrame, 1}, {}, {I::Glass, 2}, {}},
                                                           {I::CauldronItem, 1},  6.0f},
        {"circle/forge", {}, {{I::MachineFrame, 1}, {}, {I::CopperPlate, 2}, {}},
                                                           {I::ForgeItem, 1},     6.0f},
        {"circle/alembic", {}, {{I::MachineFrame, 1}, {}, {I::Crystal, 1}, {}},
                                                           {I::AlembicItem, 1},   6.0f},
        // The powered twins of the sifting/glass/compacting line. Frame-tier,
        // because the point of the manual versions is that you reach these
        // THROUGH them.
        {"circle/sifter", {}, {{I::MachineFrame, 1}, {I::Wood, 4}, {I::Stone, 4}, {}},
                                                           {I::SifterItem, 1},    8.0f},
        {"circle/glassblower", {}, {{I::MachineFrame, 1}, {I::Glass, 4}, {}, {}},
                                                           {I::GlassblowerItem, 1}, 6.0f},
        {"circle/compactor", {}, {{I::MachineFrame, 1}, {I::Stone, 8}, {}, {}},
                                                           {I::CompactorItem, 1}, 6.0f},
        // -- Source transmutation: the catalyst goes in the CORE, the raw on
        // the ring. Resource production stays craftable, so the loop is closed.
        {"circle/herb-source", {I::PhilosophersCatalyst, 1}, {{I::Herb, 8}, {}, {}, {}},
                                                           {I::HerbSourceItem, 1},    10.0f},
        {"circle/crystal-source", {I::PhilosophersCatalyst, 1}, {{I::Crystal, 8}, {}, {}, {}},
                                                           {I::CrystalSourceItem, 1}, 10.0f},
        {"circle/copper-source", {I::PhilosophersCatalyst, 1}, {{I::CopperOre, 8}, {}, {}, {}},
                                                           {I::CopperSourceItem, 1},  10.0f},
        {"circle/sand-source", {I::PhilosophersCatalyst, 1}, {{I::Sand, 8}, {}, {}, {}},
                                                           {I::SandSourceItem, 1},    10.0f},
        {"circle/essence-source", {I::PhilosophersCatalyst, 1}, {{I::Essence, 8}, {}, {}, {}},
                                                           {I::EssenceSourceItem, 1}, 10.0f},
        // Resonance conversions -- the 2-for-1 is listed first because a
        // pedestal holding two Resonance also satisfies the 1-cost pattern.
        {"circle/fusion-catalyst-from-resonance", {}, {{I::Resonance, 2}, {}, {}, {}},
                                                           {I::FusionCatalyst, 1},       5.0f},
        {"circle/philosophers-catalyst-from-resonance", {}, {{I::Resonance, 1}, {}, {}, {}},
                                                           {I::PhilosophersCatalyst, 1}, 5.0f},
        // -- GREATER tier (all eight pedestals, powered): the boss keys. The
        // full ring is the ceremony -- these are the game's biggest crafts.
        // The Teleport Key's centre cost is deliberately enormous: 100
        // catalysts is a full automated Transmuter line's output, so reaching
        // the boss is an automation problem, not a menu click.
        {"circle/teleport-key",
         {I::PhilosophersCatalyst, kTeleportKeyCatalystCost},
         {{I::Crystal, 1}, {I::Essence, 1}, {I::Crystal, 1}, {},
          {I::Crystal, 1}, {I::Essence, 1}, {I::Crystal, 1}, {}},
                                                           {I::TeleportKey, 1},   12.0f},
        // 100 Void Catalysts means 100 Void Warden kills -- the warden is the
        // only source. Unlike the Teleport Key's cost this can't be automated
        // away, so the Storm Key is gated on repetition rather than on scale.
        {"circle/storm-key",
         {I::VoidCatalyst, kStormKeyCatalystCost},
         {{I::Crystal, 1}, {I::SpringWater, 1}, {I::Crystal, 1}, {I::SpringWater, 1},
          {I::Crystal, 1}, {I::SpringWater, 1}, {I::Crystal, 1}, {I::SpringWater, 1}},
                                                           {I::StormKey, 1},      12.0f},
        {"circle/fusion-catalyst-from-stone",
         {I::PhilosophersStone, 1},
         {{I::Crystal, 1}, {}, {I::Essence, 1}, {},
          {I::Crystal, 1}, {}, {I::Essence, 1}, {}},
                                                           {I::FusionCatalyst, 1}, 15.0f},
    };
}

const std::vector<MachineRecipe>& machineRecipes() {
    return kMachineRecipes;
}

const std::vector<CircleRecipe>& circleRecipes() {
    return kCircleRecipes;
}

ItemStack primaryOutput(const MachineRecipe& r) {
    return r.outputs.empty() ? ItemStack{} : r.outputs.front().stack;
}

std::vector<const MachineRecipe*> recipesForMachine(BlockId type) {
    // A manual twin delegates to its powered counterpart, so both tiers list
    // (and index) exactly the same rows.
    const BlockId group = recipeGroupFor(type);
    std::vector<const MachineRecipe*> out;
    for (const MachineRecipe& r : kMachineRecipes) {
        if (r.machine == group) out.push_back(&r);
    }
    return out;
}

int recipeIndexForKey(BlockId type, std::string_view key) {
    if (key.empty()) return -1;
    const auto rows = recipesForMachine(type);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (key == rows[i]->key) return static_cast<int>(i);
    }
    return -1;
}

const char* recipeKeyFor(BlockId type, int index) {
    if (index < 0) return "";
    const auto rows = recipesForMachine(type);
    if (static_cast<std::size_t>(index) >= rows.size()) return "";
    return rows[static_cast<std::size_t>(index)]->key.c_str();
}

int circleIndexForKey(std::string_view key) {
    if (key.empty()) return -1;
    for (std::size_t i = 0; i < kCircleRecipes.size(); ++i) {
        if (key == kCircleRecipes[i].key) return static_cast<int>(i);
    }
    return -1;
}

const char* circleKeyFor(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= kCircleRecipes.size()) return "";
    return kCircleRecipes[static_cast<std::size_t>(index)].key.c_str();
}

// The pack loader's handles on the tables. Seeded above with the compiled rows;
// a pack may replace, append to, or delete from them at startup. See the note
// in Recipes.h about why that must happen before a world exists.
namespace recipes {
    std::vector<Recipe>&        handTable()    { return kRecipes; }
    std::vector<MachineRecipe>& machineTable() { return kMachineRecipes; }
    std::vector<CircleRecipe>&  circleTable()  { return kCircleRecipes; }
}
