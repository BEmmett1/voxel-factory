#include "game/ContentPack.h"

#include "game/Block.h"
#include "game/BlockShape.h"
#include "game/ContentRegistry.h"
#include "game/ContentValidate.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"

#include <json.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Reading a content pack.
//
// The writer (ContentDump.cpp) is the spec, so this file's job is to accept
// exactly what that writes and to say -- clearly, in one line per problem --
// why it will not accept anything else. Everything a pack can get wrong is a
// diagnostic naming the key it is about, because the caller of this is often
// not a person: it is something that will read the complaint and try again.
//
// Two rules make a bad pack harmless. Nothing is applied unless EVERYTHING
// parses (the tables are built to one side and swapped in at the end), and the
// caller re-runs content::validate() afterwards, so a pack that parses
// perfectly and deadlocks the tech tree is refused just as firmly as one with
// a typo.

using json = nlohmann::json;

namespace {

    // ---- Small readers ----------------------------------------------------
    // Every one of these appends a diagnostic and returns a harmless value
    // rather than throwing, so one pass over a broken pack reports every
    // problem in it instead of only the first.
    struct Reader {
        std::vector<std::string> problems;
        std::string where; // the row being read, for the message

        void bad(const std::string& msg) { problems.push_back(where + msg); }

        bool wantObject(const json& j) {
            if (j.is_object()) return true;
            bad("is not an object");
            return false;
        }

        std::string str(const json& j, const char* field, bool required = true) {
            const auto it = j.find(field);
            if (it == j.end() || !it->is_string()) {
                if (required) bad(std::string("has no \"") + field + "\" string");
                return {};
            }
            return it->get<std::string>();
        }

        float number(const json& j, const char* field, float fallback) {
            const auto it = j.find(field);
            if (it == j.end()) return fallback;
            if (!it->is_number()) {
                bad(std::string("has a non-numeric \"") + field + "\"");
                return fallback;
            }
            return it->get<float>();
        }

        // Key -> id against the STAGED tables rather than the live registry, so
        // a row may name content the same pack is adding. Which section came
        // first in the file is then not something an author has to know.
        const std::vector<BlockInfo>* blocks = nullptr;
        const std::vector<ItemInfo>*  items = nullptr;

        ItemId item(const std::string& key) {
            if (key.empty()) return ItemId::None;
            for (std::size_t i = 0; i < items->size(); ++i) {
                if (key == (*items)[i].key) return static_cast<ItemId>(i);
            }
            bad("names item '" + key + "', which neither this build nor this pack has");
            return ItemId::None;
        }

        BlockId block(const std::string& key) {
            if (key.empty()) return BlockId::Air;
            for (std::size_t i = 0; i < blocks->size(); ++i) {
                if (key == (*blocks)[i].key) return static_cast<BlockId>(i);
            }
            bad("names block '" + key + "', which neither this build nor this pack has");
            return BlockId::Air;
        }

        // Enum spellings. The dump writes these names, so the loader reading
        // the same table is what keeps the two from drifting.
        template <std::size_t N, typename E>
        E named(const json& j, const char* field, const char* const (&names)[N], E fallback) {
            const auto it = j.find(field);
            if (it == j.end()) return fallback;
            if (it->is_string()) {
                const std::string s = it->get<std::string>();
                for (std::size_t i = 0; i < N; ++i) {
                    if (s == names[i]) return static_cast<E>(i);
                }
            }
            bad(std::string("has an unknown \"") + field + "\"");
            return fallback;
        }

        bool boolean(const json& j, const char* field, bool fallback) {
            const auto it = j.find(field);
            if (it == j.end()) return fallback;
            if (!it->is_boolean()) {
                bad(std::string("has a non-boolean \"") + field + "\"");
                return fallback;
            }
            return it->get<bool>();
        }

        int integer(const json& j, const char* field, int fallback) {
            const auto it = j.find(field);
            if (it == j.end()) return fallback;
            if (!it->is_number_integer()) {
                bad(std::string("has a non-integer \"") + field + "\"");
                return fallback;
            }
            return it->get<int>();
        }

