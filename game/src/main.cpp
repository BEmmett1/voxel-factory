// We run our own main(), so tell SDL not to hijack it.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>

#include "game/VoxelGame.h"
#include "game/AlchemyCircle.h"
#include "game/ContentValidate.h"
#include "game/MachineSystem.h"
#include "game/Recipes.h"
#include "game/SaveSystem.h"
#include "game/Settings.h"
#include "game/World.h"
#include "VoxelGameInternal.h" // vg::kOrgName / kAppName

#include "engine/CrashHandler.h"
#include "engine/Log.h"
#include "engine/Paths.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

#define SELFTEST_CHECK(cond)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "selftest FAILED: %s (main.cpp:%d)\n", #cond, \
                         __LINE__);                                            \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// Rewrite a v22 save's block (table 0) or item (table 1) key table with two
// keys swapped, WITHOUT touching the body. That fabricates something this
// build cannot otherwise produce: a file whose ordinals are not ours -- the
// same disagreement a save from a modded build, or a server we joined, hands
// us. Every id in the body now names the other block, so a load that honours
// the key table must come back mirrored and a load that ignores it comes back
// unchanged. The swap keeps the region's byte length identical (same strings,
// different order), so nothing after it moves.
bool swapKeysInSave(const std::string& path, int table,
                    const std::string& a, const std::string& b) {
    std::vector<char> buf;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::size_t p = 8; // magic + version
    auto readU32 = [&](std::uint32_t& v) {
        if (p + 4 > buf.size()) return false;
        std::memcpy(&v, buf.data() + p, 4);
        p += 4;
        return true;
    };
    // Walk (and skip) the tables ahead of the one we want.
    for (int t = 0; t <= table; ++t) {
        std::uint32_t n = 0;
        if (!readU32(n)) return false;
        const std::size_t start = p;
        std::vector<std::string> keys;
        keys.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint32_t len = 0;
            if (!readU32(len) || p + len > buf.size()) return false;
            keys.emplace_back(buf.data() + p, len);
            p += len;
        }
        if (t != table) continue;

        const auto ia = std::find(keys.begin(), keys.end(), a);
        const auto ib = std::find(keys.begin(), keys.end(), b);
        if (ia == keys.end() || ib == keys.end()) return false;
        std::iter_swap(ia, ib);

        std::vector<char> region;
        for (const std::string& k : keys) {
            const auto len = static_cast<std::uint32_t>(k.size());
            const char* lp = reinterpret_cast<const char*>(&len);
            region.insert(region.end(), lp, lp + 4);
            region.insert(region.end(), k.begin(), k.end());
        }
        if (region.size() != p - start) return false; // must not shift the body
        std::copy(region.begin(), region.end(), buf.begin() + static_cast<std::ptrdiff_t>(start));
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return out.good();
}

// Rename one key in place, simulating a save that names content this build
// does not have (the "you removed a mod" case). `to` must be the same length
// as `from` so nothing after the table moves -- the point of the fixture is the
// unknown key, not exercising the parser's offset arithmetic twice.
bool renameKeyInSave(const std::string& path, const std::string& from,
                     const std::string& to) {
    if (from.size() != to.size()) return false;
    std::vector<char> buf;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto at = std::search(buf.begin(), buf.end(), from.begin(), from.end());
    if (at == buf.end()) return false;
    std::copy(to.begin(), to.end(), at);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return out.good();
}

