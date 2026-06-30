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
        {{{I::CopperPlate, 2}, {I::Crystal, 1}},     {I::GeneratorItem, 1}},
        {{{I::CopperPlate, 3}, {I::Stone, 2}},       {I::MinerItem, 1}},
    };
}

const std::vector<Recipe>& handcraftRecipes() {
    return kRecipes;
}
