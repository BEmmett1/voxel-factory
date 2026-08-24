#include "game/SaveSystem.h"

#include "game/World.h"
#include "game/Chunk.h"
#include "game/Block.h"
#include "game/ContentRegistry.h"
#include "game/Item.h"
#include "game/Machine.h"
#include "game/Recipes.h"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

    constexpr std::uint32_t kMagic = 0x53465856u; // "VXFS"
    constexpr std::uint32_t kVersion = 25;        // bump when enums/layout change
    // Append-only growth stays loadable: v10 appended the player-health float
    // (older saves keep the caller's default), v11 appended ItemId entries
    // at the enum tail (readInventory accepts older, shorter item sets),
    // v12 appended the ten hotbar slot ids, v13/v14 appended the per-boss
    // defeated flags, v15 appended the playtime double, and v16 appended the
    // ground-item drops. v17 only GREW the BlockId/ItemId enums at their tails
    // (Composter block + stick/pebble/tool items) — no layout change, so old
    // saves still load. v18 appended the three equipped-armor slot ids at the
    // tail (older saves default to unarmored) and GREW the enums (Forge block +
    // armor/Forge items). v19 was the first entry that was NOT a tail append:
    // Ingot -> Copper Plate moved off the Grinder onto the Press, shifting both
    // machines' recipe lists under saved locks, and had to be migrated by
    // shuffling indices on load.
    //
    // v20 ends that whole class of problem. A machine's locked recipe is now
    // stored as the recipe's KEY (a length-prefixed string; empty = AUTO)
    // instead of its position, so the recipe tables can be reordered, edited,
    // and deleted freely -- see Recipes.h. Pre-v20 saves still carry an index,
    // which is migrated through the frozen v19 order snapshot below, so v19's
    // bespoke remap is gone with it. v20 also appends `burnLeft` to each
    // machine record (fuel-fired Processors); older saves default it to 0,
    // which just means the burner relights on its next craft.
    //
    // v21 appends a third INVENTORY to each machine record: the dedicated fuel
    // buffer that machines with recipes now keep their firewood in (see
    // usesFuelSlot in Machine.h). A tail append, so older saves still load --
    // but they load with an EMPTY fuel slot and their charcoal still sitting in
    // `input`, which would leave every existing Furnace stone cold. So the read
    // path migrates: see the fuel sweep in readMachines().
    //
    // v22 does for BLOCK AND ITEM IDS what v20 did for recipe locks: the file
    // now names its content instead of assuming a shared ordinal space. Right
    // after the version come two KEY TABLES (blocks, then items), each a
    // length-prefixed list of stable keys in the writer's ordinal order. Every
    // id in the body -- chunk bytes, inventory slot positions, machine types,
    // belt cargo, hotbar, armor, drops -- is read through the ContentMap those
    // tables build, so an id whose ordinal moved still lands on the right
    // content and one we no longer have becomes Air/None instead of whatever
    // now occupies its slot. This is what stops the block and item enums being
    // append-only forever, and it is the same negotiation a multiplayer client
    // will need on join, which is why it lives in ContentRegistry rather than
    // here. Pre-v22 saves get ContentMap::identity(): they were written by this
    // content set's own ancestor, so their ordinals are already ours.
    //
    // v23 appends a filter id to each BELT record. A tail append within the
    // record rather than at the end of the file, so it is read version-gated
    // exactly as v20's burnLeft and v21's fuel buffer were; kOldestLoadable
    // does not move and a pre-v23 belt loads unfiltered, which is what it was.
    //
    // v24 appends the master on/off switch to each MACHINE record, the same
    // shape again. A pre-v24 machine loads ENABLED, which is the only honest
    // default: every machine in every older save was built before a switch
    // existed, so all of them were running.
    //
    // v25 appends the crop growth timers at the END of the file, the v15/v16/
    // v18 shape. The crop BLOCKS ride the chunk data like any other block and
    // need nothing (v22's key tables already carry them); this is only how far
    // into its current stage each plant is. A pre-v25 save has no crops.
    constexpr std::uint32_t kOldestLoadable = 9;

    // The metadata sidecar (independent little format; see SlotMeta).
    constexpr std::uint32_t kMetaMagic = 0x4154454du;  // "META"
    constexpr std::uint32_t kMetaVersion = 1;

    template <typename T>
    void writePod(std::ofstream& out, const T& v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    template <typename T>
    bool readPod(std::ifstream& in, T& v) {
        in.read(reinterpret_cast<char*>(&v), sizeof(T));
        return in.good();
    }

    void writeInventory(std::ofstream& out, const Inventory& inv) {
        const std::uint32_t n = static_cast<std::uint32_t>(itemCount());
        writePod(out, n);
        for (std::uint32_t i = 0; i < n; ++i) {
            writePod(out, static_cast<std::int32_t>(inv.count(static_cast<ItemId>(i))));
        }
    }

    bool readInventory(std::ifstream& in, Inventory& inv,
                       const content::ContentMap& map) {
        std::uint32_t n = 0;
        if (!readPod(in, n)) return false;
        if (const std::size_t declared = map.foreignItemCount(); declared > 0) {
            // v22+: the writer told us exactly how many items it had, so any
            // other length is a truncated or corrupt record, not an old one.
            if (n != declared) return false;
        } else if (n > static_cast<std::uint32_t>(itemCount())) {
            // Pre-v22: entry i IS ItemId(i) because the enum only ever grew at
            // its tail, so a shorter run is an older file and the missing tail
            // defaults to zero. More slots than we know = a newer build = reject.
            return false;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            std::int32_t c = 0;
            if (!readPod(in, c) || c < 0) return false;
            // An item this build no longer has maps to None and its count is
            // dropped -- there is nothing to hold it in.
            if (c > 0) {
                if (const ItemId id = map.item(i); id != ItemId::None) inv.add(id, c);
            }
        }
        return true;
    }


    // Length-prefixed UTF-8. Only used for recipe keys, which are short by
    // construction, so the cap doubles as corruption detection.
    void writeString(std::ofstream& out, std::string_view s) {
        writePod(out, static_cast<std::uint32_t>(s.size()));
        if (!s.empty()) out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    bool readString(std::ifstream& in, std::string& s) {
        std::uint32_t n = 0;
        if (!readPod(in, n) || n > 256u) return false;
        s.assign(n, '\0');
        if (n > 0) in.read(s.data(), static_cast<std::streamsize>(n));
        return in.good();
    }

    // How a content set describes itself: a length-prefixed list of stable
    // keys, position = that side's ordinal. Written by save(), read by load(),
    // and the same shape a server would send a joining client.
    void writeKeyTable(std::ofstream& out, const std::vector<std::string>& keys) {
        writePod(out, static_cast<std::uint32_t>(keys.size()));
        for (const std::string& k : keys) writeString(out, k);
    }

    // A block/item ordinal on disk. v22 widened both enums past 255, so it also
    // widened the encoding; anything older wrote a single byte. Reads funnel
    // through here so that difference lives in exactly one place.
    void writeId(std::ofstream& out, std::uint32_t id) {
        writePod(out, static_cast<std::uint16_t>(id));
    }

    bool readId(std::ifstream& in, std::uint32_t version, std::uint32_t& id) {
        if (version >= 22) {
            std::uint16_t v = 0;
            if (!readPod(in, v)) return false;
            id = v;
            return true;
        }
        std::uint8_t v = 0;
        if (!readPod(in, v)) return false;
        id = v;
        return true;
    }

    bool readKeyTable(std::ifstream& in, std::vector<std::string>& out) {
        std::uint32_t n = 0;
        if (!readPod(in, n) || n > 65535u) return false;
        out.clear();
        out.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            std::string s;
            if (!readString(in, s) || s.empty()) return false;
            out.push_back(std::move(s));
        }
        return true;
    }

    // ---- Frozen v19 recipe order ----------------------------------------
    // Saves before v20 stored Machine::selectedRecipe as a POSITION in these
    // lists. This is a snapshot of that order taken when v20 landed, so an old
    // lock can be translated into the key it meant at the time. It is HISTORY,
    // not content: never edit it to track kMachineRecipes / kCircleRecipes.
    // A position naming a recipe that no longer exists resolves to AUTO.
    constexpr const char* kV19Grinder[] = {
        "grinder/ground-herb", "grinder/crystal-dust", "grinder/sand"};
    constexpr const char* kV19Composter[] = {
        "composter/dirt-from-sticks", "composter/dirt-from-sapling"};
    constexpr const char* kV19Cauldron[] = {
        "cauldron/herbal-tincture", "cauldron/mineral-solution"};
    constexpr const char* kV19Infuser[] = {
        "infuser/healing-draught", "infuser/mana-vial"};
    constexpr const char* kV19Alembic[] = {"alembic/elixir-of-vigor"};
    constexpr const char* kV19Distiller[] = {"distiller/refined-elixir"};
    constexpr const char* kV19Transmuter[] = {
        "transmuter/philosophers-catalyst", "transmuter/philosophers-stone"};
    constexpr const char* kV19Forge[] = {
        "forge/copper-helm", "forge/copper-chest", "forge/copper-boots",
        "forge/aegis-helm", "forge/aegis-chest", "forge/aegis-boots"};
    constexpr const char* kV19Press[] = {
        "press/copper-plate", "press/copper-rod", "press/gear",
        "press/machine-casing", "press/etched-plate", "press/machine-frame"};

    struct LegacyList {
        BlockId            machine;
        const char* const* keys;
        std::size_t        count;
    };

    constexpr LegacyList kV19Lists[] = {
        {BlockId::Grinder,    kV19Grinder,    std::size(kV19Grinder)},
        {BlockId::Composter,  kV19Composter,  std::size(kV19Composter)},
        {BlockId::Cauldron,   kV19Cauldron,   std::size(kV19Cauldron)},
        {BlockId::Infuser,    kV19Infuser,    std::size(kV19Infuser)},
        {BlockId::Alembic,    kV19Alembic,    std::size(kV19Alembic)},
        {BlockId::Distiller,  kV19Distiller,  std::size(kV19Distiller)},
        {BlockId::Transmuter, kV19Transmuter, std::size(kV19Transmuter)},
        {BlockId::Forge,      kV19Forge,      std::size(kV19Forge)},
        {BlockId::Press,      kV19Press,      std::size(kV19Press)},
    };

    constexpr const char* kV19Circle[] = {
        "circle/grinder", "circle/generator", "circle/composter",
        "circle/rain-barrel", "circle/wire", "circle/press",
        "circle/copper-axe", "circle/copper-pickaxe", "circle/copper-shovel",
        "circle/copper-sword", "circle/conduit", "circle/wrench",
        "circle/miner", "circle/distiller", "circle/transmuter",
        "circle/infuser", "circle/cauldron", "circle/forge", "circle/alembic",
        "circle/herb-source", "circle/crystal-source", "circle/copper-source",
        "circle/sand-source", "circle/essence-source",
        "circle/fusion-catalyst-from-resonance",
        "circle/philosophers-catalyst-from-resonance",
        "circle/teleport-key", "circle/storm-key",
        "circle/fusion-catalyst-from-stone",
    };

} // namespace

