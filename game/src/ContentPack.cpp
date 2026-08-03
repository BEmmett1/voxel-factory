#include "game/ContentPack.h"

#include "game/Block.h"
#include "game/ContentRegistry.h"
#include "game/ContentValidate.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"

#include <json.hpp>

#include <algorithm>
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

        ItemId item(const std::string& key) {
            if (key.empty()) return ItemId::None;
            const ItemId id = content::itemFromKey(key);
            if (id == content::kNoItem) {
                bad("names item '" + key + "', which this build does not have");
                return ItemId::None;
            }
            return id;
        }

        BlockId block(const std::string& key) {
            if (key.empty()) return BlockId::Air;
            const BlockId id = content::blockFromKey(key);
            if (id == content::kNoBlock) {
                bad("names block '" + key + "', which this build does not have");
                return BlockId::Air;
            }
            return id;
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

    // ---- The sections that are still compiled in --------------------------
    // Blocks, items, machine traits and fuels come from the registries in
    // Block.cpp / Item.cpp / Machine.h, so a pack cannot author them yet. It
    // may still CONTAIN them -- --dump-content writes the whole content set,
    // and a pack derived from that dump would otherwise be unfeedable to the
    // game that wrote it -- as long as it only restates what is already true.
    //
    // "State it, don't change it" is checked against the writer itself, by
    // parsing our own dump, so this can never disagree with what the dump says.
    // The diagnostic then names the row rather than the section: what a pack
    // author needs is not "blocks are compiled in" but "you tried to change
    // core:stone, and that is the part I cannot do".
    void checkCompiledSections(const json& doc, std::vector<std::string>& problems) {
        static const struct { const char* section; const char* idField; } kCompiled[] = {
            {"blocks", "key"}, {"items", "key"}, {"machines", "block"}, {"fuels", "item"},
        };

        json ours; // our own content, through the same writer the pack came from
        bool parsed = false;
        for (const auto& [section, idField] : kCompiled) {
            const auto incoming = doc.find(section);
            if (incoming == doc.end()) continue;
            if (!incoming->is_array()) {
                problems.push_back(std::string("\"") + section + "\" is not an array");
                continue;
            }
            if (incoming->empty()) continue;
            if (!parsed) { ours = json::parse(content::dumpContent()); parsed = true; }

            for (const json& row : *incoming) {
                const auto id = row.is_object() ? row.find(idField) : row.end();
                if (id == row.end() || !id->is_string()) {
                    problems.push_back(std::string("a \"") + section + "\" row has no \"" +
                                       idField + "\"");
                    continue;
                }
                const std::string key = id->get<std::string>();
                const json& mine = ours[section];
                const auto at = std::find_if(mine.begin(), mine.end(), [&](const json& r) {
                    const auto k = r.find(idField);
                    return k != r.end() && *k == *id;
                });
                if (at == mine.end()) {
                    problems.push_back("pack adds " + std::string(section) + " '" + key +
                                       "', which this build cannot do yet -- blocks, items, "
                                       "machines and fuels are still compiled in, so a pack "
                                       "may only change recipes");
                } else if (*at != row) {
                    problems.push_back("pack changes " + std::string(section) + " '" + key +
                                       "', which this build cannot do yet -- blocks, items, "
                                       "machines and fuels are still compiled in, so a pack "
                                       "may only change recipes");
                }
            }
        }
    }

} // namespace

namespace {

    std::vector<std::string> loadRecipePack(const std::string& path) {
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
        checkCompiledSections(doc, problems);
        if (!problems.empty()) return problems;

        // ---- Parse to one side ---------------------------------------------
        Reader rd;
        std::vector<Recipe> hand;
        std::vector<MachineRecipe> mach;
        std::vector<CircleRecipe> circ;
        std::vector<std::string> removals;

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

        std::vector<std::string> problems;
        for (const std::string& path : paths) {
            const std::string name = std::filesystem::path(path).filename().string();
            for (std::string& msg : loadRecipePack(path)) {
                problems.push_back(name + ": " + std::move(msg));
            }
            if (!problems.empty()) break;
        }
        if (problems.empty()) problems = validate();

        if (!problems.empty()) {
            recipes::handTable() = hand0;
            recipes::machineTable() = mach0;
            recipes::circleTable() = circ0;
        }
        return problems;
    }

} // namespace content