        // "#4d9e42" -- see hexColor() in ContentDump.cpp for why a colour is a
        // string here rather than three numbers.
        glm::vec3 color(const json& j) {
            const auto it = j.find("color");
            if (it == j.end()) return glm::vec3(0.0f);
            const std::string s = it->is_string() ? it->get<std::string>() : std::string();
            if (s.size() != 7 || s[0] != '#') {
                bad("has a \"color\" that is not \"#rrggbb\"");
                return glm::vec3(0.0f);
            }
            auto channel = [&](std::size_t at) {
                return static_cast<float>(std::stoi(s.substr(at, 2), nullptr, 16)) / 255.0f;
            };
            try {
                return {channel(1), channel(3), channel(5)};
            } catch (const std::exception&) {
                bad("has a \"color\" that is not \"#rrggbb\"");
                return glm::vec3(0.0f);
            }
        }

        // {"item": "core:wood", "count": 2}, or null for "this slot is empty".
        // A count is optional and defaults to 1, matching the dump.
        ItemStack stack(const json& j) {
            if (j.is_null()) return {};
            if (!j.is_object()) { bad("has a stack that is not an object"); return {}; }
            const auto it = j.find("item");
            if (it != j.end() && it->is_null()) return {}; // a blank weighted output
            const ItemStack s{item(str(j, "item")),
                              static_cast<int>(number(j, "count", 1.0f))};
            if (s.count < 1) bad("has a stack with a count below 1");
            return s;
        }

        std::vector<ItemStack> stackList(const json& j, const char* field) {
            std::vector<ItemStack> out;
            const auto it = j.find(field);
            if (it == j.end()) return out;
            if (!it->is_array()) { bad(std::string("has a \"") + field + "\" that is not an array"); return out; }
            for (const json& e : *it) out.push_back(stack(e));
            return out;
        }
    };

    // ---- Keyed merge ------------------------------------------------------
    // A row whose key matches an existing one REPLACES it in place; anything
    // else is appended. Replacing in place is what makes a dump round-trip: a
    // pack generated from the live tables re-states every row it already has,
    // and the order has to survive that. It is also what a rebalance means --
    // "this recipe, but cheaper" should not move it behind the pattern that
    // shadows it.
    template <typename Row>
    void merge(std::vector<Row>& table, std::vector<Row>& incoming) {
        for (Row& row : incoming) {
            const auto at = std::find_if(table.begin(), table.end(),
                                         [&](const Row& r) { return r.key == row.key; });
            if (at != table.end()) *at = std::move(row);
            else table.push_back(std::move(row));
        }
    }

    template <typename Row>
    void removeKeys(std::vector<Row>& table, const std::vector<std::string>& keys) {
        table.erase(std::remove_if(table.begin(), table.end(),
                                   [&](const Row& r) {
                                       return std::find(keys.begin(), keys.end(), r.key) !=
                                              keys.end();
                                   }),
                    table.end());
    }

    // ---- Strings a pack owns ----------------------------------------------
    // BlockInfo/ItemInfo hold `const char*` because every row used to be a
    // string literal with static storage duration. A loaded row's key and name
    // have to live at least as long, and registry rows are never freed, so this
    // arena is deliberately never emptied. A deque, not a vector: the addresses
    // handed out must survive the next push.
    const char* intern(std::string s) {
        static std::deque<std::string> arena;
        arena.push_back(std::move(s));
        return arena.back().c_str();
    }

} // namespace

namespace {

    std::vector<std::string> loadPack(const std::string& path) {
        std::vector<std::string> problems;

        json doc;
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return {"cannot open pack '" + path + "'"};
            try {
                in >> doc;
            } catch (const std::exception& e) {
                return {"pack '" + path + "' is not valid JSON: " + e.what()};
            }
        }
        if (!doc.is_object()) return {"pack '" + path + "' is not a JSON object"};

        const auto fmt = doc.find("format");
        if (fmt == doc.end() || !fmt->is_number_integer()) {
            problems.push_back("pack has no integer \"format\" field (this build reads " +
                               std::to_string(content::kPackFormat) + ")");
        } else if (fmt->get<int>() != content::kPackFormat) {
            problems.push_back("pack is format " + std::to_string(fmt->get<int>()) +
                               "; this build reads " + std::to_string(content::kPackFormat));
        }
        if (!problems.empty()) return problems;

        // ---- Parse to one side ---------------------------------------------
        // The staged copies. Nothing below touches a live registry until the
        // very end, so a pack that fails anywhere leaves the game untouched
        // rather than half converted.
        Reader rd;
        std::vector<BlockInfo> blocks = blockRows();
        std::vector<ItemInfo> items = itemRows();
        std::vector<MachineTraits> traits = machineTraitRows();
        std::vector<FuelInfo> fuels = fuelRows();
        rd.blocks = &blocks;
        rd.items = &items;
        std::vector<Recipe> hand;
        std::vector<MachineRecipe> mach;
        std::vector<CircleRecipe> circ;
        std::vector<std::string> removals;