namespace SaveSystem {

    // Translate a pre-v20 index into the current runtime index, via the key it
    // named back then. Unknown -> -1 (AUTO), which is the honest answer: better
    // no lock than the wrong one.
    int legacyRecipeIndex(std::uint32_t version, BlockId machine, std::int32_t sel) {
        if (sel < 0) return -1;

        // v19 moved Ingot -> Copper Plate off the Grinder onto the Press, so a
        // v<19 index has to be walked forward into v19 order first. The Grinder
        // lost its row 0 (rows shift down; the plate lock itself has nowhere to
        // go), the Press gained one ahead of its rows.
        if (version < 19) {
            if (machine == BlockId::Grinder) sel = sel > 0 ? sel - 1 : -1;
            else if (machine == BlockId::Press) sel += 1;
            if (sel < 0) return -1;
        }

        const std::size_t at = static_cast<std::size_t>(sel);
        if (machineTraits(machine).kind == MachineKind::RuneCore) {
            return at < std::size(kV19Circle) ? circleIndexForKey(kV19Circle[at]) : -1;
        }
        for (const LegacyList& l : kV19Lists) {
            if (l.machine != machine) continue;
            return at < l.count ? recipeIndexForKey(machine, l.keys[at]) : -1;
        }
        return -1;
    }

bool save(const std::string& path, const SaveData& d) {
    // Write to a sibling temp file, then rotate it in (current file -> .bak),
    // so a crash or power loss mid-write can never destroy the only save.
    const std::string tmpPath = path + ".tmp";
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    writePod(out, kMagic);
    writePod(out, kVersion);

    // The content set this file's ids are relative to (v22). Written before
    // anything that uses an id, so the reader can build its translation before
    // it has to interpret a single byte of the body.
    writeKeyTable(out, content::blockKeyTable());
    writeKeyTable(out, content::itemKeyTable());

    writePod(out, d.worldSeed);
    writePod(out, d.sourceRng);
    writePod(out, d.camPos.x);
    writePod(out, d.camPos.y);
    writePod(out, d.camPos.z);
    writePod(out, d.camYaw);
    writePod(out, d.camPitch);
    writePod(out, static_cast<std::int32_t>(d.selectedSlot));

    writeInventory(out, d.inventory);

    // Chunks: coord + raw block bytes.
    writePod(out, static_cast<std::uint32_t>(d.world.chunks().size()));
    for (const auto& [coord, chunk] : d.world.chunks()) {
        writePod(out, static_cast<std::int32_t>(coord.x));
        writePod(out, static_cast<std::int32_t>(coord.y));
        writePod(out, static_cast<std::int32_t>(coord.z));
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    writeId(out, static_cast<std::uint32_t>(chunk->get(x, y, z)));
                }
            }
        }
    }

    // Machines.
    writePod(out, static_cast<std::uint32_t>(d.registries.machines.size()));
    for (const auto& [pos, m] : d.registries.machines) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writeId(out, static_cast<std::uint32_t>(m.type));
        // The lock travels as the recipe's KEY, so editing the recipe tables
        // can never repoint it (Recipes.h). Empty = AUTO.
        writeString(out, machineTraits(m.type).kind == MachineKind::RuneCore
                             ? circleKeyFor(m.selectedRecipe)
                             : recipeKeyFor(m.type, m.selectedRecipe));
        writePod(out, m.progress);
        writePod(out, m.burnLeft);
        writeInventory(out, m.input);
        writeInventory(out, m.output);
        writeInventory(out, m.fuel); // v21; empty for anything without a slot
        writePod(out, static_cast<std::uint8_t>(m.enabled ? 1 : 0)); // v24
    }

    // Belts.
    writePod(out, static_cast<std::uint32_t>(d.registries.belts.size()));
    for (const auto& [pos, b] : d.registries.belts) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, static_cast<std::int8_t>(b.facing.x));
        writePod(out, static_cast<std::int8_t>(b.facing.y));
        writePod(out, static_cast<std::int8_t>(b.facing.z));
        writeId(out, static_cast<std::uint32_t>(b.item));
        writeId(out, static_cast<std::uint32_t>(b.filter)); // v23
    }

    // Sources.
    writePod(out, static_cast<std::uint32_t>(d.registries.sources.size()));
    for (const auto& [pos, timer] : d.registries.sources) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, timer);
    }

    // Saplings.
    writePod(out, static_cast<std::uint32_t>(d.registries.saplings.size()));
    for (const auto& [pos, timer] : d.registries.saplings) {
        writePod(out, static_cast<std::int32_t>(pos.x));
        writePod(out, static_cast<std::int32_t>(pos.y));
        writePod(out, static_cast<std::int32_t>(pos.z));
        writePod(out, timer);
    }

    // Weather.
    writePod(out, static_cast<std::uint8_t>(d.weather.raining ? 1 : 0));
    writePod(out, d.weather.timer);
    writePod(out, d.bucketFill);

    // Player health (appended in v10).
    writePod(out, d.player.health);

    // Hotbar slot assignments (appended in v12); 0 = ItemId::None = empty.
    for (const ItemId id : d.hotbar) {
        writeId(out, static_cast<std::uint32_t>(id));
    }

    // Boss progression (appended in v13; each new boss appends its own, v14).
    writePod(out, static_cast<std::uint8_t>(d.bossDefeated ? 1 : 0));
    writePod(out, static_cast<std::uint8_t>(d.tempestDefeated ? 1 : 0));

    // Total active playtime in seconds (appended in v15).
    writePod(out, d.playtime);

    // Ground items (appended in v16). Only Overworld drops are persisted (the
    // arena is transient); position + id + count is enough — velocity/settle
    // rebuild on load.
    std::uint32_t dropCount = 0;
    for (const DroppedItem& dr : d.drops) {
        if (dr.dim == DimensionId::Overworld && dr.id != ItemId::None && dr.count > 0) {
            ++dropCount;
        }
    }
    writePod(out, dropCount);
    for (const DroppedItem& dr : d.drops) {
        if (dr.dim != DimensionId::Overworld || dr.id == ItemId::None || dr.count <= 0) {
            continue;
        }
        writePod(out, dr.pos.x);
        writePod(out, dr.pos.y);
        writePod(out, dr.pos.z);
        writeId(out, static_cast<std::uint32_t>(dr.id));
        writePod(out, static_cast<std::int32_t>(dr.count));
    }

    // Equipped armor (appended in v18); 0 = ItemId::None = empty slot.
    for (const ItemId id : d.armor) {
        writeId(out, static_cast<std::uint32_t>(id));
    }

    // Crop growth timers (appended in v25). A tail append like v15/v16/v18, so
    // kOldestLoadable does not move and a pre-v25 save loads with an empty
    // field -- which is what it had. The BLOCKS are already in the chunk data;
    // this is only how far into its current stage each one is.
    writePod(out, static_cast<std::uint32_t>(d.registries.crops.size()));
    for (const auto& [pos, timer] : d.registries.crops) {
        writePod(out, pos.x);
        writePod(out, pos.y);
        writePod(out, pos.z);
        writePod(out, timer);
    }

    out.close();
    if (!out.good()) return false;

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, path + ".bak", ec); // replaces any old .bak
        if (ec) return false;
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) return false;

    // Refresh the sidecar so the slot picker can list this slot cheaply. This
    // is best-effort: a failed meta write never fails the save (the picker
    // falls back to the file's timestamp).
    SlotMeta meta;
    meta.saveVersion = kVersion;
    meta.unixTime = static_cast<std::uint64_t>(std::time(nullptr));
    meta.playtimeSeconds = static_cast<std::uint32_t>(
        d.playtime > 0.0 ? d.playtime : 0.0);
    meta.bossProgress = static_cast<std::uint8_t>((d.bossDefeated ? 1 : 0) |
                                                  (d.tempestDefeated ? 2 : 0));
    if (std::ofstream mout(SaveSystem::metaPath(path), std::ios::binary | std::ios::trunc);
        mout) {
        writePod(mout, kMetaMagic);
        writePod(mout, kMetaVersion);
        writePod(mout, meta.saveVersion);
        writePod(mout, meta.unixTime);
        writePod(mout, meta.playtimeSeconds);
        writePod(mout, meta.bossProgress);
    }
    return true;
}

