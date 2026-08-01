#pragma once

#include "game/Item.h"

#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// RECIPE KEYS: why every row carries one.
//
// A machine locked to a MAKE row stores that choice in Machine::selectedRecipe,
// which is a POSITION in its recipe list. Saving that position directly is what
// used to make these tables append-only: move a row and every save in existence
// silently starts making something else (the v19 save migration exists because
// of exactly that).
//
// So the tables are keyed instead. Each row owns a short, stable string -- the
// key is the recipe's identity, its position is not -- and the save stores the
// KEY. Rows may then be reordered, retimed, rebalanced, or deleted freely; a
// lock on a recipe that no longer exists resolves to -1 (AUTO) on load rather
// than to whatever row inherited its index.
//
// Two rules remain, and they are unrelated to ordering:
//   * keys must be unique within their table (--selftest checks this), and
//   * BlockId/ItemId enums are still APPEND-ONLY -- those ordinals really are
//     the save encoding for blocks and inventories.
//
// Convention: "<surface>/<output>", e.g. "hand/wood-pickaxe", "press/plate",
// "circle/teleport-key". A key is never shown to the player, so rename it only
// when you mean "this is a different recipe now".
// ---------------------------------------------------------------------------

// A crafting recipe: consume `inputs`, produce `output`.
struct Recipe {
    const char*            key;
    std::vector<ItemStack> inputs;
    ItemStack              output;
};

// Hand-craft recipes available in the crafting menu (the survival tier).
const std::vector<Recipe>& handcraftRecipes();

// A recipe a machine performs over time when supplied (and powered, or fueled,
// depending on the machine's traits row).
//
// `outputs` holds one entry for an ordinary recipe. More than one makes the
// result a WEIGHTED ROLL -- one entry is drawn per craft, weights need not sum
// to anything in particular, and an entry whose stack is {None, 0} is a
// deliberate blank (the Sifter's "nothing this time"). outputs[0] is the
// recipe's headline product: it names the row in the panel and is what the
// tech-tree reachability check follows.
struct RecipeOutput {
    ItemStack stack;
    float     weight = 1.0f;
};

struct MachineRecipe {
    const char*               key;
    BlockId                   machine;
    std::vector<ItemStack>    inputs;
    std::vector<RecipeOutput> outputs;
    float                     seconds; // processing time, before MachineTraits::speedMult
};

// The headline product of a recipe (outputs[0]); {None, 0} if it has none.
ItemStack primaryOutput(const MachineRecipe& r);

// The reagent-processing chain (grinder/cauldron/infuser/alembic/...).
const std::vector<MachineRecipe>& machineRecipes();

// The recipes a given machine type can run, in table order (the order the
// machine panel lists them and Machine::selectedRecipe indexes them at
// RUNTIME -- the save stores the key, not the index). Resolves through
// MachineTraits::recipeGroup, so a manual twin runs its powered counterpart's
// list and a recipe is authored exactly once.
std::vector<const MachineRecipe*> recipesForMachine(BlockId type);

// Key <-> runtime index for a machine's recipe list. `recipeIndexForKey`
// returns -1 for an empty or unknown key, which is the AUTO sentinel and the
// safe landing spot for a save that locked a recipe since deleted.
int         recipeIndexForKey(BlockId type, std::string_view key);
const char* recipeKeyFor(BlockId type, int index);

// An Alchemy Circle pattern: a ring NECKLACE plus an optional catalyst in the
// Rune Core's own buffer. `ring` holds 4 entries (the cardinal pedestals, a
// Lesser circle can run it) or 8 (the full ring, Greater only), listed
// clockwise; {None, 0} is a slot that must be EMPTY. Slots match on
// "holds at least this many", so belt-fed pedestals keep a circle running.
//
// Matching is rotation-invariant, which is what keeps same-ingredient recipes
// distinct by ARRANGEMENT (two plates on one pedestal vs. one on each of two)
// without ever punishing which way the player faced when they built it.
//
// Keyed like the machine table, and freely reorderable for the same reason --
// which matters more here, because ORDER IS GAMEPLAY: a pattern that is a
// superset of another must be listed first, so rebalancing one recipe often
// means moving it.
struct CircleRecipe {
    const char*            key;
    ItemStack              center{};  // catalyst in the core; {None, 0} = none
    std::vector<ItemStack> ring;      // 4 or 8 slots, clockwise
    ItemStack              output;
    float                  seconds;
};

// The boss keys' centre catalyst costs -- the game's two biggest sinks, named
// here so the recipe table and --selftest can't drift apart. They gate very
// differently: the Teleport Key's catalyst is farmable, so its cost is an
// automation target, while the Storm Key's comes only from Void Warden kills.
inline constexpr int kTeleportKeyCatalystCost = 100;
inline constexpr int kStormKeyCatalystCost    = 100;

// Every Alchemy Circle pattern, in match order (first match wins).
const std::vector<CircleRecipe>& circleRecipes();

// Key <-> index for the circle table, same contract as the machine pair above.
int         circleIndexForKey(std::string_view key);
const char* circleKeyFor(int index);