// --selftest: a SaveSystem round-trip with no window/GL, so CI can run it
// headless. Builds a small but representative SaveData, saves, loads into
// fresh state, and compares; then checks the .bak rotation and that a
// truncated file is rejected. Returns a process exit code.
int runSelfTest() {
    namespace fs = std::filesystem;
    const std::string path =
        (fs::temp_directory_path() / "voxel-factory-selftest.vxf").string();
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path + ".bak", ec);

    // Blocks across three chunks (including negative coords), one loaded
    // machine, one loaded belt, growth timers, weather, camera, seed.
    World world;
    world.setBlock(1, 2, 3, BlockId::Grass);
    world.setBlock(17, 2, 3, BlockId::Log);
    world.setBlock(-5, 0, -5, BlockId::Scaffold);

    Inventory inv;
    inv.add(ItemId::CopperIngot, 7);
    inv.add(ItemId::Herb, 2);

    std::unordered_map<glm::ivec3, Machine, IVec3Hash> machines;
    Machine grinder;
    grinder.type = BlockId::Grinder;
    grinder.selectedRecipe = 1;
    grinder.progress = 0.75f;
    grinder.input.add(ItemId::Herb, 3);
    grinder.output.add(ItemId::GroundHerb, 2);
    machines[glm::ivec3{1, 3, 3}] = grinder;

    // A furnace exercises the v21 fuel buffer: wood in IN is FEEDSTOCK (it
    // chars into charcoal) while charcoal in FUEL is the fire. The two must
    // stay in their own buffers across a round-trip, or a load would hand a
    // furnace its own product to burn.
    Machine furnace;
    furnace.type = BlockId::Furnace;
    furnace.burnLeft = 12.5f;
    furnace.input.add(ItemId::Wood, 5);
    furnace.fuel.add(ItemId::Charcoal, 2);
    machines[glm::ivec3{9, 3, 3}] = furnace;

    std::unordered_map<glm::ivec3, Belt, IVec3Hash> belts;
    belts[glm::ivec3{2, 3, 3}] = Belt{glm::ivec3{-1, 0, 0}, ItemId::GroundHerb};

    std::unordered_map<glm::ivec3, float, IVec3Hash> sources;
    sources[glm::ivec3{4, 2, 4}] = 3.5f;
    std::unordered_map<glm::ivec3, float, IVec3Hash> saplings;
    saplings[glm::ivec3{6, 2, 6}] = 9.0f;

    Weather weather;
    weather.raining = true;
    weather.timer = 42.0f;
    PlayerController player;
    player.health = 6.5f;
    float bucketFill = 0.25f;
    glm::vec3 camPos{8.0f, 20.0f, 8.0f};
    float yaw = -90.0f, pitch = -15.0f;
    std::uint32_t seed = 1234u, rngState = 5678u;
    int slot = 4;
    bool bossDefeated = true; // save a beaten warden; must round-trip
    bool tempestDefeated = false; // the tempest still stands (mixed flags)
    double playtime = 3672.0; // 1h 01m 12s; must round-trip (v15)
    // A mixed hotbar: a tool, a gap, and a placeable among defaults.
    std::array<ItemId, kHotbarSlots> hotbar{};
    hotbar[0] = ItemId::CopperSword;
    hotbar[1] = ItemId::Conduit;
    hotbar[9] = ItemId::ScaffoldItem; // slots 2-8 stay None

    // A settled ground item (v16) must round-trip.
    std::vector<DroppedItem> drops;
    {
        DroppedItem dr;
        dr.pos = {2.5f, 15.0f, 3.5f};
        dr.id = ItemId::Crystal;
        dr.count = 4;
        dr.settled = true;
        drops.push_back(dr);
    }

    // Equipped armor (v18): a mixed set (head + body worn, feet empty).
    std::array<ItemId, kArmorSlots> armor{ItemId::CopperHelm, ItemId::AegisChest,
                                          ItemId::None};

    SaveData src{world, inv, {machines, belts, sources, saplings},
                 weather, player, bucketFill,
                 camPos, yaw, pitch, seed, rngState, slot, hotbar, bossDefeated,
                 tempestDefeated, playtime, drops, armor};
    SELFTEST_CHECK(SaveSystem::save(path, src));

    World world2;
    Inventory inv2;
    std::unordered_map<glm::ivec3, Machine, IVec3Hash> machines2;
    std::unordered_map<glm::ivec3, Belt, IVec3Hash> belts2;
    std::unordered_map<glm::ivec3, float, IVec3Hash> sources2, saplings2;
    Weather weather2; // defaults: clear sky, fresh timer
    PlayerController player2;
    float bucketFill2 = 0.0f;
    glm::vec3 camPos2{0.0f};
    float yaw2 = 0.0f, pitch2 = 0.0f;
    std::uint32_t seed2 = 0u, rngState2 = 0u;
    int slot2 = 0;
    bool bossDefeated2 = false;
    bool tempestDefeated2 = true; // pre-set to prove the load overwrites it
    double playtime2 = 99.0;      // pre-set to prove the load overwrites it
    // Pre-filled with a different pattern to prove the load overwrites it.
    std::array<ItemId, kHotbarSlots> hotbar2;
    hotbar2.fill(ItemId::Wood);
    std::vector<DroppedItem> drops2; // pre-empty; the load fills it
    // Pre-filled with a different pattern to prove the load overwrites it.
    std::array<ItemId, kArmorSlots> armor2;
    armor2.fill(ItemId::Wood);
    SaveData dst{world2, inv2, {machines2, belts2, sources2, saplings2},
                 weather2, player2, bucketFill2,
                 camPos2, yaw2, pitch2, seed2, rngState2, slot2, hotbar2, bossDefeated2,
                 tempestDefeated2, playtime2, drops2, armor2};
    SELFTEST_CHECK(SaveSystem::load(path, dst));

    SELFTEST_CHECK(world2.chunks().size() == world.chunks().size());
    SELFTEST_CHECK(world2.getBlock(1, 2, 3) == BlockId::Grass);
    SELFTEST_CHECK(world2.getBlock(17, 2, 3) == BlockId::Log);
    SELFTEST_CHECK(world2.getBlock(-5, 0, -5) == BlockId::Scaffold);
    SELFTEST_CHECK(world2.getBlock(0, 0, 0) == BlockId::Air);

    SELFTEST_CHECK(inv2.count(ItemId::CopperIngot) == 7);
    SELFTEST_CHECK(inv2.count(ItemId::Herb) == 2);

    SELFTEST_CHECK(machines2.size() == 2);
    const Machine& m2 = machines2.at(glm::ivec3{1, 3, 3});
    SELFTEST_CHECK(m2.type == BlockId::Grinder);
    SELFTEST_CHECK(m2.selectedRecipe == 1);
    SELFTEST_CHECK(m2.progress == 0.75f);
    SELFTEST_CHECK(m2.input.count(ItemId::Herb) == 3);
    SELFTEST_CHECK(m2.output.count(ItemId::GroundHerb) == 2);

    const Machine& f2 = machines2.at(glm::ivec3{9, 3, 3});
    SELFTEST_CHECK(f2.type == BlockId::Furnace);
    SELFTEST_CHECK(f2.burnLeft == 12.5f);
    SELFTEST_CHECK(f2.input.count(ItemId::Wood) == 5);   // feedstock, still IN
    SELFTEST_CHECK(f2.fuel.count(ItemId::Charcoal) == 2); // the fire, still FUEL
    SELFTEST_CHECK(f2.input.count(ItemId::Charcoal) == 0);
    SELFTEST_CHECK(f2.fuel.count(ItemId::Wood) == 0);
    // The buffer a burner draws from, and the split a belt has to infer.
    SELFTEST_CHECK(&MachineSystem::fuelBuffer(f2) == &f2.fuel);
    SELFTEST_CHECK(usesFuelSlot(BlockId::Furnace));
    SELFTEST_CHECK(usesFuelSlot(BlockId::Bloomery));
    SELFTEST_CHECK(!usesFuelSlot(BlockId::Generator)); // no recipes, no ambiguity
    {
        Machine fb;
        fb.type = BlockId::Furnace;
        // Charcoal is nothing the furnace makes anything FROM, so it is fuel;
        // wood is what charcoal is made OF, so a belt must treat it as input.
        SELFTEST_CHECK(&MachineSystem::bufferFor(fb, ItemId::Charcoal) == &fb.fuel);
        SELFTEST_CHECK(&MachineSystem::bufferFor(fb, ItemId::Wood) == &fb.input);
        Machine gen;
        gen.type = BlockId::Generator;
        SELFTEST_CHECK(&MachineSystem::bufferFor(gen, ItemId::Wood) == &gen.input);
    }

    SELFTEST_CHECK(belts2.size() == 1);
    const Belt& b2 = belts2.at(glm::ivec3{2, 3, 3});
    SELFTEST_CHECK(b2.facing == glm::ivec3(-1, 0, 0));
    SELFTEST_CHECK(b2.item == ItemId::GroundHerb);

    SELFTEST_CHECK(sources2.size() == 1 && sources2.at(glm::ivec3{4, 2, 4}) == 3.5f);
    SELFTEST_CHECK(saplings2.size() == 1 && saplings2.at(glm::ivec3{6, 2, 6}) == 9.0f);

    SELFTEST_CHECK(weather2.raining == true);
    SELFTEST_CHECK(weather2.timer == 42.0f && bucketFill2 == 0.25f);
    SELFTEST_CHECK(camPos2 == camPos && yaw2 == yaw && pitch2 == pitch);
    SELFTEST_CHECK(seed2 == seed && rngState2 == rngState);
    SELFTEST_CHECK(slot2 == 4);
    SELFTEST_CHECK(player2.health == 6.5f);
    for (int i = 0; i < kHotbarSlots; ++i) {
        SELFTEST_CHECK(hotbar2[i] == hotbar[i]);
    }
    SELFTEST_CHECK(bossDefeated2 == true);   // the v13 trailing flag round-trips
    SELFTEST_CHECK(tempestDefeated2 == false); // v14 flag round-trips (mixed)
    SELFTEST_CHECK(playtime2 == playtime);   // the v15 playtime double round-trips
    SELFTEST_CHECK(drops2.size() == 1);      // the v16 ground item round-trips
    SELFTEST_CHECK(drops2[0].id == ItemId::Crystal && drops2[0].count == 4);
    SELFTEST_CHECK(drops2[0].pos == glm::vec3(2.5f, 15.0f, 3.5f));
    SELFTEST_CHECK(armor2[0] == ItemId::CopperHelm);  // the v18 armor round-trips
    SELFTEST_CHECK(armor2[1] == ItemId::AegisChest);
    SELFTEST_CHECK(armor2[2] == ItemId::None);

    // The metadata sidecar the picker reads without loading the full save.
    SlotMeta meta;
    SELFTEST_CHECK(SaveSystem::readMeta(path, meta));
    SELFTEST_CHECK(meta.playtimeSeconds == 3672u);
    SELFTEST_CHECK(meta.bossProgress == 1u); // warden set, tempest clear -> bit0
    SELFTEST_CHECK(fs::exists(SaveSystem::metaPath(path)));

    // Distinct slot paths stay independent (the multi-slot guarantee).
    const std::string slotA = path + ".slotA";
    const std::string slotB = path + ".slotB";
    fs::remove(slotA, ec);
    fs::remove(slotB, ec);
    playtime = 10.0;
    SELFTEST_CHECK(SaveSystem::save(slotA, src));
    playtime = 20.0;
    SELFTEST_CHECK(SaveSystem::save(slotB, src));
    SlotMeta metaA, metaB;
    SELFTEST_CHECK(SaveSystem::readMeta(slotA, metaA) && metaA.playtimeSeconds == 10u);
    SELFTEST_CHECK(SaveSystem::readMeta(slotB, metaB) && metaB.playtimeSeconds == 20u);
    SELFTEST_CHECK(!SaveSystem::readMeta(path + ".nope", metaA)); // absent slot
    for (const std::string& p : {slotA, slotB}) {
        fs::remove(p, ec);
        fs::remove(p + ".bak", ec);
        fs::remove(SaveSystem::metaPath(p), ec);
    }

    // A second save rotates the first file to .bak; no .tmp is left behind.
    SELFTEST_CHECK(SaveSystem::save(path, src));
    SELFTEST_CHECK(fs::exists(path + ".bak"));
    SELFTEST_CHECK(!fs::exists(path + ".tmp"));

    // A truncated file must be rejected, not misread.
    const auto full = fs::file_size(path);
    const std::string cut = path + ".cut";
    {
        std::ifstream in(path, std::ios::binary);
        std::ofstream out(cut, std::ios::binary | std::ios::trunc);
        std::string bytes(static_cast<std::size_t>(full) / 2, '\0');
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    World world3;
    std::unordered_map<glm::ivec3, Machine, IVec3Hash> machines3;
    std::unordered_map<glm::ivec3, Belt, IVec3Hash> belts3;
    std::unordered_map<glm::ivec3, float, IVec3Hash> sources3, saplings3;
    std::vector<DroppedItem> drops3;
    std::array<ItemId, kArmorSlots> armor3{};
    SaveData cutDst{world3, inv2, {machines3, belts3, sources3, saplings3},
                    weather2, player2, bucketFill2,
                    camPos2, yaw2, pitch2, seed2, rngState2, slot2, hotbar2, bossDefeated2,
                    tempestDefeated2, playtime2, drops3, armor3};
    SELFTEST_CHECK(!SaveSystem::load(cut, cutDst));

    fs::remove(path, ec);
    fs::remove(path + ".bak", ec);
    fs::remove(SaveSystem::metaPath(path), ec);
    fs::remove(cut, ec);

    // Settings: cfg round-trip, rotation, and parse tolerance (headless).
    const std::string cfg =
        (fs::temp_directory_path() / "voxel-factory-selftest.cfg").string();
    fs::remove(cfg, ec);
    fs::remove(cfg + ".bak", ec);

    Settings s;
    SELFTEST_CHECK(!SettingsIO::load(cfg, s)); // missing file = false, defaults kept
    s.fullscreen = true;
    s.vsync = false;
    s.sensitivity = 0.20f;
    s.volume = 0.5f;
    s.binds[static_cast<int>(Action::Jump)] = SDL_SCANCODE_J;
    // Rebinding W to Sprint leaves MoveForward EXPLICITLY unbound — the
    // round-trip must preserve that, not resurrect the default (which the
    // duplicate pass would then strip from the wrong action).
    s.binds[static_cast<int>(Action::MoveForward)] = SDL_SCANCODE_UNKNOWN;
    s.binds[static_cast<int>(Action::Sprint)] = SDL_SCANCODE_W;
    SELFTEST_CHECK(SettingsIO::save(cfg, s));

    Settings t;
    SELFTEST_CHECK(SettingsIO::load(cfg, t));
    SELFTEST_CHECK(t.fullscreen && !t.vsync);
    SELFTEST_CHECK(t.sensitivity == 0.20f && t.volume == 0.5f);
    SELFTEST_CHECK(t.key(Action::Jump) == SDL_SCANCODE_J);
    SELFTEST_CHECK(t.key(Action::MoveForward) == SDL_SCANCODE_UNKNOWN);
    SELFTEST_CHECK(t.key(Action::Sprint) == SDL_SCANCODE_W);

    // A second save rotates .bak and leaves no .tmp (the SaveSystem contract).
    SELFTEST_CHECK(SettingsIO::save(cfg, s));
    SELFTEST_CHECK(fs::exists(cfg + ".bak"));
    SELFTEST_CHECK(!fs::exists(cfg + ".tmp"));

    // Tolerance: junk lines and unknown keys are skipped; out-of-range floats
    // clamp; bad numbers and reserved scancodes keep their defaults.
    {
        std::ofstream bad(cfg, std::ios::trunc);
        bad << "# comment\nGARBAGE\nNEWKEY=5\nSENSITIVITY=99\nVOLUME=abc\n"
            << "BIND_JUMP=41\nVSYNC=0\n"; // 41 = SDL_SCANCODE_ESCAPE (reserved)
    }
    Settings u;
    SELFTEST_CHECK(SettingsIO::load(cfg, u));
    SELFTEST_CHECK(u.sensitivity == kSensitivityMax);          // clamped
    SELFTEST_CHECK(u.volume == 0.8f);                          // bad value -> default
    SELFTEST_CHECK(u.key(Action::Jump) == SDL_SCANCODE_SPACE); // reserved -> default
    SELFTEST_CHECK(!u.vsync);

    fs::remove(cfg, ec);
    fs::remove(cfg + ".bak", ec);

    // ---- Content ids travel by key, not by ordinal (v22) -----------------
    // The ordinal of a block or item is an encoding relative to the content set
    // that wrote it, so a save has to say what its numbers MEAN. This is the
    // check that the load path actually reads the key tables instead of
    // trusting the raw bytes: swap two keys in the file and the world must come
    // back mirrored. If it comes back unchanged, ids are being taken at face
    // value and the whole layer is decorative.
    //
    // It is also the cheapest proxy for the multiplayer case the same mechanism
    // exists to serve, where a client's ordinals legitimately differ from the
    // ones it compiled with.
    {
        const std::string p =
            (fs::temp_directory_path() / "voxel-factory-selftest-ids.vxf").string();
        fs::remove(p, ec);
        fs::remove(p + ".bak", ec);

        World w;
        w.setBlock(0, 1, 0, BlockId::Stone);
        w.setBlock(1, 1, 0, BlockId::Scaffold);
        w.setBlock(2, 1, 0, BlockId::Grass); // a control: must NOT move
        Inventory iv;
        iv.add(ItemId::CopperIngot, 3);
        iv.add(ItemId::Charcoal, 9);

        std::unordered_map<glm::ivec3, Machine, IVec3Hash> ms;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> bs;
        std::unordered_map<glm::ivec3, float, IVec3Hash> sr, sp;
        Weather wt;
        PlayerController pl;
        float bf = 0.0f;
        glm::vec3 cp{0.0f};
        float yw = 0.0f, pt = 0.0f;
        std::uint32_t sd = 1u, rng = 1u;
        int sl = 0;
        std::array<ItemId, kHotbarSlots> hb{};
        hb[0] = ItemId::Charcoal; // the hotbar is an id site too
        bool bd = false, td = false;
        double play = 0.0;
        std::vector<DroppedItem> dr;
        std::array<ItemId, kArmorSlots> ar{};

        SaveData sv{w, iv, {ms, bs, sr, sp}, wt, pl, bf, cp, yw, pt,
                    sd, rng, sl, hb, bd, td, play, dr, ar};
        SELFTEST_CHECK(SaveSystem::save(p, sv));

        // Blocks: Stone <-> Scaffold.
        SELFTEST_CHECK(swapKeysInSave(p, 0, "core:stone", "core:scaffold"));
        // Items: Copper Ingot <-> Charcoal.
        SELFTEST_CHECK(swapKeysInSave(p, 1, "core:copper_ingot", "core:charcoal"));

        World w2;
        Inventory iv2;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> ms2;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> bs2;
        std::unordered_map<glm::ivec3, float, IVec3Hash> sr2, sp2;
        Weather wt2;
        PlayerController pl2;
        float bf2 = 0.0f;
        glm::vec3 cp2{0.0f};
        float yw2 = 0.0f, pt2 = 0.0f;
        std::uint32_t sd2 = 0u, rng2 = 0u;
        int sl2 = 0;
        std::array<ItemId, kHotbarSlots> hb2{};
        bool bd2 = false, td2 = false;
        double play2 = 0.0;
        std::vector<DroppedItem> dr2;
        std::array<ItemId, kArmorSlots> ar2{};

        SaveData sv2{w2, iv2, {ms2, bs2, sr2, sp2}, wt2, pl2, bf2, cp2, yw2, pt2,
                     sd2, rng2, sl2, hb2, bd2, td2, play2, dr2, ar2};
        SELFTEST_CHECK(SaveSystem::load(p, sv2));

        // The two swapped blocks come back as each other; the third is proof
        // the whole table did not simply shift.
        SELFTEST_CHECK(w2.getBlock(0, 1, 0) == BlockId::Scaffold);
        SELFTEST_CHECK(w2.getBlock(1, 1, 0) == BlockId::Stone);
        SELFTEST_CHECK(w2.getBlock(2, 1, 0) == BlockId::Grass);

        // Same for the inventory, whose slot POSITIONS are item ordinals.
        SELFTEST_CHECK(iv2.count(ItemId::Charcoal) == 3);
        SELFTEST_CHECK(iv2.count(ItemId::CopperIngot) == 9);
        SELFTEST_CHECK(hb2[0] == ItemId::CopperIngot);

        // A save naming content this build does not have is refused outright,
        // rather than loaded with holes where the missing blocks were.
        SELFTEST_CHECK(renameKeyInSave(p, "core:stone", "mod:absent"));
        World w3;
        Inventory iv3;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> ms3;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> bs3;
        std::unordered_map<glm::ivec3, float, IVec3Hash> sr3, sp3;
        Weather wt3;
        PlayerController pl3;
        float bf3 = 0.0f;
        glm::vec3 cp3{0.0f};
        float yw3 = 0.0f, pt3 = 0.0f;
        std::uint32_t sd3 = 0u, rng3 = 0u;
        int sl3 = 0;
        std::array<ItemId, kHotbarSlots> hb3{};
        bool bd3 = false, td3 = false;
        double play3 = 0.0;
        std::vector<DroppedItem> dr3;
        std::array<ItemId, kArmorSlots> ar3{};
        SaveData sv3{w3, iv3, {ms3, bs3, sr3, sp3}, wt3, pl3, bf3, cp3, yw3, pt3,
                     sd3, rng3, sl3, hb3, bd3, td3, play3, dr3, ar3};
        SELFTEST_CHECK(!SaveSystem::load(p, sv3));

        fs::remove(p, ec);
        fs::remove(p + ".bak", ec);
        fs::remove(SaveSystem::metaPath(p), ec);
    }

    // ---- The Alchemy Circle (headless: no window, no GL) ----------------
    // Worth testing here rather than by hand: the necklace matcher is the one
    // piece of this game whose bugs are silent (a pattern quietly matching the
    // wrong recipe), and building a 5x5 multiblock in-game to check it is slow.
    {
        World cw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> cm;
        const glm::ivec3 core{40, 20, 40};
        cw.setBlock(core.x, core.y, core.z, BlockId::RuneCore);
        cm[core].type = BlockId::RuneCore;

        // No pedestals yet: an inert core.
        SELFTEST_CHECK(AlchemyCircle::tierAt(cw, cm, core) == AlchemyCircle::Tier::None);

        auto placePedestal = [&](int slot) {
            const glm::ivec3 p = AlchemyCircle::slotPos(core, slot);
            cw.setBlock(p.x, p.y, p.z, BlockId::Pedestal);
            cm[p].type = BlockId::Pedestal;
        };
        auto layOn = [&](int slot, ItemId id, int n) {
            cm[AlchemyCircle::slotPos(core, slot)].input.add(id, n);
        };

        for (int sl = 0; sl < AlchemyCircle::kRingSlots; sl += 2) placePedestal(sl);
        SELFTEST_CHECK(AlchemyCircle::tierAt(cw, cm, core) == AlchemyCircle::Tier::Lesser);

        // Conduit is "two plates on ONE pedestal"; the Wrench is "one plate on
        // each of two OPPOSITE pedestals". Same ingredients, told apart by
        // arrangement alone -- the whole reason the ring is a necklace.
        Inventory noCatalyst;
        layOn(0, ItemId::CopperPlate, 2);
        auto ring = AlchemyCircle::ringContents(cw, cm, core);
        auto m = AlchemyCircle::findMatch(ring, noCatalyst, AlchemyCircle::Tier::Lesser, false);
        SELFTEST_CHECK(m && m.recipe->output.id == ItemId::Conduit);

        cm[AlchemyCircle::slotPos(core, 0)].input.remove(ItemId::CopperPlate, 1);
        layOn(4, ItemId::CopperPlate, 1); // now 1 north + 1 south
        ring = AlchemyCircle::ringContents(cw, cm, core);
        m = AlchemyCircle::findMatch(ring, noCatalyst, AlchemyCircle::Tier::Lesser, false);
        SELFTEST_CHECK(m && m.recipe->output.id == ItemId::Wrench);

        // Rotation invariance: the same necklace laid starting at EAST must
        // match the same recipe, or orientation would punish the builder.
        cm[AlchemyCircle::slotPos(core, 0)].input.remove(ItemId::CopperPlate, 1);
        cm[AlchemyCircle::slotPos(core, 4)].input.remove(ItemId::CopperPlate, 1);
        layOn(2, ItemId::CopperPlate, 1);
        layOn(6, ItemId::CopperPlate, 1);
        ring = AlchemyCircle::ringContents(cw, cm, core);
        m = AlchemyCircle::findMatch(ring, noCatalyst, AlchemyCircle::Tier::Lesser, false);
        SELFTEST_CHECK(m && m.recipe->output.id == ItemId::Wrench);

        // The Press is the bootstrap machine (it makes the plates), so its
        // pattern has to be layable on a LESSER circle from hand-craftable
        // ingots and stone, and must not be shadowed by the Grinder or
        // Generator patterns it sits next to. It is the only 4-slot necklace
        // with no empty slot, which is exactly what keeps it unambiguous.
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
            cm[AlchemyCircle::slotPos(core, sl)].input = Inventory{};
        }
        layOn(0, ItemId::CopperIngot, 2);
        layOn(2, ItemId::Stone, 2);
        layOn(4, ItemId::CopperIngot, 2);
        layOn(6, ItemId::Stone, 2);
        ring = AlchemyCircle::ringContents(cw, cm, core);
        m = AlchemyCircle::findMatch(ring, noCatalyst, AlchemyCircle::Tier::Lesser, false);
        SELFTEST_CHECK(m && m.recipe->output.id == ItemId::PressItem);

        // A Lesser circle must NOT reach an eight-slot pattern, and the same
        // pattern must run once the full ring exists AND it is powered.
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
            cm[AlchemyCircle::slotPos(core, sl)].input = Inventory{};
        }
        // One short of the centre cost: the ring is laid perfectly and the
        // tier is right, so ONLY the catalyst count may refuse it. Worth
        // pinning -- a centre cost that silently rounds down to "any amount"
        // would make the game's most expensive craft free.
        Inventory catalyst;
        catalyst.add(ItemId::VoidCatalyst, kStormKeyCatalystCost - 1);
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
            if (sl % 2 == 0) placePedestal(sl);
            layOn(sl, sl % 2 == 0 ? ItemId::Crystal : ItemId::SpringWater, 1);
        }
        for (int sl = 1; sl < AlchemyCircle::kRingSlots; sl += 2) placePedestal(sl);
        SELFTEST_CHECK(AlchemyCircle::tierAt(cw, cm, core) == AlchemyCircle::Tier::Greater);
        ring = AlchemyCircle::ringContents(cw, cm, core);
        SELFTEST_CHECK(!AlchemyCircle::findMatch(ring, catalyst, AlchemyCircle::Tier::Greater, true));
        catalyst.add(ItemId::VoidCatalyst, 1); // now exactly the cost
        SELFTEST_CHECK(!AlchemyCircle::findMatch(ring, catalyst, AlchemyCircle::Tier::Lesser, true));
        SELFTEST_CHECK(!AlchemyCircle::findMatch(ring, catalyst, AlchemyCircle::Tier::Greater, false));
        m = AlchemyCircle::findMatch(ring, catalyst, AlchemyCircle::Tier::Greater, true);
        SELFTEST_CHECK(m && m.recipe->output.id == ItemId::StormKey);

        // Consuming a match empties exactly the pattern, catalyst included.
        AlchemyCircle::consume(cw, cm, core, m, catalyst);
        SELFTEST_CHECK(catalyst.count(ItemId::VoidCatalyst) == 0);
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
            SELFTEST_CHECK(cm[AlchemyCircle::slotPos(core, sl)].input.count(ItemId::Crystal) == 0);
            SELFTEST_CHECK(cm[AlchemyCircle::slotPos(core, sl)].input.count(ItemId::SpringWater) == 0);
        }

        // A pedestal that is a Machine but no longer a Pedestal BLOCK must not
        // contribute -- registry and world can disagree for a frame mid-edit.
        const glm::ivec3 north = AlchemyCircle::slotPos(core, 0);
        cw.setBlock(north.x, north.y, north.z, BlockId::Air);
        SELFTEST_CHECK(AlchemyCircle::tierAt(cw, cm, core) != AlchemyCircle::Tier::Greater);
    }

    // ---- The content set is coherent --------------------------------------
    // Recipe keys, circle-pattern shadowing and the tech-tree reachability
    // closure all moved into content::validate() (ContentValidate.h), because
    // the pack loader has to ask the same questions of content that arrives at
    // runtime -- and a generator repairing its own output needs the answers as
    // English, not as an exit code. This is that check, reported the way a
    // build wants it.
    {
        const std::vector<std::string> problems = content::validate();
        for (const std::string& msg : problems) std::printf("selftest: %s\n", msg.c_str());
        SELFTEST_CHECK(problems.empty());
    }

    // ---- Pre-v20 lock migration ------------------------------------------
    // Old saves stored selectedRecipe as a POSITION. That format can no longer
    // be written, so nothing else exercises this path -- and a regression here
    // would not fail loudly, it would quietly point every old save's locked
    // machine at the wrong recipe.
    {
        // A v19 Press's row 0 was the plate; it still is, wherever it sits now.
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(19, BlockId::Press, 0) ==
                       recipeIndexForKey(BlockId::Press, "press/copper-plate"));
        // v19 shifted the Press up by one and the Grinder down by one, so a
        // v18 file has to be walked through that step first.
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(18, BlockId::Press, 0) ==
                       recipeIndexForKey(BlockId::Press, "press/copper-rod"));
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(18, BlockId::Grinder, 1) ==
                       recipeIndexForKey(BlockId::Grinder, "grinder/ground-herb"));
        // The v18 Grinder's row 0 WAS the plate, which the Grinder no longer
        // makes: AUTO, never a neighbouring row.
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(18, BlockId::Grinder, 0) == -1);
        // AUTO stays AUTO; a position past the end of the old list is junk.
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(19, BlockId::Forge, -1) == -1);
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(19, BlockId::Grinder, 99) == -1);
        // A Rune Core's index runs over the CIRCLE table, not a machine list.
        SELFTEST_CHECK(SaveSystem::legacyRecipeIndex(19, BlockId::RuneCore, 0) ==
                       circleIndexForKey("circle/grinder"));
    }


    // ---- The hand-cranked tier is inert without a hand ---------------------
    // The whole point of the manual tier: a fully loaded machine left alone
    // must produce NOTHING and burn NOTHING, however long it sits. A
    // regression here would look like the tier merely being slow again, which
    // is exactly the state this replaced -- and it would go unnoticed, because
    // everything still works, just for free.
    {
        World cw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> cm;
        PowerState dead; // nothing energized; a Bloomery asks for no power
        const glm::ivec3 p{40, 20, 40};
        cw.setBlock(p.x, p.y, p.z, BlockId::Bloomery);
        cm[p].type = BlockId::Bloomery;
        cm[p].input.add(ItemId::CopperOre, 8); // a smelt is ready to go
        cm[p].fuel.add(ItemId::Charcoal, 4);   // and the fire is stocked

        std::uint32_t rc = 0;
        for (int i = 0; i < 400; ++i) { // 20 seconds of being ignored
            MachineSystem::tickPowered(cw, cm, dead, 1u, rc);
        }
        SELFTEST_CHECK(cm[p].progress == 0.0f);
        SELFTEST_CHECK(cm[p].fuel.count(ItemId::Charcoal) == 4);
        SELFTEST_CHECK(cm[p].output.count(ItemId::CopperIngot) == 0);

        // One turn of the handle, and it moves by exactly that much.
        cm[p].crankBanked = vg::kCrankProgress;
        MachineSystem::tickPowered(cw, cm, dead, 1u, rc);
        SELFTEST_CHECK(cm[p].progress == vg::kCrankProgress);
        SELFTEST_CHECK(cm[p].crankBanked == 0.0f);
        SELFTEST_CHECK(cm[p].fuel.count(ItemId::Charcoal) == 3); // now it burns

        // Enough turns to finish the craft. A Furnace smelt is 4s, and the
        // manual twin owes kManualSlowdown times that.
        const float need = 4.0f * kManualSlowdown;
        for (int i = 0; cm[p].output.count(ItemId::CopperIngot) == 0 && i < 64; ++i) {
            cm[p].crankBanked = vg::kCrankProgress;
            MachineSystem::tickPowered(cw, cm, dead, 1u, rc);
        }
        SELFTEST_CHECK(cm[p].output.count(ItemId::CopperIngot) == 1);
        SELFTEST_CHECK(cm[p].input.count(ItemId::CopperOre) == 6); // 2 per smelt
        SELFTEST_CHECK(need > 0.0f);

        // A powered twin, by contrast, runs on nothing but time.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> pm;
        const glm::ivec3 q{44, 20, 44};
        cw.setBlock(q.x, q.y, q.z, BlockId::Furnace);
        pm[q].type = BlockId::Furnace;
        pm[q].input.add(ItemId::CopperOre, 8);
        pm[q].fuel.add(ItemId::Charcoal, 4);
        for (int i = 0; i < 200; ++i) MachineSystem::tickPowered(cw, pm, dead, 1u, rc);
        SELFTEST_CHECK(pm[q].output.count(ItemId::CopperIngot) > 0);
    }

    std::printf("selftest OK\n");
    return 0;
}

