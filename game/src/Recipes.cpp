#include "game/Recipes.h"

namespace {
    using I = ItemId;

    // The equipment hand-craft chain (v2: machine-made plates). Index order is
    // stable (the menu relies on it). Only the Generator and Grinder can be
    // built without Copper Plates -- plates come from a powered, fueled
    // Grinder, so the tech tree bootstraps through automation.
    const std::vector<Recipe> kRecipes = {
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
        {{{I::CopperPlate, 3}, {I::Crystal, 1}, {I::Wood, 2}}, {I::MachineFrame, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 2}},      {I::CauldronItem, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 1}, {I::Vial, 1}}, {I::InfuserItem, 1}},
        {{{I::MachineFrame, 1}, {I::Crystal, 1}},    {I::AlembicItem, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 2}, {I::Crystal, 1}}, {I::DistillerItem, 1}},
        {{{I::MachineFrame, 1}, {I::Crystal, 2}, {I::Essence, 1}}, {I::TransmuterItem, 1}},
        {{{I::MachineFrame, 1}, {I::CopperPlate, 3}, {I::Stone, 4}}, {I::MinerItem, 1}},
        // End-game: transmute new resource sources from a catalyst + the raw.
        // This closes the loop -- resource production itself is craftable.
        {{{I::PhilosophersCatalyst, 1}, {I::Herb, 8}},        {I::HerbSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::Crystal, 8}},     {I::CrystalSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::CopperOre, 8}},   {I::CopperSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::Sand, 8}},        {I::SandSourceItem, 1}},
        {{{I::PhilosophersCatalyst, 1}, {I::Essence, 8}},     {I::EssenceSourceItem, 1}},
        // The ticket to the boss arena: philosopher-adjacent and consumed on
        // use -- the factory produces both your gear and your fights.
        {{{I::PhilosophersCatalyst, 1}, {I::Crystal, 4}, {I::Essence, 2}}, {I::TeleportKey, 1}},
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
        // needs) and grinds raws to powder.
        {B::Grinder,  {{I::CopperIngot, 1}},                       {I::CopperPlate, 1},     3.0f},
        {B::Grinder,  {{I::Herb, 1}},                              {I::GroundHerb, 1},      2.0f},
        {B::Grinder,  {{I::Crystal, 1}},                           {I::CrystalDust, 1},     2.0f},
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
