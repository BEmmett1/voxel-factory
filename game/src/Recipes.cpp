#include "game/Recipes.h"

namespace {
    using I = ItemId;

    // The equipment hand-craft chain (v2: machine-made plates). Index order is
    // stable (the menu relies on it). Nothing here costs a Copper Plate --
    // plates come from a powered, fueled Press, so the tech tree bootstraps
    // through automation.
    // Hand-crafting is now a SURVIVAL TIER only. It is instant, free, and needs
    // no world state, which is exactly why it must not be able to build the
    // factory: everything past this list moved onto the Alchemy Circle
    // (kCircleRecipes below), where crafting occupies floor space, takes time,
    // and can be belt-fed. What stays here is what you need to get off the
    // ground with nothing -- plus the Circle's own two parts, or the tech tree
    // would deadlock behind a circle you cannot build.
    const std::vector<Recipe> kRecipes = {
        // ---- Early-game ladder (the hard start) ----
        // The two lowest tool tiers. Wood tools come from hand-gathered sticks
        // (leaves) + pebbles (sifting dirt/grass) and gate Stone + Logs; Stone
        // tools come from mined stone + sticks and gate the ore tier (copper,
        // crystal, essence).
        {{{I::Stick, 2}, {I::Pebble, 3}},            {I::WoodPickaxe, 1}},
        {{{I::Stick, 2}, {I::Pebble, 3}},            {I::WoodAxe, 1}},
        {{{I::Stone, 3}, {I::Stick, 2}},             {I::StonePickaxe, 1}},
        {{{I::Stone, 3}, {I::Stick, 2}},             {I::StoneAxe, 1}},
        {{{I::Stone, 2}, {I::Stick, 2}},             {I::StoneShovel, 1}},
        // Renewable Stone (the "long process"): compact a lot of dirt + sand.
        // Dirt renews via the Composter, Sand via the Sand Source / Grinder, so
        // the finite island is never the bottleneck.
        {{{I::DirtItem, 4}, {I::Sand, 4}},           {I::Stone, 2}},
        // Hand basics: smelting, glass, and the two structural staples.
        {{{I::CopperOre, 2}},                        {I::CopperIngot, 1}},
        {{{I::Stone, 1}},                            {I::ScaffoldItem, 4}},
        {{{I::Sand, 1}},                             {I::Glass, 1}},
        {{{I::Glass, 1}},                            {I::Vial, 1}},
        {{{I::Wood, 3}},                             {I::Bucket, 1}},
        // The Alchemy Circle itself -- the one piece of the factory you may
        // still build by hand, because it is the gateway to all the rest.
        {{{I::Stone, 4}, {I::CopperIngot, 1}},       {I::PedestalItem, 1}},
        {{{I::Stone, 6}, {I::CopperIngot, 2}, {I::Crystal, 1}}, {I::RuneCoreItem, 1}},
    };
}

const std::vector<Recipe>& handcraftRecipes() {
    return kRecipes;
}

namespace {
    using B = BlockId;

