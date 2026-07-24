#include "game/Recipes.h"

namespace {
    using I = ItemId;

    // The equipment hand-craft chain (v2: machine-made plates). Index order is
    // stable (the menu relies on it). Only the Generator and Grinder can be
    // built without Copper Plates -- plates come from a powered, fueled
    // Grinder, so the tech tree bootstraps through automation.
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
        // The Composter machine: turns renewable plant matter into Dirt.
        {{{I::Wood, 6}, {I::Stick, 4}},              {I::ComposterItem, 1}},
        // Hand basics.
        {{{I::CopperOre, 2}},                        {I::CopperIngot, 1}},
        {{{I::CopperIngot, 1}},                      {I::WireItem, 2}},
        {{{I::Stone, 1}},                            {I::ScaffoldItem, 4}},
        {{{I::Sand, 1}},                             {I::Glass, 1}},
        {{{I::Glass, 1}},                            {I::Vial, 1}},
        {{{I::Wood, 3}},                             {I::Bucket, 1}},
        // The bootstrap pair: buildable straight from ingots.
        {{{I::CopperIngot, 3}, {I::Stone, 4}},       {I::GrinderItem, 1}},
        {{{I::CopperIngot, 2}, {I::Stone, 2}, {I::Wood, 2}}, {I::GeneratorItem, 1}},
        {{{I::Wood, 6}, {I::Bucket, 1}},             {I::RainBarrelItem, 1}},
        // Plate-gated gear (plates are Grinder-made).
        {{{I::CopperPlate, 2}},                      {I::Conduit, 2}},
        {{{I::CopperPlate, 2}},                      {I::Wrench, 1}},
        {{{I::CopperPlate, 2}, {I::Wood, 1}},        {I::CopperSword, 1}},
        // Copper tools (top tier): gate copper-tier blocks; the fastest.
        {{{I::CopperPlate, 3}, {I::Wood, 2}},        {I::CopperPickaxe, 1}},
        {{{I::CopperPlate, 3}, {I::Wood, 2}},        {I::CopperAxe, 1}},
        {{{I::CopperPlate, 2}, {I::Wood, 2}},        {I::CopperShovel, 1}},
        {{{I::CopperPlate, 3}, {I::Crystal, 1}, {I::Wood, 2}}, {I::MachineFrame, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 2}},      {I::CauldronItem, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 1}, {I::Vial, 1}}, {I::InfuserItem, 1}},
        {{{I::MachineFrame, 1}, {I::Crystal, 1}},    {I::AlembicItem, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 2}, {I::Crystal, 1}}, {I::DistillerItem, 1}},
        {{{I::MachineFrame, 1}, {I::Crystal, 2}, {I::Essence, 1}}, {I::TransmuterItem, 1}},
        {{{I::MachineFrame, 1}, {I::CopperPlate, 3}, {I::Stone, 4}}, {I::MinerItem, 1}},
        // The Forge: the armory machine. Weapons/armor are forged, not
        // hand-crafted, so gearing up is an automation problem.
        {{{I::MachineFrame, 1}, {I::CopperPlate, 2}}, {I::ForgeItem, 1}},
        // End-game: transmute new resource sources from a catalyst + the raw.
        // This closes the loop -- resource production itself is craftable.
        {{{I::PhilosophersCatalyst, 1}, {I::Herb, 8}},        {I::HerbSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::Crystal, 8}},     {I::CrystalSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::CopperOre, 8}},   {I::CopperSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::Sand, 8}},        {I::SandSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::Essence, 8}},     {I::EssenceSourceItem, 1}},
        // Tickets to the boss arenas: philosopher-adjacent and consumed on
        // use -- the factory produces both your gear and your fights. The
        // Storm Key is gated on beating the Void Warden (its catalyst).
        {{{I::PhilosophersCatalyst, 1}, {I::Crystal, 4}, {I::Essence, 2}}, {I::TeleportKey, 1}},
        {{{I::VoidCatalyst, 1}, {I::Crystal, 4}, {I::SpringWater, 4}},     {I::StormKey, 1}},
        // Source fusion. The bootstrap catalyst finally gives the
        // Philosopher's Stone (the chain's dead-end trophy) a use; fusion
        // output then sustains more catalysts AND converts to premium
        // Philosopher's Catalyst -- closing the loop the same way the source
        // crafts above do (resource production is itself craftable).
        {{{I::PhilosophersStone, 1}, {I::Crystal, 2}, {I::Essence, 2}}, {I::FusionCatalyst, 1}},
        {{{I::Resonance, 2}},                                          {I::FusionCatalyst, 1}},
        {{{I::Resonance, 1}},                                          {I::PhilosophersCatalyst, 1}},
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
        // Grinder: presses plates (the machine-made part every later machine
        // needs) and grinds raws to powder. Also crushes Stone back into Sand,
        // so Sand is renewable from Stone (which is renewable from dirt+sand).
        {B::Grinder,  {{I::CopperIngot, 1}},                       {I::CopperPlate, 1},     3.0f},
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
    };
}

const std::vector<MachineRecipe>& machineRecipes() {
    return kMachineRecipes;
}

std::vector<const MachineRecipe*> recipesForMachine(BlockId type) {
    std::vector<const MachineRecipe*> out;
    for (const MachineRecipe& r : kMachineRecipes) {
        if (r.machine == type) out.push_back(&r);
    }
    return out;
}