        auto topSection = [&](const char* name) -> const json& {
            static const json none = json::array();
            const auto it = doc.find(name);
            if (it == doc.end()) return none;
            if (!it->is_array()) {
                rd.where = std::string("\"") + name + "\" ";
                rd.bad("is not an array");
                return none;
            }
            return *it;
        };

        // ---- Pass 1: every new key exists before any field is resolved -----
        // Rows point at each other by key -- a block's drop names an item, an
        // item places a block -- and a pack adding both would otherwise depend
        // on which section it happened to write first. Declaring the keys up
        // front makes file order stop mattering, which is one less rule for an
        // author (or a generator) to get right.
        auto declare = [&](const char* section, const char* what, auto& table, auto make) {
            for (const json& j : topSection(section)) {
                rd.where = std::string("a ") + what + " ";
                if (!rd.wantObject(j)) continue;
                const std::string key = rd.str(j, "key");
                if (key.empty()) continue;
                const bool known = std::any_of(table.begin(), table.end(),
                                               [&](const auto& r) { return key == r.key; });
                if (!known) table.push_back(make(intern(key)));
            }
        };
        declare("blocks", "block", blocks, [](const char* k) {
            // A placeholder only has to be findable by key; pass 2 overwrites
            // every other field.
            BlockInfo b{};
            b.key = k;
            b.name = k;
            return b;
        });
        declare("items", "item", items, [](const char* k) {
            ItemInfo it{};
            it.key = k;
            it.name = k;
            return it;
        });

        // ---- Pass 2: blocks -------------------------------------------------
        for (const json& j : topSection("blocks")) {
            rd.where = "a block ";
            if (!j.is_object()) continue;
            const std::string key = rd.str(j, "key");
            if (key.empty()) continue;
            rd.where = "block '" + key + "' ";
            const auto at = std::find_if(blocks.begin(), blocks.end(),
                                         [&](const BlockInfo& b) { return key == b.key; });
            BlockInfo b{};
            b.id = static_cast<BlockId>(at - blocks.begin());
            b.key = at->key;   // already interned (or a compiled literal)
            b.name = j.contains("name") ? intern(rd.str(j, "name")) : at->name;
            b.solid = rd.boolean(j, "solid", true);
            b.fullCube = rd.boolean(j, "fullCube", true);
            b.color = rd.color(j);
            b.emissive = rd.number(j, "emissive", 0.0f);
            b.machine = rd.boolean(j, "machine", false);
            b.source = rd.boolean(j, "source", false);
            b.node = rd.boolean(j, "node", false);
            b.spawnsNode = rd.block(rd.str(j, "spawnsNode", false));
            if (const auto d = j.find("drop"); d != j.end()) {
                const ItemStack s = rd.stack(*d);
                b.drop = {s.id, s.count};
            }
            if (const auto t = j.find("tiles"); t != j.end() && t->is_object()) {
                b.tiles = {rd.integer(*t, "top", 0), rd.integer(*t, "side", 0),
                           rd.integer(*t, "bottom", 0)};
            }
            b.hardness = rd.number(j, "hardness", 0.0f);
            b.tool = rd.named(j, "tool", kToolNames, ToolType::None);
            b.toolTier = rd.integer(j, "toolTier", 0);
            // Shapes are baked from Blockbench models, so a pack may only NAME
            // one that exists -- which is also why this is the one content
            // reference that is a plain name rather than a namespaced key.
            b.shape = rd.named(j, "shape", kShapeNames, ShapeId::FullCube);
            *at = b;
        }