// ---- --dump-recipes ------------------------------------------------------
// Emits RECIPES.md from the live tables. The doc used to be hand-maintained
// beside Recipes.cpp with a "update both together" warning on it, which is
// exactly the kind of promise a repo cannot keep -- so it is generated now:
//
//     voxel-factory.exe --dump-recipes > RECIPES.md
//
// Editing a recipe is one edit again, which was half the point of the keys.
int dumpRecipes() {
    auto stackList = [](const std::vector<ItemStack>& v) {
        std::string s;
        for (const ItemStack& i : v) {
            if (i.id == ItemId::None) continue;
            if (!s.empty()) s += " + ";
            s += itemName(i.id);
            if (i.count > 1) s += " x" + std::to_string(i.count);
        }
        return s.empty() ? std::string("-") : s;
    };

    std::printf("# Recipes\n\n");
    std::printf("**Generated** by `voxel-factory --dump-recipes` from the tables in\n"
                "`game/src/Recipes.cpp`. Do not hand-edit: edit the recipe and\n"
                "regenerate. The `key` column is the recipe's stable identity -- it is\n"
                "what a save stores for a machine locked to a MAKE row, which is why\n"
                "the tables can be reordered and edited freely.\n\n");

    std::printf("## Hand-craft (the survival tier)\n\n");
    std::printf("Instant and free, so it deliberately cannot build the factory.\n\n");
    std::printf("| key | inputs | output |\n|---|---|---|\n");
    for (const Recipe& r : handcraftRecipes()) {
        std::string out = itemName(r.output.id);
        if (r.output.count > 1) out += " x" + std::to_string(r.output.count);
        std::printf("| `%s` | %s | %s |\n", r.key, stackList(r.inputs).c_str(), out.c_str());
    }

    std::printf("\n## Machines\n\n");
    for (const MachineTraits& t : kMachineTraits) {
        if (t.recipeGroup != BlockId::Air) continue; // twins share the rows below
        const auto rows = recipesForMachine(t.block);

        std::printf("\n### %s\n\n", blockName(t.block));
        if (t.burnsFuel) std::printf("Burns fuel. ");
        else if (t.demand > 0) std::printf("Draws %d power. ", t.demand);
        else std::printf("Runs unpowered. ");
        // Name the hand-cranked twin, if it has one: the two tiers run the
        // same rows, so listing them twice would be a lie about the data.
        for (const MachineTraits& twin : kMachineTraits) {
            if (twin.recipeGroup != t.block) continue;
            std::printf("Hand tier: **%s** (%.0fx slower). ",
                        blockName(twin.block), static_cast<double>(twin.speedMult));
        }
        std::printf("\n\n");

        if (rows.empty()) {
            std::printf("_No recipes -- its behavior is code, not a table._\n");
            continue;
        }
        std::printf("| key | inputs | output | seconds |\n|---|---|---|---|\n");
        for (const MachineRecipe* r : rows) {
            std::string out;
            float total = 0.0f;
            for (const RecipeOutput& o : r->outputs) total += o.weight;
            for (const RecipeOutput& o : r->outputs) {
                if (!out.empty()) out += ", ";
                if (o.stack.id == ItemId::None) out += "nothing";
                else {
                    out += itemName(o.stack.id);
                    if (o.stack.count > 1) out += " x" + std::to_string(o.stack.count);
                }
                if (r->outputs.size() > 1 && total > 0.0f) {
                    out += " (" + std::to_string(
                        static_cast<int>(o.weight / total * 100.0f + 0.5f)) + "%)";
                }
            }
            std::printf("| `%s` | %s | %s | %.1f |\n", r->key,
                        stackList(r->inputs).c_str(), out.c_str(),
                        static_cast<double>(r->seconds));
        }
    }

    std::printf("\n## Alchemy Circle\n\n");
    std::printf("A `ring` of 4 entries is the CARDINAL pedestals clockwise from north\n"
                "(a Lesser circle can run it); 8 entries is the full ring and needs a\n"
                "powered Greater circle. `-` is a slot that must be EMPTY. Matching is\n"
                "rotation-invariant and each slot matches \"holds at least this many\",\n"
                "so one pattern can shadow another -- order is what disambiguates, and\n"
                "`--selftest` lays every pattern to prove none is unreachable.\n\n");
    std::printf("| key | centre | ring (clockwise from N) | output | seconds |\n"
                "|---|---|---|---|---|\n");
    for (const CircleRecipe& r : circleRecipes()) {
        std::string ring;
        for (const ItemStack& slot : r.ring) {
            if (!ring.empty()) ring += ", ";
            if (slot.id == ItemId::None) { ring += "-"; continue; }
            ring += itemName(slot.id);
            if (slot.count > 1) ring += " x" + std::to_string(slot.count);
        }
        std::string centre = "-";
        if (r.center.id != ItemId::None) {
            centre = itemName(r.center.id);
            if (r.center.count > 1) centre += " x" + std::to_string(r.center.count);
        }
        std::string out = itemName(r.output.id);
        if (r.output.count > 1) out += " x" + std::to_string(r.output.count);
        std::printf("| `%s` | %s | %s | %s | %.1f |\n", r.key, centre.c_str(),
                    ring.c_str(), out.c_str(), static_cast<double>(r.seconds));
    }

    std::printf("\n## Fuels\n\n| item | seconds |\n|---|---|\n");
    for (const FuelInfo& f : kFuels) {
        std::printf("| %s | %.0f |\n", itemName(f.item), static_cast<double>(f.seconds));
    }
    std::printf("\nA machine that both burns fuel and runs recipes has a FUEL buffer of its\n"
                "own, so a Furnace can char wood while burning wood -- which pile an\n"
                "arriving belt item joins is inferred (ingredient wins), and a hand-drag\n"
                "lands in the cell you dropped it on.\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();

    // Headless save round-trip for CI; runs before any window/GL setup.
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) {
        return runSelfTest();
    }
    // Regenerates RECIPES.md from the live tables; also headless.
    if (argc > 1 && std::strcmp(argv[1], "--dump-recipes") == 0) {
        return dumpRecipes();
    }
    // Is the content set coherent? The --selftest checks that are about
    // CONTENT rather than about code, on their own and without the save
    // round-trip: the answer a pack author (or a generator repairing its own
    // output) actually wants, printed one problem per line.
    if (argc > 1 && std::strcmp(argv[1], "--validate") == 0) {
        const std::vector<std::string> problems = content::validate();
        for (const std::string& msg : problems) std::printf("%s\n", msg.c_str());
        return problems.empty() ? 0 : 1;
    }

    // File logging + crash dumps live under the pref dir, next to the save, so
    // a player's bug report carries a log and (on a fault) a dump. Resolved the
    // same way VoxelGame does; SDL_GetPrefPath needs no prior SDL_Init.
    const std::string pref = engine::prefDir(vg::kOrgName, vg::kAppName);
    if (!pref.empty()) {
        engine::Log::init(pref);
        engine::CrashHandler::install(pref);
    }

    int exitCode = 0;
    try {
        VoxelGame game;
        game.run();
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        // Launched from Explorer there is no console — surface the error.
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Voxel Factory - Fatal Error",
                                 e.what(), nullptr);
        exitCode = 1;
    }
    engine::Log::shutdown();
    return exitCode;
}
