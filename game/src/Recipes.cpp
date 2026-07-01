#include "game/Recipes.h"

namespace {
    using I = ItemId;

    // The equipment hand-craft chain. Index order is stable (the menu relies on
    // it). Index 0 = Copper Ore -> Copper Ingot, etc.
    const std::vector<Recipe> kRecipes = {
        {{{I::CopperOre, 1}},                        {I::CopperIngot, 1}},
        {{{I::CopperIngot, 1}},                      {I::CopperPlate, 1}},
        {{{I::CopperPlate, 1}},                      {I::WireItem, 2}},
        {{{I::CopperPlate, 2}},                      {I::Conduit, 2}},
        {{{I::Sand, 1}},                             {I::Glass, 1}},
        {{{I::Glass, 1}},                            {I::Vial, 1}},
        {{{I::CopperPlate, 2}, {I::Stone, 1}},       {I::GrinderItem, 1}},
        {{{I::CopperPlate, 2}, {I::Glass, 1}},       {I::CauldronItem, 1}},
        {{{I::CopperPlate, 2}, {I::Crystal, 1}},     {I::MachineFrame, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 1}},      {I::InfuserItem, 1}},
        {{{I::MachineFrame, 1}, {I::Crystal, 1}},    {I::AlembicItem, 1}},
        {{{I::MachineFrame, 1}, {I::Glass, 2}},      {I::DistillerItem, 1}},
        {{{I::MachineFrame, 1}, {I::Crystal, 2}},    {I::TransmuterItem, 1}},
        {{{I::CopperPlate, 2}, {I::Crystal, 1}},     {I::GeneratorItem, 1}},
        {{{I::CopperPlate, 3}, {I::Stone, 2}},       {I::MinerItem, 1}},
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
        // Grinder: raw -> powder
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