        // ---- Pass 2: items --------------------------------------------------
        for (const json& j : topSection("items")) {
            rd.where = "an item ";
            if (!j.is_object()) continue;
            const std::string key = rd.str(j, "key");
            if (key.empty()) continue;
            rd.where = "item '" + key + "' ";
            const auto at = std::find_if(items.begin(), items.end(),
                                         [&](const ItemInfo& r) { return key == r.key; });
            ItemInfo it{};
            it.id = static_cast<ItemId>(at - items.begin());
            it.key = at->key;
            it.name = j.contains("name") ? intern(rd.str(j, "name")) : at->name;
            it.atlasTile = rd.integer(j, "atlasTile", -1);
            it.placeable = rd.boolean(j, "placeable", false);
            it.placesBlock = rd.block(rd.str(j, "places", false));
            it.nodeBlock = rd.block(rd.str(j, "nodeBlock", false));
            it.tool = rd.named(j, "tool", kToolNames, ToolType::None);
            it.toolTier = rd.integer(j, "toolTier", 0);
            it.miningSpeed = rd.number(j, "miningSpeed", 1.0f);
            it.armorSlot = rd.named(j, "armorSlot", kArmorSlotNames, ArmorSlot::None);
            it.armor = rd.number(j, "armor", 0.0f);
            it.weaponDamage = rd.number(j, "weaponDamage", 0.0f);
            *at = it;
        }

        // ---- Pass 2: machine traits and fuels -------------------------------
        // Keyed on the block and the item respectively, and replaced in place
        // for the same reason recipes are.
        for (const json& j : topSection("machines")) {
            rd.where = "a machine ";
            if (!rd.wantObject(j)) continue;
            const std::string key = rd.str(j, "block");
            rd.where = "machine '" + key + "' ";
            MachineTraits t{};
            t.block = rd.block(key);
            t.kind = rd.named(j, "kind", kKindNames, MachineKind::Processor);
            t.demand = rd.integer(j, "demand", 5);
            t.powerOutput = rd.integer(j, "powerOutput", 0);
            t.burnsFuel = rd.boolean(j, "burnsFuel", false);
            t.fuelMult = rd.number(j, "fuelMult", 1.0f);
            t.collects = rd.item(rd.str(j, "collects", false));
            t.collectCap = rd.integer(j, "collectCap", 0);
            t.collectSeconds = rd.number(j, "collectSeconds", 0.0f);
            t.recipeGroup = rd.block(rd.str(j, "recipeGroup", false));
            t.speedMult = rd.number(j, "speedMult", 1.0f);
            t.handCranked = rd.boolean(j, "handCranked", false);
            const auto at = std::find_if(traits.begin(), traits.end(),
                                         [&](const MachineTraits& r) { return r.block == t.block; });
            if (at != traits.end()) *at = t;
            else traits.push_back(t);
        }

        for (const json& j : topSection("fuels")) {
            rd.where = "a fuel ";
            if (!rd.wantObject(j)) continue;
            const std::string key = rd.str(j, "item");
            rd.where = "fuel '" + key + "' ";
            const FuelInfo f{rd.item(key), rd.number(j, "seconds", 0.0f)};
            const auto at = std::find_if(fuels.begin(), fuels.end(),
                                         [&](const FuelInfo& r) { return r.item == f.item; });
            if (at != fuels.end()) *at = f;
            else fuels.push_back(f);
        }

        const auto recipes = doc.find("recipes");
        if (recipes != doc.end() && !recipes->is_object()) {
            return {"\"recipes\" is not an object"};
        }
        const json empty = json::object();
        const json& rec = recipes != doc.end() ? *recipes : empty;

        auto section = [&](const char* name) -> const json& {
            static const json none = json::array();
            const auto it = rec.find(name);
            if (it == rec.end()) return none;
            if (!it->is_array()) {
                rd.where = std::string("recipes.") + name + " ";
                rd.bad("is not an array");
                return none;
            }
            return *it;
        };

        for (const json& j : section("hand")) {
            Recipe r;
            rd.where = "a hand recipe ";
            if (!rd.wantObject(j)) continue;
            r.key = rd.str(j, "key");
            rd.where = "hand recipe '" + r.key + "' ";
            r.inputs = rd.stackList(j, "inputs");
            const auto out = j.find("output");
            if (out == j.end()) rd.bad("has no \"output\"");
            else r.output = rd.stack(*out);
            hand.push_back(std::move(r));
        }