    // The reagent chain. Each machine type runs whichever of its recipes it has
    // the inputs for.
    const std::vector<MachineRecipe> kMachineRecipes = {
        // Grinder: grinds raws to powder. Also crushes Stone back into Sand,
        // so Sand is renewable from Stone (which is renewable from dirt+sand).
        // Plates moved to the Press, where the verb belongs -- see the note at
        // the Press rows below for what that costs in bootstrapping.
        {B::Grinder,  {{I::Herb, 1}},                              {I::GroundHerb, 1},      2.0f},
        {B::Grinder,  {{I::Crystal, 1}},                           {I::CrystalDust, 1},     2.0f},
        {B::Grinder,  {{I::Stone, 1}},                             {I::Sand, 2},            2.0f},
        // Composter: renewable Dirt from plant matter (sticks / saplings).
        {B::Composter, {{I::Stick, 3}},                            {I::DirtItem, 2},        2.5f},
        {B::Composter, {{I::SaplingItem, 1}},                      {I::DirtItem, 3},        3.0f},
        // Cauldron: powder + water -> solution/tincture
        {B::Cauldron, {{I::GroundHerb, 1}, {I::SpringWater, 1}},   {I::HerbalTincture, 1},  3.0f},
        {B::Cauldron, {{I::CrystalDust, 1}, {I::SpringWater, 1}},  {I::MineralSolution, 1}, 3.0f},
        // Infuser: tincture/solution -> potion
        {B::Infuser,  {{I::HerbalTincture, 1}, {I::Vial, 1}},      {I::HealingDraught, 1},  4.0f},
        {B::Infuser,  {{I::MineralSolution, 1}, {I::Essence, 1}},  {I::ManaVial, 1},        4.0f},
        // Alembic: potions -> elixir
        {B::Alembic,  {{I::HealingDraught, 1}, {I::ManaVial, 1}},  {I::ElixirOfVigor, 1},   5.0f},
        // Distiller/Transmuter: the philosopher's tier
        {B::Distiller,  {{I::ElixirOfVigor, 1}, {I::Essence, 1}},        {I::RefinedElixir, 1},        6.0f},
        {B::Transmuter, {{I::RefinedElixir, 1}, {I::CrystalDust, 1}},    {I::PhilosophersCatalyst, 1}, 8.0f},
        {B::Transmuter, {{I::PhilosophersCatalyst, 1}, {I::ElixirOfVigor, 1}}, {I::PhilosophersStone, 1}, 10.0f},
        // Forge: the armory. The Copper set forges from plates; the Aegis set is
        // gated on the boss drops (Void Catalyst / Storm Core), giving those
        // trophies a sink and closing the fight -> forge -> harder-fight loop.
        {B::Forge, {{I::CopperPlate, 3}},                             {I::CopperHelm, 1},  5.0f},
        {B::Forge, {{I::CopperPlate, 5}},                             {I::CopperChest, 1}, 7.0f},
        {B::Forge, {{I::CopperPlate, 3}},                             {I::CopperBoots, 1}, 5.0f},
        {B::Forge, {{I::MachineFrame, 1}, {I::VoidCatalyst, 1}},      {I::AegisHelm, 1},   8.0f},
        {B::Forge, {{I::MachineFrame, 2}, {I::VoidCatalyst, 1}, {I::StormCore, 1}}, {I::AegisChest, 1}, 12.0f},
        {B::Forge, {{I::MachineFrame, 1}, {I::StormCore, 1}},         {I::AegisBoots, 1},  8.0f},
        // Press: the shared parts tier. Every machine now reaches the Machine
        // Frame through four machine steps instead of one hand-craft, so the
        // factory has to exist before the tech tree opens up.
        //
        // Plate is FIRST on purpose. The Press is now the only plate source and
        // therefore the first machine a new game builds, and AUTO runs the
        // first recipe whose inputs are present -- so with plate second, a
        // fresh Press fed the one raw the player has (ingots) would quietly
        // make Copper Rods forever. Rod costs the same single ingot and loses
        // that tie by design: plates feed casings, etched plates, tools and
        // armor, rods feed only gears. Lock the MAKE ROD row to get rods on a
        // shared Press -- the same bargain the other four rows already make.
        // (Ordering this run repointed saved Press locks, which is what the
        // v19 save migration exists for.)
        {B::Press, {{I::CopperIngot, 1}},                             {I::CopperPlate, 1},   3.0f},
        {B::Press, {{I::CopperIngot, 1}},                             {I::CopperRod, 2},     2.0f},
        {B::Press, {{I::CopperRod, 2}},                               {I::Gear, 1},          3.0f},
        {B::Press, {{I::CopperPlate, 4}},                             {I::MachineCasing, 1}, 5.0f},
        {B::Press, {{I::CopperPlate, 1}, {I::CrystalDust, 2}},        {I::EtchedPlate, 1},   4.0f},
        {B::Press, {{I::MachineCasing, 1}, {I::Gear, 2}, {I::EtchedPlate, 1}},
                                                                     {I::MachineFrame, 1},  8.0f},
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
    // makes with its five recipes.
    const std::vector<CircleRecipe> kCircleRecipes = {
        // -- Bootstrap tier: no plates, so a Lesser circle can build the
        // machines that make plates. This is the whole reason the Lesser
        // circle runs unpowered.
        {{}, {{I::CopperIngot, 3}, {}, {I::Stone, 4}, {}},  {I::GrinderItem, 1},   6.0f},
        {{}, {{I::CopperIngot, 2}, {I::Stone, 2}, {I::Wood, 2}, {}},
                                                           {I::GeneratorItem, 1}, 6.0f},
        {{}, {{I::Wood, 6}, {}, {I::Stick, 4}, {}},        {I::ComposterItem, 1}, 5.0f},
        {{}, {{I::Wood, 6}, {}, {I::Bucket, 1}, {}},       {I::RainBarrelItem, 1}, 5.0f},
        {{}, {{I::CopperIngot, 1}, {}, {}, {}},            {I::WireItem, 2},      2.0f},
        // The Press makes the plates, so it belongs to THIS tier and must cost
        // none: ingots opposite ingots, stone opposite stone. The same raw
        // price as the old ingot+plate pattern, laid out so nothing else can
        // match it -- the Grinder and Generator both require an empty west
        // pedestal, and this one fills it. Left at its original table index on
        // purpose: circleRecipes() order is a saved index on a locked Rune Core,
        // and since the pattern is unambiguous its position buys nothing.
        {{}, {{I::CopperIngot, 2}, {I::Stone, 2}, {I::CopperIngot, 2}, {I::Stone, 2}},
                                                           {I::PressItem, 1},     8.0f},
        // -- Plate tier. Conduit and the Wrench cost the same two plates and
        // are told apart by ARRANGEMENT alone: both plates on one pedestal
        // versus one plate on each of two opposite pedestals. That is the
        // necklace earning its keep.
        // Pickaxe and Axe cost exactly the same; only the arrangement differs
        // (wood OPPOSITE the plates vs. wood BESIDE them).
        {{}, {{I::CopperPlate, 3}, {I::Wood, 2}, {}, {}},  {I::CopperAxe, 1},     4.0f},
        {{}, {{I::CopperPlate, 3}, {}, {I::Wood, 2}, {}},  {I::CopperPickaxe, 1}, 4.0f},
        {{}, {{I::CopperPlate, 2}, {}, {I::Wood, 2}, {}},  {I::CopperShovel, 1},  4.0f},
        {{}, {{I::CopperPlate, 2}, {}, {I::Wood, 1}, {}},  {I::CopperSword, 1},   4.0f},
        {{}, {{I::CopperPlate, 2}, {}, {}, {}},            {I::Conduit, 2},       3.0f},
        {{}, {{I::CopperPlate, 1}, {}, {I::CopperPlate, 1}, {}}, {I::Wrench, 1},  3.0f},
        // -- Machine Frame tier: the alchemy chain proper. Three-ingredient
        // patterns first, so they win over their two-ingredient prefixes.
        {{}, {{I::MachineFrame, 1}, {I::CopperPlate, 3}, {I::Stone, 4}, {}},
                                                           {I::MinerItem, 1},     8.0f},
        {{}, {{I::MachineFrame, 1}, {I::Glass, 2}, {I::Crystal, 1}, {}},
                                                           {I::DistillerItem, 1}, 8.0f},
        {{}, {{I::MachineFrame, 1}, {I::Crystal, 2}, {I::Essence, 1}, {}},
                                                           {I::TransmuterItem, 1}, 8.0f},
        {{}, {{I::MachineFrame, 1}, {I::Glass, 1}, {I::Vial, 1}, {}},
                                                           {I::InfuserItem, 1},   6.0f},
        {{}, {{I::MachineFrame, 1}, {}, {I::Glass, 2}, {}}, {I::CauldronItem, 1}, 6.0f},
        {{}, {{I::MachineFrame, 1}, {}, {I::CopperPlate, 2}, {}}, {I::ForgeItem, 1}, 6.0f},
        {{}, {{I::MachineFrame, 1}, {}, {I::Crystal, 1}, {}}, {I::AlembicItem, 1}, 6.0f},
        // -- Source transmutation: the catalyst goes in the CORE, the raw on
        // the ring. Resource production stays craftable, so the loop is closed.
        {{I::PhilosophersCatalyst, 1}, {{I::Herb, 8}, {}, {}, {}},
                                                           {I::HerbSourceItem, 1},    10.0f},
        {{I::PhilosophersCatalyst, 1}, {{I::Crystal, 8}, {}, {}, {}},
                                                           {I::CrystalSourceItem, 1}, 10.0f},
        {{I::PhilosophersCatalyst, 1}, {{I::CopperOre, 8}, {}, {}, {}},
                                                           {I::CopperSourceItem, 1},  10.0f},
        {{I::PhilosophersCatalyst, 1}, {{I::Sand, 8}, {}, {}, {}},
                                                           {I::SandSourceItem, 1},    10.0f},
        {{I::PhilosophersCatalyst, 1}, {{I::Essence, 8}, {}, {}, {}},
                                                           {I::EssenceSourceItem, 1}, 10.0f},
        // Resonance conversions -- the 2-for-1 is listed first because a
        // pedestal holding two Resonance also satisfies the 1-cost pattern.
        {{}, {{I::Resonance, 2}, {}, {}, {}},              {I::FusionCatalyst, 1},       5.0f},
        {{}, {{I::Resonance, 1}, {}, {}, {}},              {I::PhilosophersCatalyst, 1}, 5.0f},
        // -- GREATER tier (all eight pedestals, powered): the boss keys. The
        // full ring is the ceremony -- these are the game's biggest crafts.
        // The Teleport Key's centre cost is deliberately enormous: 100
        // catalysts is a full automated Transmuter line's output, so reaching
        // the boss is an automation problem, not a menu click.
        {{I::PhilosophersCatalyst, kTeleportKeyCatalystCost},
         {{I::Crystal, 1}, {I::Essence, 1}, {I::Crystal, 1}, {},
          {I::Crystal, 1}, {I::Essence, 1}, {I::Crystal, 1}, {}},
                                                           {I::TeleportKey, 1},   12.0f},
        // 100 Void Catalysts means 100 Void Warden kills -- the warden is the
        // only source. Unlike the Teleport Key's cost this can't be automated
        // away, so the Storm Key is gated on repetition rather than on scale.
        {{I::VoidCatalyst, kStormKeyCatalystCost},
         {{I::Crystal, 1}, {I::SpringWater, 1}, {I::Crystal, 1}, {I::SpringWater, 1},
          {I::Crystal, 1}, {I::SpringWater, 1}, {I::Crystal, 1}, {I::SpringWater, 1}},
                                                           {I::StormKey, 1},      12.0f},
        {{I::PhilosophersStone, 1},
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

std::vector<const MachineRecipe*> recipesForMachine(BlockId type) {
    std::vector<const MachineRecipe*> out;
    for (const MachineRecipe& r : kMachineRecipes) {
        if (r.machine == type) out.push_back(&r);
    }
    return out;
}
