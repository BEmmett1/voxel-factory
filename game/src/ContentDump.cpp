#include "game/ContentPack.h"

#include "game/Block.h"
#include "game/BlockShape.h"
#include "game/ContentRegistry.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// The content set, as data.
//
// This is the same move --dump-recipes made for RECIPES.md, aimed at a reader
// that is not a person. It exists for three jobs at once, which is why it is
// worth more than the report it resembles:
//
//   * it is the SPEC. The pack loader (ContentPack.cpp) reads back exactly what
//     is written here, so the format cannot drift from its own documentation --
//     dump -> load -> dump is byte-identical, and --selftest holds that.
//   * it is the VOCABULARY. Every key this build knows, in one place, so
//     content written against it can only name things that exist.
//   * it is the EXAMPLE. Every construct the format has, filled in with real,
//     balanced content rather than with a toy.
//
// Nothing here is referenced by ordinal. An ordinal is this build's private
// encoding (ContentRegistry.h); a key is what two content sets can agree on,
// which is the whole reason keys exist.

using json = nlohmann::ordered_json;

namespace {

    // A field at its default value is OMITTED, exactly as a kBlocks row omits
    // it: absent means default, in the registry tables and in this file alike.
    // That convention is what keeps the dump readable at 48 blocks and ~130
    // items instead of a wall of zeroes.
    template <typename T>
    void put(json& j, const char* field, const T& value, const T& fallback) {
        if (!(value == fallback)) j[field] = value;
    }

    // A float widened to double prints its binary error (0.6f becomes
    // 0.6000000238418579), which is unreadable and, worse, unwritable -- nobody
    // hand-authoring a pack would type that, so the round-trip would depend on
    // a precision no author can reproduce. Four decimals is finer than any knob
    // in the game and survives double -> float -> double unchanged.
    double num(float v) { return std::round(static_cast<double>(v) * 10000.0) / 10000.0; }

    void put(json& j, const char* field, float value, float fallback) {
        if (value != fallback) j[field] = num(value);
    }