        for (const json& j : section("machine")) {
            MachineRecipe r;
            rd.where = "a machine recipe ";
            if (!rd.wantObject(j)) continue;
            r.key = rd.str(j, "key");
            rd.where = "machine recipe '" + r.key + "' ";
            r.machine = rd.block(rd.str(j, "machine"));
            r.inputs = rd.stackList(j, "inputs");
            const auto outs = j.find("outputs");
            if (outs == j.end() || !outs->is_array() || outs->empty()) {
                rd.bad("has no \"outputs\" array");
            } else {
                for (const json& o : *outs) {
                    // One entry is an ordinary deterministic craft; more than
                    // one makes it a weighted roll.
                    r.outputs.push_back({rd.stack(o), rd.number(o, "weight", 1.0f)});
                }
            }
            r.seconds = rd.number(j, "seconds", 1.0f);
            if (r.seconds <= 0.0f) rd.bad("has a \"seconds\" of zero or less");
            mach.push_back(std::move(r));
        }

        for (const json& j : section("circle")) {
            CircleRecipe r;
            rd.where = "a circle recipe ";
            if (!rd.wantObject(j)) continue;
            r.key = rd.str(j, "key");
            rd.where = "circle recipe '" + r.key + "' ";
            const auto centre = j.find("center");
            if (centre != j.end()) r.center = rd.stack(*centre);
            r.ring = rd.stackList(j, "ring");
            // Checked here as well as in validate() so the message can name the
            // count: 4 is the cardinals (a Lesser circle can run it), 8 is the
            // full ring, and nothing else is a necklace.
            if (r.ring.size() != 4 && r.ring.size() != 8) {
                rd.bad("has " + std::to_string(r.ring.size()) +
                       " ring slots; a pattern needs 4 (the cardinals) or 8");
            }
            const auto out = j.find("output");
            if (out == j.end()) rd.bad("has no \"output\"");
            else r.output = rd.stack(*out);
            r.seconds = rd.number(j, "seconds", 1.0f);
            circ.push_back(std::move(r));
        }

        // "remove": ["hand/bucket", "press/gear"] -- keys, across all three
        // tables, so a total conversion can clear the ground it wants.
        const auto rem = rec.find("remove");
        if (rem != rec.end()) {
            if (!rem->is_array()) {
                rd.where = "recipes.remove ";
                rd.bad("is not an array");
            } else {
                for (const json& k : *rem) {
                    if (k.is_string()) removals.push_back(k.get<std::string>());
                    else { rd.where = "recipes.remove "; rd.bad("holds a non-string key"); }
                }
            }
        }

        if (!rd.problems.empty()) return rd.problems;

        // ---- Apply, all at once ---------------------------------------------
        // Nothing above this line touched the live tables, so a pack that fails
        // leaves the game exactly as it was rather than half-converted.
        restoreBlocks(std::move(blocks));
        restoreItems(std::move(items));
        restoreMachineTraits(std::move(traits));
        restoreFuels(std::move(fuels));
        removeKeys(recipes::handTable(), removals);
        removeKeys(recipes::machineTable(), removals);
        removeKeys(recipes::circleTable(), removals);
        merge(recipes::handTable(), hand);
        merge(recipes::machineTable(), mach);
        merge(recipes::circleTable(), circ);
        return {};
    }

} // namespace

namespace content {

    std::vector<std::string> findPacks(const std::string& dir) {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
                out.push_back(entry.path().string());
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<std::string> applyPacks(const std::vector<std::string>& paths) {
        if (paths.empty()) return {};

        // The undo. Copying three vectors is nothing next to what it buys: a
        // pack can be rejected AFTER it has been applied, which is the only way
        // to find out whether the tech tree it describes actually closes.
        const std::vector<Recipe> hand0 = recipes::handTable();
        const std::vector<MachineRecipe> mach0 = recipes::machineTable();
        const std::vector<CircleRecipe> circ0 = recipes::circleTable();
        const std::vector<BlockInfo> blocks0 = blockRows();
        const std::vector<ItemInfo> items0 = itemRows();
        const std::vector<MachineTraits> traits0 = machineTraitRows();
        const std::vector<FuelInfo> fuels0 = fuelRows();

        std::vector<std::string> problems;
        for (const std::string& path : paths) {
            const std::string name = std::filesystem::path(path).filename().string();
            for (std::string& msg : loadPack(path)) {
                problems.push_back(name + ": " + std::move(msg));
            }
            if (!problems.empty()) break;
        }
        if (problems.empty()) problems = validate();

        if (!problems.empty()) {
            recipes::handTable() = hand0;
            recipes::machineTable() = mach0;
            recipes::circleTable() = circ0;
            restoreBlocks(blocks0);
            restoreItems(items0);
            restoreMachineTraits(traits0);
            restoreFuels(fuels0);
        }
        return problems;
    }

} // namespace content