bool load(const std::string& path, SaveData& d) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::uint32_t magic = 0, version = 0;
    if (!readPod(in, magic) || magic != kMagic) return false;
    if (!readPod(in, version) || version < kOldestLoadable || version > kVersion) {
        return false;
    }

    // Build the id translation before reading anything that carries an id.
    // Pre-v22 files predate the key tables, but they were written by an
    // ancestor of this very content set, so their ordinals are ours already.
    content::ContentMap map = content::ContentMap::identity();
    if (version >= 22) {
        std::vector<std::string> blockKeys, itemKeys;
        if (!readKeyTable(in, blockKeys) || !readKeyTable(in, itemKeys)) return false;
        map = content::ContentMap::build(blockKeys, itemKeys);
        // Refuse a save naming content we do not have, up front and before a
        // single id is interpreted. The alternative -- loading it with holes
        // where the missing blocks were -- silently eats a factory, and a
        // player who removed a mod would rather be told than shown the damage.
        // (The nicer answer later is a placeholder block that round-trips its
        // key so re-adding the mod restores the world; that needs a real
        // ordinal for content we cannot describe, which is Layer 1's job.)
        if (!map.missing().empty()) return false;
    }

    if (!readPod(in, d.worldSeed)) return false;
    if (!readPod(in, d.sourceRng)) return false;
    if (!readPod(in, d.camPos.x) || !readPod(in, d.camPos.y) || !readPod(in, d.camPos.z)) return false;
    if (!readPod(in, d.camYaw) || !readPod(in, d.camPitch)) return false;
    std::int32_t slot = 0;
    if (!readPod(in, slot) || slot < 0) return false;
    d.selectedSlot = slot;

    if (!readInventory(in, d.inventory, map)) return false;

    // Chunks. A block byte is an ordinal in the WRITER's content set, so what
    // bounds it is that set's size, not ours.
    const std::uint32_t blockLimit =
        map.foreignBlockCount() > 0 ? static_cast<std::uint32_t>(map.foreignBlockCount())
                                    : static_cast<std::uint32_t>(blockCount());
    const std::uint32_t itemLimit =
        map.foreignItemCount() > 0 ? static_cast<std::uint32_t>(map.foreignItemCount())
                                   : static_cast<std::uint32_t>(itemCount());
    std::uint32_t chunkCount = 0;
    if (!readPod(in, chunkCount) || chunkCount > 4096u) return false;
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        std::int32_t cx = 0, cy = 0, cz = 0;
        if (!readPod(in, cx) || !readPod(in, cy) || !readPod(in, cz)) return false;
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    std::uint32_t b = 0;
                    if (!readId(in, version, b) || b >= blockLimit) return false;
                    d.world.setBlock(cx * CHUNK_SIZE + x, cy * CHUNK_SIZE + y,
                                     cz * CHUNK_SIZE + z, map.block(b));
                }
            }
        }
    }

    // Machines.
    std::uint32_t machineCount = 0;
    if (!readPod(in, machineCount) || machineCount > 100000u) return false;
    for (std::uint32_t i = 0; i < machineCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        std::uint32_t type = 0;
        Machine m;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readId(in, version, type) || type >= blockLimit) return false;
        m.type = map.block(type);
        if (!isMachine(m.type)) return false;
        if (version >= 20) {
            // The lock is a key. A key that no longer names a recipe -- because
            // it was renamed or deleted -- lands on AUTO instead of on whatever
            // row happens to sit at some index today.
            std::string key;
            if (!readString(in, key)) return false;
            m.selectedRecipe = machineTraits(m.type).kind == MachineKind::RuneCore
                                   ? circleIndexForKey(key)
                                   : recipeIndexForKey(m.type, key);
        } else {
            std::int32_t sel = -1;
            if (!readPod(in, sel)) return false;
            m.selectedRecipe = legacyRecipeIndex(version, m.type, sel);
        }
        if (!readPod(in, m.progress)) return false;
        // burnLeft appended in v20; older saves relight on the next craft.
        if (version >= 20 && !readPod(in, m.burnLeft)) return false;
        if (!readInventory(in, m.input, map) || !readInventory(in, m.output, map)) return false;
        if (version >= 21) {
            if (!readInventory(in, m.fuel, map)) return false;
        } else if (usesFuelSlot(m.type)) {
            // Pre-v21 kept fuel and ingredients in one buffer, so an existing
            // Furnace's charcoal is sitting in `input`. Sweep it across using
            // the rule the old pickFuel used to apply every tick -- fuel that
            // is ALSO an ingredient here was feedstock and stays put. Applied
            // once, at the boundary, and then the rule is retired for good.
            for (const FuelInfo& f : fuelRows()) {
                const int held = m.input.count(f.item);
                if (held <= 0) continue;
                bool ingredient = false;
                for (const MachineRecipe* r : recipesForMachine(m.type)) {
                    for (const ItemStack& ri : r->inputs) {
                        if (ri.id == f.item) { ingredient = true; break; }
                    }
                    if (ingredient) break;
                }
                if (ingredient) continue;
                m.input.remove(f.item, held);
                m.fuel.add(f.item, held);
            }
        }
        // The master switch, appended in v24. Anything older predates the
        // switch entirely, so every machine in it was running -- and `enabled`
        // already defaults true, which is why there is nothing to migrate.
        if (version >= 24) {
            std::uint8_t on = 1;
            if (!readPod(in, on)) return false;
            m.enabled = on != 0;
        }
        d.registries.machines[{x, y, z}] = std::move(m);
    }

    // Belts.
    std::uint32_t beltCount = 0;
    if (!readPod(in, beltCount) || beltCount > 1000000u) return false;
    for (std::uint32_t i = 0; i < beltCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        std::int8_t fx = 0, fy = 0, fz = 0;
        std::uint32_t item = 0;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, fx) || !readPod(in, fy) || !readPod(in, fz)) return false;
        if (!readId(in, version, item) || item >= itemLimit) return false;
        Belt b;
        b.facing = {fx, fy, fz};
        b.item = map.item(item);
        // v23 appended the filter. A pre-v23 belt carried anything, which is
        // exactly what ItemId::None means, so there is nothing to migrate.
        if (version >= 23) {
            std::uint32_t filter = 0;
            if (!readId(in, version, filter) || filter >= itemLimit) return false;
            b.filter = map.item(filter);
        }
        d.registries.belts[{x, y, z}] = b;
    }

    // Sources.
    std::uint32_t sourceCount = 0;
    if (!readPod(in, sourceCount) || sourceCount > 100000u) return false;
    for (std::uint32_t i = 0; i < sourceCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        float timer = 0.0f;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, timer)) return false;
        d.registries.sources[{x, y, z}] = timer;
    }

    // Saplings.
    std::uint32_t saplingCount = 0;
    if (!readPod(in, saplingCount) || saplingCount > 100000u) return false;
    for (std::uint32_t i = 0; i < saplingCount; ++i) {
        std::int32_t x = 0, y = 0, z = 0;
        float timer = 0.0f;
        if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
        if (!readPod(in, timer)) return false;
        d.registries.saplings[{x, y, z}] = timer;
    }

    // Weather.
    std::uint8_t raining = 0;
    if (!readPod(in, raining)) return false;
    d.weather.raining = raining != 0;
    if (!readPod(in, d.weather.timer)) return false;
    if (!readPod(in, d.bucketFill)) return false;

    // Player health: appended in v10; older saves keep the caller's default.
    if (version >= 10 && !readPod(in, d.player.health)) return false;

    // Hotbar slots: appended in v12; older saves keep the caller's default.
    if (version >= 12) {
        for (ItemId& cell : d.hotbar) {
            std::uint32_t v = 0;
            if (!readId(in, version, v) || v >= itemLimit) return false;
            cell = map.item(v);
        }
    }

    // Boss progression: appended per boss (v13, v14); older saves keep the
    // defaults (false).
    if (version >= 13) {
        std::uint8_t defeated = 0;
        if (!readPod(in, defeated)) return false;
        d.bossDefeated = defeated != 0;
    }
    if (version >= 14) {
        std::uint8_t defeated = 0;
        if (!readPod(in, defeated)) return false;
        d.tempestDefeated = defeated != 0;
    }

    // Playtime: appended in v15; older saves keep the caller's default (0).
    if (version >= 15 && !readPod(in, d.playtime)) return false;

    // Ground items: appended in v16; older saves leave the list empty. Loaded
    // drops start settled in the Overworld (velocity/settle aren't persisted).
    d.drops.clear();
    if (version >= 16) {
        std::uint32_t dropCount = 0;
        if (!readPod(in, dropCount) || dropCount > 1000000u) return false;
        d.drops.reserve(dropCount);
        for (std::uint32_t i = 0; i < dropCount; ++i) {
            DroppedItem dr;
            if (!readPod(in, dr.pos.x) || !readPod(in, dr.pos.y) || !readPod(in, dr.pos.z)) {
                return false;
            }
            std::uint32_t id = 0;
            std::int32_t c = 0;
            if (!readId(in, version, id) || id >= itemLimit) return false;
            if (!readPod(in, c) || c <= 0) return false;
            dr.id = map.item(id);
            dr.count = c;
            dr.dim = DimensionId::Overworld;
            dr.settled = true;
            d.drops.push_back(dr);
        }
    }

    // Equipped armor: appended in v18; older saves leave the caller's default
    // (all None). A worn piece must still be a valid item id.
    if (version >= 18) {
        for (ItemId& cell : d.armor) {
            std::uint32_t v = 0;
            if (!readId(in, version, v) || v >= itemLimit) return false;
            cell = map.item(v);
        }
    }

    // Crop timers: appended in v25; older saves have no crops, which is
    // exactly what they had. The stage itself came back with the chunk data,
    // so a missing timer only costs a plant its progress toward the next one.
    d.registries.crops.clear();
    if (version >= 25) {
        std::uint32_t cropCount = 0;
        if (!readPod(in, cropCount) || cropCount > 1000000u) return false;
        for (std::uint32_t i = 0; i < cropCount; ++i) {
            std::int32_t x = 0, y = 0, z = 0;
            float timer = 0.0f;
            if (!readPod(in, x) || !readPod(in, y) || !readPod(in, z)) return false;
            if (!readPod(in, timer)) return false;
            d.registries.crops[{x, y, z}] = timer;
        }
    }

    return true;
}

std::string metaPath(const std::string& savePath) {
    return savePath + ".meta";
}

bool readMeta(const std::string& savePath, SlotMeta& out) {
    std::ifstream in(metaPath(savePath), std::ios::binary);
    if (!in) return false;
    std::uint32_t magic = 0, metaVersion = 0;
    if (!readPod(in, magic) || magic != kMetaMagic) return false;
    if (!readPod(in, metaVersion) || metaVersion != kMetaVersion) return false;
    SlotMeta m;
    if (!readPod(in, m.saveVersion) || !readPod(in, m.unixTime) ||
        !readPod(in, m.playtimeSeconds) || !readPod(in, m.bossProgress)) {
        return false;
    }
    out = m;
    return true;
}

} // namespace SaveSystem