    // Colors are the one place a pack author will want to TYPE a value rather
    // than compute it, and "#4d9e42" is how everyone writes one. It is also
    // eight times shorter than three doubles, which matters when the dump's job
    // is to fit in something's context.
    std::string hexColor(const glm::vec3& c) {
        auto byte = [](float v) {
            const int i = static_cast<int>(std::lround(v * 255.0f));
            return static_cast<unsigned>(std::clamp(i, 0, 255));
        };
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b));
        return buf;
    }

    std::string blockKey(BlockId id) { return std::string(content::blockKey(id)); }
    std::string itemKey(ItemId id) { return std::string(content::itemKey(id)); }

    // {"item": "core:wood", "count": 2}. A count of 1 is the default and drops
    // out, so the common case reads as just an item.
    json stack(const ItemStack& s) {
        if (s.id == ItemId::None) return json(nullptr);
        json j;
        j["item"] = itemKey(s.id);
        put(j, "count", s.count, 1);
        return j;
    }

    json stackList(const std::vector<ItemStack>& v) {
        json a = json::array();
        for (const ItemStack& s : v) a.push_back(stack(s));
        return a;
    }

    json dumpBlocks() {
        json a = json::array();
        for (int b = 0; b < static_cast<int>(blockCount()); ++b) {
            const BlockId id = static_cast<BlockId>(b);
            const BlockInfo& info = blockInfo(id);
            json j;
            j["key"] = blockKey(id);
            j["name"] = info.name;
            put(j, "solid", info.solid, true);
            put(j, "fullCube", info.fullCube, true);
            j["color"] = hexColor(info.color);
            put(j, "emissive", info.emissive, 0.0f);
            put(j, "machine", info.machine, false);
            put(j, "source", info.source, false);
            put(j, "node", info.node, false);
            if (info.spawnsNode != BlockId::Air) j["spawnsNode"] = blockKey(info.spawnsNode);
            if (info.drop.item != ItemId{} && info.drop.count > 0) {
                j["drop"] = stack({info.drop.item, info.drop.count});
            }
            j["tiles"] = json::object({{"top", info.tiles.top},
                                       {"side", info.tiles.side},
                                       {"bottom", info.tiles.bottom}});
            put(j, "hardness", info.hardness, 0.0f);
            if (info.tool != ToolType::None) j["tool"] = kToolNames[static_cast<std::size_t>(info.tool)];
            put(j, "toolTier", info.toolTier, 0);
            if (info.provides != SoilKind::None) {
                j["provides"] = kSoilNames[static_cast<std::size_t>(info.provides)];
            }
            if (info.needsSoil != SoilKind::None) {
                j["needsSoil"] = kSoilNames[static_cast<std::size_t>(info.needsSoil)];
            }
            // Omitted when it is the plain unit cube, like every other default.
            if (info.shape != ShapeId::FullCube) j["shape"] = shapeName(info.shape);
            a.push_back(j);
        }
        return a;
    }

    json dumpItems() {
        json a = json::array();
        for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            const ItemInfo& info = itemInfo(id);
            json j;
            j["key"] = itemKey(id);
            j["name"] = info.name;
            put(j, "atlasTile", info.atlasTile, -1);
            put(j, "placeable", info.placeable, false);
            if (info.placesBlock != BlockId::Air) j["places"] = blockKey(info.placesBlock);
            if (info.nodeBlock != BlockId::Air) j["nodeBlock"] = blockKey(info.nodeBlock);
            if (info.tool != ToolType::None) j["tool"] = kToolNames[static_cast<std::size_t>(info.tool)];
            put(j, "toolTier", info.toolTier, 0);
            put(j, "miningSpeed", info.miningSpeed, 1.0f);
            if (info.armorSlot != ArmorSlot::None) j["armorSlot"] = kArmorSlotNames[static_cast<std::size_t>(info.armorSlot)];
            put(j, "armor", info.armor, 0.0f);
            put(j, "weaponDamage", info.weaponDamage, 0.0f);
            a.push_back(j);
        }
        return a;
    }

    json dumpMachines() {
        json a = json::array();
        for (const MachineTraits& t : machineTraitRows()) {
            json j;
            j["block"] = blockKey(t.block);
            put(j, "kind", std::string(kKindNames[static_cast<std::size_t>(t.kind)]),
                std::string("processor"));
            put(j, "demand", t.demand, 5);
            put(j, "powerOutput", t.powerOutput, 0);
            put(j, "burnsFuel", t.burnsFuel, false);
            put(j, "fuelMult", t.fuelMult, 1.0f);
            if (t.collects != ItemId::None) {
                j["collects"] = itemKey(t.collects);
                j["collectCap"] = t.collectCap;
                j["collectSeconds"] = num(t.collectSeconds);
            }
            // The manual tier: whose recipe list this machine runs, and what it
            // pays for the privilege of needing no power.
            if (t.recipeGroup != BlockId::Air) j["recipeGroup"] = blockKey(t.recipeGroup);
            put(j, "speedMult", t.speedMult, 1.0f);
            put(j, "handCranked", t.handCranked, false);
            a.push_back(j);
        }
        return a;
    }

    json dumpFuels() {
        json a = json::array();
        for (const FuelInfo& f : fuelRows()) {
            a.push_back(json::object({{"item", itemKey(f.item)}, {"seconds", num(f.seconds)}}));
        }
        return a;
    }

    json dumpRecipeTables() {
        json hand = json::array();
        for (const Recipe& r : handcraftRecipes()) {
            json j;
            j["key"] = r.key;
            j["inputs"] = stackList(r.inputs);
            j["output"] = stack(r.output);
            hand.push_back(j);
        }

        json mach = json::array();
        for (const MachineRecipe& r : machineRecipes()) {
            json j;
            j["key"] = r.key;
            j["machine"] = blockKey(r.machine);
            j["inputs"] = stackList(r.inputs);
            json outs = json::array();
            for (const RecipeOutput& o : r.outputs) {
                // More than one output makes the craft a weighted ROLL; a blank
                // stack is a deliberate "nothing this time" (the Sifter).
                json e = stack(o.stack);
                if (e.is_null()) e = json::object({{"item", nullptr}});
                put(e, "weight", o.weight, 1.0f);
                outs.push_back(e);
            }
            j["outputs"] = outs;
            j["seconds"] = num(r.seconds);
            mach.push_back(j);
        }

        json circ = json::array();
        for (const CircleRecipe& r : circleRecipes()) {
            json j;
            j["key"] = r.key;
            if (r.center.id != ItemId::None) j["center"] = stack(r.center);
            // 4 slots = the cardinals (a Lesser circle can run it), 8 = the
            // full ring, clockwise from north. null = a slot that must be EMPTY.
            j["ring"] = stackList(r.ring);
            j["output"] = stack(r.output);
            j["seconds"] = num(r.seconds);
            circ.push_back(j);
        }

        return json::object({{"hand", hand}, {"machine", mach}, {"circle", circ}});
    }

} // namespace

namespace content {

    std::string dumpContent() {
        json doc;
        doc["format"] = kPackFormat;
        doc["game"] = VOXEL_FACTORY_VERSION;
        doc["blocks"] = dumpBlocks();
        doc["items"] = dumpItems();
        doc["machines"] = dumpMachines();
        doc["fuels"] = dumpFuels();
        doc["recipes"] = dumpRecipeTables();
        return doc.dump(2) + "\n";
    }

} // namespace content
