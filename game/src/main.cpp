// We run our own main(), so tell SDL not to hijack it.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>

#include "game/VoxelGame.h"
#include "game/AlchemyCircle.h"
#include "game/BlockShape.h"
#include "game/ContentPack.h"
#include "game/ContentRegistry.h"
#include "game/ContentValidate.h"
#include "game/CreatureSystem.h"
#include "game/CropSystem.h"
#include "game/MachineSystem.h"
#include "game/Recipes.h"
#include "game/SaveSystem.h"
#include "game/Settings.h"
#include "game/TubeShape.h"
#include "game/World.h"
#include "VoxelGameInternal.h" // vg::kOrgName / kAppName

#include "engine/BbModel.h"
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
    // The crop registry every tickPowered takes. Only the Harvester touches it,
    // so the cases below that are not about farming share one empty map.
    CropSystem::CropMap noCrops;
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
    // Switched OFF (v24). The default is true, so only a machine that was
    // deliberately idled proves the flag is actually written and read -- the
    // grinder above stays on, so the round-trip has to carry both values.
    furnace.enabled = false;
    machines[glm::ivec3{9, 3, 3}] = furnace;

    std::unordered_map<glm::ivec3, Belt, IVec3Hash> belts;
    // Cargo AND a filter (v23), and deliberately different items -- a filter
    // that happened to equal the cargo would pass even if the two were written
    // or read in the wrong order.
    belts[glm::ivec3{2, 3, 3}] =
        Belt{glm::ivec3{-1, 0, 0}, ItemId::GroundHerb, ItemId::Crystal};
    // Cargo MOTION is render state, re-derived by the next beltStep. Set it
    // here so the load side can prove it is not written: persisting it would
    // have forced a save version for a purely visual field.
    belts[glm::ivec3{2, 3, 3}].cameFrom = glm::ivec3{0, 0, 1};

    std::unordered_map<glm::ivec3, float, IVec3Hash> sources;
    sources[glm::ivec3{4, 2, 4}] = 3.5f;
    std::unordered_map<glm::ivec3, float, IVec3Hash> saplings;
    saplings[glm::ivec3{6, 2, 6}] = 9.0f;
    CropSystem::CropMap crops;
    crops[glm::ivec3{7, 2, 7}] = 12.5f;

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

    SaveData src{world, inv, {machines, belts, sources, saplings, crops},
                 weather, player, bucketFill,
                 camPos, yaw, pitch, seed, rngState, slot, hotbar, bossDefeated,
                 tempestDefeated, playtime, drops, armor};
    SELFTEST_CHECK(SaveSystem::save(path, src));

    World world2;
    Inventory inv2;
    std::unordered_map<glm::ivec3, Machine, IVec3Hash> machines2;
    std::unordered_map<glm::ivec3, Belt, IVec3Hash> belts2;
    std::unordered_map<glm::ivec3, float, IVec3Hash> sources2, saplings2;
    CropSystem::CropMap crops2;
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
    SaveData dst{world2, inv2, {machines2, belts2, sources2, saplings2, crops2},
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
    SELFTEST_CHECK(m2.enabled); // v24: this one was left running

    const Machine& f2 = machines2.at(glm::ivec3{9, 3, 3});
    SELFTEST_CHECK(f2.type == BlockId::Furnace);
    SELFTEST_CHECK(f2.burnLeft == 12.5f);
    SELFTEST_CHECK(!f2.enabled); // v24: ...and this one was switched off
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
    SELFTEST_CHECK(b2.filter == ItemId::Crystal); // v23
    // Transient, so it must come back cleared rather than round-tripped --
    // this is the assertion that says the flowing-cargo visual cost no bump.
    SELFTEST_CHECK(b2.cameFrom == glm::ivec3(0));

    SELFTEST_CHECK(sources2.size() == 1 && sources2.at(glm::ivec3{4, 2, 4}) == 3.5f);
    SELFTEST_CHECK(saplings2.size() == 1 && saplings2.at(glm::ivec3{6, 2, 6}) == 9.0f);
    // v25's tail append: how far a plant is into its current stage. The stage
    // itself rides the chunk data, so this is the only part that needed a bump.
    SELFTEST_CHECK(crops2.size() == 1 && crops2.at(glm::ivec3{7, 2, 7}) == 12.5f);

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
    CropSystem::CropMap crops3;
    std::vector<DroppedItem> drops3;
    std::array<ItemId, kArmorSlots> armor3{};
    SaveData cutDst{world3, inv2, {machines3, belts3, sources3, saplings3, crops3},
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
    // A cfg from before an action existed has no line for it -> its default.
    SELFTEST_CHECK(u.key(Action::Screenshot) == SDL_SCANCODE_F2);

    // ...unless that old cfg already gave the new default away: the player's
    // own bind wins (first in enum order) and the newcomer loads unbound.
    {
        std::ofstream old(cfg, std::ios::trunc);
        old << "BIND_HELP=" << static_cast<int>(SDL_SCANCODE_F2) << "\n";
    }
    Settings oldCfg;
    SELFTEST_CHECK(SettingsIO::load(cfg, oldCfg));
    SELFTEST_CHECK(oldCfg.key(Action::Help) == SDL_SCANCODE_F2);
    SELFTEST_CHECK(oldCfg.key(Action::Screenshot) == SDL_SCANCODE_UNKNOWN);

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
        CropSystem::CropMap cr;
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

        SaveData sv{w, iv, {ms, bs, sr, sp, cr}, wt, pl, bf, cp, yw, pt,
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
        CropSystem::CropMap cr2;
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

        SaveData sv2{w2, iv2, {ms2, bs2, sr2, sp2, cr2}, wt2, pl2, bf2, cp2, yw2, pt2,
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
        CropSystem::CropMap cr3;
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
        SaveData sv3{w3, iv3, {ms3, bs3, sr3, sp3, cr3}, wt3, pl3, bf3, cp3, yw3, pt3,
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

    // ---- A circle runs only once STARTED, and only what it was started on ---
    // The bug this pins: laying a Press by hand passes through "two ingots on
    // one pedestal", which satisfies circle/wire ("at least one ingot"), and a
    // circle that ran whatever the ring spelled crafted wire out from under
    // you. Starting locks the recipe; the lock then keeps a belt-fed circle on
    // THAT recipe while its pattern refills.
    {
        World cw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> cm;
        PowerState dead; // a Lesser circle runs unpowered
        const glm::ivec3 core{40, 20, 40};
        cw.setBlock(core.x, core.y, core.z, BlockId::RuneCore);
        cm[core].type = BlockId::RuneCore;
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; sl += 2) {
            const glm::ivec3 p = AlchemyCircle::slotPos(core, sl);
            cw.setBlock(p.x, p.y, p.z, BlockId::Pedestal);
            cm[p].type = BlockId::Pedestal;
        }
        auto layOn = [&](int slot, ItemId id, int n) {
            cm[AlchemyCircle::slotPos(core, slot)].input.add(id, n);
        };
        std::uint32_t rc = 0;
        std::vector<MachineSystem::CircleCompletion> done;
        auto run = [&](int ticks) {
            for (int i = 0; i < ticks; ++i) {
                MachineSystem::tickPowered(cw, cm, dead, 1u, rc, noCrops, &done);
            }
        };

        // Half-laid: a pattern the ring spells (Wire), never started.
        layOn(0, ItemId::CopperIngot, 2);
        run(400); // far past a Lesser wire craft
        SELFTEST_CHECK(cm[core].output.count(ItemId::WireItem) == 0);
        SELFTEST_CHECK(cm[AlchemyCircle::slotPos(core, 0)].input.count(ItemId::CopperIngot) == 2);
        SELFTEST_CHECK(cm[core].progress == 0.0f);

        // Finish laying the Press, start it on exactly that.
        layOn(2, ItemId::Stone, 2);
        layOn(4, ItemId::CopperIngot, 2);
        layOn(6, ItemId::Stone, 2);
        const auto ring = AlchemyCircle::ringContents(cw, cm, core);
        const auto match = AlchemyCircle::findMatch(ring, cm[core].input,
                                                    AlchemyCircle::Tier::Lesser, false);
        SELFTEST_CHECK(match && match.recipe->output.id == ItemId::PressItem);
        cm[core].selectedRecipe = static_cast<int>(match.recipe - circleRecipes().data());
        const int pressTicks = static_cast<int>(
            AlchemyCircle::craftSeconds(*match.recipe, AlchemyCircle::Tier::Lesser, false) /
            vg::kTickSeconds) + 2;
        run(pressTicks);
        SELFTEST_CHECK(cm[core].output.count(ItemId::PressItem) == 1);
        SELFTEST_CHECK(cm[core].output.count(ItemId::WireItem) == 0);

        // The finish was REPORTED, with each ingredient on the pedestal it sat on.
        SELFTEST_CHECK(done.size() == 1);
        if (done.size() == 1) {
            SELFTEST_CHECK(done[0].core == core && done[0].made == ItemId::PressItem);
            SELFTEST_CHECK(!done[0].greater);
            SELFTEST_CHECK(done[0].consumed[0] == ItemId::CopperIngot &&
                           done[0].consumed[2] == ItemId::Stone &&
                           done[0].consumed[4] == ItemId::CopperIngot &&
                           done[0].consumed[6] == ItemId::Stone);
            SELFTEST_CHECK(done[0].consumed[1] == ItemId::None);
        }

        // Still started: a lone ingot now spells Wire, and it must NOT run --
        // this is a belt mid-refill, and the circle waits for its Press.
        layOn(0, ItemId::CopperIngot, 1);
        run(400);
        SELFTEST_CHECK(cm[core].selectedRecipe >= 0);
        SELFTEST_CHECK(cm[core].output.count(ItemId::WireItem) == 0);
        SELFTEST_CHECK(cm[core].output.count(ItemId::PressItem) == 1);

        // ...and the refilled pattern runs again with no second START.
        layOn(0, ItemId::CopperIngot, 1);
        layOn(2, ItemId::Stone, 2);
        layOn(4, ItemId::CopperIngot, 2);
        layOn(6, ItemId::Stone, 2);
        run(pressTicks);
        SELFTEST_CHECK(cm[core].output.count(ItemId::PressItem) == 2);
        SELFTEST_CHECK(done.size() == 2);
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

    // ---- The pack format reads back what it writes -------------------------
    // --dump-content is the format's specification, which is only true while
    // the loader accepts it exactly. Feed the dump back and dump again: any
    // field the writer emits and the reader drops (or rounds, or reorders)
    // shows up here as a difference, and nowhere else.
    {
        const std::string packPath =
            (fs::temp_directory_path() / "voxel-factory-selftest-pack.json").string();
        const std::string before = content::dumpContent();
        // The compiled content set, kept so each case below can put back
        // exactly what it found and the next one starts from the build again.
        const std::vector<BlockInfo> blockRows0 = blockRows();
        const std::vector<ItemInfo> itemRows0 = itemRows();
        const std::vector<MachineTraits> machineTraitRows0 = machineTraitRows();
        const std::vector<FuelInfo> fuelRows0 = fuelRows();
        const std::vector<MachineRecipe> machineRecipes0 = recipes::machineTable();
        {
            std::ofstream out(packPath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << before;
        }
        const std::vector<std::string> problems = content::applyPacks({packPath});
        for (const std::string& msg : problems) std::printf("selftest: %s\n", msg.c_str());
        SELFTEST_CHECK(problems.empty());
        SELFTEST_CHECK(content::dumpContent() == before);

        // ---- A pack that deadlocks the tech tree is refused, WHOLE ---------
        // The generate -> validate -> repair loop rests on this: content that
        // parses perfectly can still describe an unplayable game, and the
        // reachability closure is what notices. Deleting the only recipe that
        // presses a Copper Plate strands every machine behind it.
        //
        // What is actually under test is the rollback. A pack is applied before
        // it can be judged -- there is no way to ask "would this close?" of a
        // table it is not in -- so a refusal has to put back exactly what was
        // there, or a bad pack would half-convert the game on its way out.
        const std::string badPath =
            (fs::temp_directory_path() / "voxel-factory-selftest-badpack.json").string();
        {
            std::ofstream out(badPath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << R"({"format": 1, "recipes": {"remove": ["press/copper-plate"]}})";
        }
        std::error_code rmErr;
        const std::vector<std::string> refused = content::applyPacks({badPath});
        SELFTEST_CHECK(!refused.empty());
        SELFTEST_CHECK(content::validate().empty());
        SELFTEST_CHECK(content::dumpContent() == before);
        SELFTEST_CHECK(recipeIndexForKey(BlockId::Press, "press/copper-plate") >= 0);

        // ---- A pack that breaks the RENEWABLE loop is refused --------------
        // Reachability does not blink at this, and cannot: the island generates
        // plenty of stone, so every recipe below stays reachable forever. What
        // it costs is the thousandth hour.
        //
        // hand/pebble-stone is the whole hinge, and it is worth knowing why.
        // compactor/stone would close the loop -- Dirt and Sand both regrow --
        // but a Compactor costs Stone x8, a Tamper costs Stone x6, and the
        // Circle you would build either on costs Stone too. EVERY producer of
        // stone costs stone. Sifted topsoil is the only way in that does not,
        // so deleting that one row turns the entire tech tree into a finite
        // pile of whatever worldgen happened to bury.
        const std::string finitePath =
            (fs::temp_directory_path() / "voxel-factory-selftest-finitepack.json").string();
        {
            std::ofstream out(finitePath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << R"({"format": 1, "recipes": {"remove": ["hand/pebble-stone"]}})";
        }
        const std::vector<std::string> finite = content::applyPacks({finitePath});
        SELFTEST_CHECK(!finite.empty());
        SELFTEST_CHECK(content::validate().empty());   // the rollback put it back
        SELFTEST_CHECK(content::dumpContent() == before);

        // ---- An item nothing consumes is refused ---------------------------
        // The mirror of the check above: reachability proves you can GET
        // everything, this proves everything you get is FOR something. The key
        // is deliberately `core:`, which also pins that the balance checks
        // scope by NAMESPACE rather than being switched off -- the mod:widget
        // pack further down adds exactly such an item and must still be taken.
        const std::string orphanPath =
            (fs::temp_directory_path() / "voxel-factory-selftest-orphanpack.json").string();
        {
            std::ofstream out(orphanPath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << R"({"format": 1,
                 "items": [{"key": "core:trophy", "name": "Trophy", "atlasTile": 64}]})";
        }
        const std::vector<std::string> orphan = content::applyPacks({orphanPath});
        SELFTEST_CHECK(!orphan.empty());
        SELFTEST_CHECK(content::dumpContent() == before);

        // A pack naming content this build lacks is refused the same way, and
        // says which key -- the difference between a fixable complaint and a
        // shrug.
        const std::string unknownPath =
            (fs::temp_directory_path() / "voxel-factory-selftest-unknownpack.json").string();
        {
            std::ofstream out(unknownPath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << R"({"format": 1, "recipes": {"hand": [{"key": "hand/x",
                 "inputs": [{"item": "mod:unobtainium"}],
                 "output": {"item": "core:stone"}}]}})";
        }
        const std::vector<std::string> unknown = content::applyPacks({unknownPath});
        SELFTEST_CHECK(unknown.size() == 1);
        SELFTEST_CHECK(unknown.front().find("mod:unobtainium") != std::string::npos);
        SELFTEST_CHECK(content::dumpContent() == before);

        // ---- A row is a PATCH of the row it names, not a replacement -------
        // The dump states every non-default field, so the round-trip above
        // reads identically either way and cannot see this. What can is a pack
        // written the way anyone actually writes one: name a key, state the
        // one field you came to change. Under replacement semantics that Stone
        // would come back black, untextured, dropping nothing and needing no
        // pickaxe -- silently, since every one of those is a legal value.
        const std::string patchPath =
            (fs::temp_directory_path() / "voxel-factory-selftest-patchpack.json").string();
        {
            std::ofstream out(patchPath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << R"({"format": 1,
                 "blocks": [{"key": "core:stone", "hardness": 9.0}],
                 "items": [{"key": "core:copper_sword", "weaponDamage": 4.0}],
                 "machines": [{"block": "core:press", "demand": 11}],
                 "recipes": {"machine": [{"key": "press/copper-plate", "seconds": 0.5}]}})";
        }
        const BlockInfo stone0 = blockInfo(BlockId::Stone);
        const std::vector<std::string> patched = content::applyPacks({patchPath});
        for (const std::string& msg : patched) std::printf("selftest: %s\n", msg.c_str());
        SELFTEST_CHECK(patched.empty());
        {
            const BlockInfo& stone = blockInfo(BlockId::Stone);
            SELFTEST_CHECK(stone.hardness == 9.0f);        // what the pack said
            SELFTEST_CHECK(stone.drop.item == stone0.drop.item &&
                           stone.drop.count == stone0.drop.count);
            SELFTEST_CHECK(stone.tiles.side == stone0.tiles.side);
            SELFTEST_CHECK(stone.color == stone0.color);
            SELFTEST_CHECK(stone.tool == stone0.tool && stone.toolTier == stone0.toolTier);
            SELFTEST_CHECK(std::string(stone.name) == stone0.name);
            SELFTEST_CHECK(itemInfo(ItemId::CopperSword).weaponDamage == 4.0f);
            SELFTEST_CHECK(itemInfo(ItemId::CopperSword).tool == ToolType::None);
            SELFTEST_CHECK(machineTraits(BlockId::Press).demand == 11);
            // The trait a patch did not mention: a Press is still a Processor
            // that runs at full speed, not a defaulted stub.
            SELFTEST_CHECK(machineTraits(BlockId::Press).kind == MachineKind::Processor);
            const int at = recipeIndexForKey(BlockId::Press, "press/copper-plate");
            SELFTEST_CHECK(at >= 0);
            const MachineRecipe& r = *recipesForMachine(BlockId::Press)[static_cast<std::size_t>(at)];
            SELFTEST_CHECK(r.seconds == 0.5f);
            SELFTEST_CHECK(!r.inputs.empty() && !r.outputs.empty()); // not a free plate
        }
        // Restore before the next case, which asserts against the compiled set.
        restoreBlocks(blockRows0);
        restoreItems(itemRows0);
        restoreMachineTraits(machineTraitRows0);
        restoreFuels(fuelRows0);
        recipes::machineTable() = machineRecipes0;
        SELFTEST_CHECK(content::dumpContent() == before);

        // ---- A pack that ADDS content, and a save that survives it ---------
        // The whole point of the runtime registries: a block whose ordinal is
        // past BlockId::Count has to ride every path a compiled one does. Most
        // of those would fail loudly. The save would NOT -- its chunk bytes and
        // its key table are both sized by the content set, and a mismatch there
        // reads back as the wrong block rather than as an error. So this puts a
        // modded block in a world, round-trips it, and looks at what comes back.
        const std::string addPath =
            (fs::temp_directory_path() / "voxel-factory-selftest-addpack.json").string();
        {
            std::ofstream out(addPath, std::ios::binary);
            SELFTEST_CHECK(static_cast<bool>(out));
            out << R"({"format": 1,
                 "items": [{"key": "mod:widget", "name": "Widget", "atlasTile": 64}],
                 "blocks": [{"key": "mod:widget_ore", "name": "Widget Ore",
                             "color": "#8020a0", "drop": {"item": "mod:widget", "count": 2},
                             "tiles": {"top": 3, "side": 3, "bottom": 3}, "hardness": 1.0}]})";
        }
        const std::vector<std::string> added = content::applyPacks({addPath});
        for (const std::string& msg : added) std::printf("selftest: %s\n", msg.c_str());
        SELFTEST_CHECK(added.empty());

        const BlockId modBlock = content::blockFromKey("mod:widget_ore");
        const ItemId modItem = content::itemFromKey("mod:widget");
        SELFTEST_CHECK(modBlock != content::kNoBlock && modItem != content::kNoItem);
        // Past the compiled enum, which is the case that did not exist before.
        SELFTEST_CHECK(static_cast<int>(modBlock) >= static_cast<int>(BlockId::Count));
        SELFTEST_CHECK(static_cast<int>(modItem) >= static_cast<int>(ItemId::Count));
        SELFTEST_CHECK(blockDrop(modBlock).id == modItem && blockDrop(modBlock).count == 2);
        // An Inventory sized before the item existed must still hold it -- that
        // is why it grows on demand rather than only at construction.
        SELFTEST_CHECK(blockName(modBlock) == std::string("Widget Ore"));

        {
            const std::string modSave =
                (fs::temp_directory_path() / "voxel-factory-selftest-mod.vxf").string();
            fs::remove(modSave, rmErr);
            World mw;
            mw.setBlock(3, 4, 5, modBlock);
            mw.setBlock(3, 4, 6, BlockId::Stone); // a compiled block beside it
            Inventory minv;
            minv.add(modItem, 9);
            minv.add(ItemId::Stone, 4);
            Weather mwx;
            PlayerController mp;
            float mfill = 0.0f;
            glm::vec3 mpos{0.0f};
            float myaw = 0.0f, mpitch = 0.0f;
            std::uint32_t mseed = 7u, mrng = 8u;
            int mslot = 0;
            std::array<ItemId, kHotbarSlots> mhot{};
            mhot[0] = modItem;
            bool mb1 = false, mb2 = false;
            double mplay = 0.0;
            std::vector<DroppedItem> mdrops;
            std::array<ItemId, kArmorSlots> marmor{};
            std::unordered_map<glm::ivec3, Machine, IVec3Hash> mmach;
            std::unordered_map<glm::ivec3, Belt, IVec3Hash> mbelt;
            std::unordered_map<glm::ivec3, float, IVec3Hash> msrc, msap;
            CropSystem::CropMap mcrop;
            SaveData ms{mw, minv, {mmach, mbelt, msrc, msap, mcrop}, mwx, mp, mfill, mpos, myaw, mpitch,
                        mseed, mrng, mslot, mhot, mb1, mb2, mplay, mdrops, marmor};
            SELFTEST_CHECK(SaveSystem::save(modSave, ms));

            World rw;
            Inventory rinv;
            Weather rwx;
            PlayerController rp;
            float rfill = 0.0f;
            glm::vec3 rpos{0.0f};
            float ryaw = 0.0f, rpitch = 0.0f;
            std::uint32_t rseed = 0u, rrng = 0u;
            int rslot = 0;
            std::array<ItemId, kHotbarSlots> rhot{};
            bool rb1 = false, rb2 = false;
            double rplay = 0.0;
            std::vector<DroppedItem> rdrops;
            std::array<ItemId, kArmorSlots> rarmor{};
            std::unordered_map<glm::ivec3, Machine, IVec3Hash> rmach;
            std::unordered_map<glm::ivec3, Belt, IVec3Hash> rbelt;
            std::unordered_map<glm::ivec3, float, IVec3Hash> rsrc, rsap;
            CropSystem::CropMap rcrop;
            SaveData rs{rw, rinv, {rmach, rbelt, rsrc, rsap, rcrop}, rwx, rp, rfill, rpos, ryaw, rpitch,
                        rseed, rrng, rslot, rhot, rb1, rb2, rplay, rdrops, rarmor};
            SELFTEST_CHECK(SaveSystem::load(modSave, rs));
            SELFTEST_CHECK(rw.getBlock(3, 4, 5) == modBlock);
            SELFTEST_CHECK(rw.getBlock(3, 4, 6) == BlockId::Stone);
            SELFTEST_CHECK(rinv.count(modItem) == 9);
            SELFTEST_CHECK(rinv.count(ItemId::Stone) == 4);
            SELFTEST_CHECK(rhot[0] == modItem);
            fs::remove(modSave, rmErr);
        }

        // Put the compiled content back: everything after this is about the
        // build, not about a pack.
        restoreBlocks(blockRows0);
        restoreItems(itemRows0);
        restoreMachineTraits(machineTraitRows0);
        SELFTEST_CHECK(content::dumpContent() == before);

        std::error_code rmErr2;
        fs::remove(packPath, rmErr2);
        fs::remove(badPath, rmErr2);
        fs::remove(unknownPath, rmErr2);
        fs::remove(addPath, rmErr2);
        fs::remove(patchPath, rmErr2);
        fs::remove(finitePath, rmErr2);
        fs::remove(orphanPath, rmErr2);
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
        PowerState dead; // nothing energized; the manual tier asks for no power
        const glm::ivec3 p{40, 20, 40};
        // A MORTAR, not a Bloomery: the crank tier is now exactly the machines
        // your arm drives, and the Bloomery left it (a fire is not an arm --
        // see below). Picking a fuelless one also keeps this test about the one
        // thing it is for.
        cw.setBlock(p.x, p.y, p.z, BlockId::Mortar);
        cm[p].type = BlockId::Mortar;
        cm[p].input.add(ItemId::Crystal, 8); // a grind is ready to go

        std::uint32_t rc = 0;
        for (int i = 0; i < 400; ++i) { // 20 seconds of being ignored
            MachineSystem::tickPowered(cw, cm, dead, 1u, rc, noCrops);
        }
        SELFTEST_CHECK(cm[p].progress == 0.0f);
        SELFTEST_CHECK(cm[p].output.count(ItemId::CrystalDust) == 0);
        SELFTEST_CHECK(cm[p].input.count(ItemId::Crystal) == 8);

        // One turn of the handle, and it moves by exactly that much.
        cm[p].crankBanked = vg::kCrankProgress;
        MachineSystem::tickPowered(cw, cm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(cm[p].progress == vg::kCrankProgress);
        SELFTEST_CHECK(cm[p].crankBanked == 0.0f);

        // Enough turns to finish the craft. A Grinder grind is 2s, and the
        // manual twin owes kManualSlowdown times that.
        for (int i = 0; cm[p].output.count(ItemId::CrystalDust) == 0 && i < 64; ++i) {
            cm[p].crankBanked = vg::kCrankProgress;
            MachineSystem::tickPowered(cw, cm, dead, 1u, rc, noCrops);
        }
        SELFTEST_CHECK(cm[p].output.count(ItemId::CrystalDust) == 1);
        SELFTEST_CHECK(cm[p].input.count(ItemId::Crystal) == 7);

        // A powered twin, by contrast, runs on nothing but time.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> pm;
        const glm::ivec3 q{44, 20, 44};
        cw.setBlock(q.x, q.y, q.z, BlockId::Furnace);
        pm[q].type = BlockId::Furnace;
        pm[q].input.add(ItemId::CopperOre, 8);
        pm[q].fuel.add(ItemId::Charcoal, 4);
        for (int i = 0; i < 200; ++i) MachineSystem::tickPowered(cw, pm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(pm[q].output.count(ItemId::CopperIngot) > 0);
    }

    // ---- The Bloomery is FIRE-driven, not arm-driven ------------------------
    // It sat in the manual tier as a hand-cranked machine, which meant a lit
    // bloomery full of ore did nothing at all unless somebody stood at it
    // turning arrows. What does the work in a bloomery is the burn, so it now
    // runs on the clock like every other machine and pays its manual-tier dues
    // in time and wasted fuel instead. Pinned here because the traits row that
    // says so is one word, and losing it would look like a balance tweak.
    {
        World bw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> bm;
        PowerState dead;
        const glm::ivec3 p{48, 20, 48};
        bw.setBlock(p.x, p.y, p.z, BlockId::Bloomery);
        bm[p].type = BlockId::Bloomery;
        bm[p].input.add(ItemId::CopperOre, 8);
        bm[p].fuel.add(ItemId::Charcoal, 4);

        SELFTEST_CHECK(!machineTraits(BlockId::Bloomery).handCranked);
        SELFTEST_CHECK(machineTraits(BlockId::Bloomery).burnsFuel);
        SELFTEST_CHECK(machineTraits(BlockId::Bloomery).demand == 0); // never electric
        // Still a manual-tier machine in every other sense: the Furnace's
        // recipes, kManualSlowdown times as long, on fuel it wastes.
        SELFTEST_CHECK(recipeGroupFor(BlockId::Bloomery) == BlockId::Furnace);
        SELFTEST_CHECK(machineTraits(BlockId::Bloomery).speedMult == kManualSlowdown);
        SELFTEST_CHECK(machineTraits(BlockId::Bloomery).fuelMult < 1.0f);

        std::uint32_t rc = 0;
        for (int i = 0; i < 400; ++i) { // left completely alone
            MachineSystem::tickPowered(bw, bm, dead, 1u, rc, noCrops);
        }
        SELFTEST_CHECK(bm[p].output.count(ItemId::CopperIngot) > 0); // it ran
        SELFTEST_CHECK(bm[p].input.count(ItemId::CopperOre) < 8);    // it ate ore
        SELFTEST_CHECK(bm[p].fuel.count(ItemId::Charcoal) < 4);      // it burned

        // ...and it is still slower than the Furnace it copies, on the same
        // stock and the same number of ticks. That gap IS the tier.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> fm2;
        const glm::ivec3 q{52, 20, 52};
        bw.setBlock(q.x, q.y, q.z, BlockId::Furnace);
        fm2[q].type = BlockId::Furnace;
        fm2[q].input.add(ItemId::CopperOre, 8);
        fm2[q].fuel.add(ItemId::Charcoal, 4);
        std::uint32_t rc2 = 0;
        for (int i = 0; i < 400; ++i) MachineSystem::tickPowered(bw, fm2, dead, 1u, rc2, noCrops);
        SELFTEST_CHECK(fm2[q].output.count(ItemId::CopperIngot) >
                       bm[p].output.count(ItemId::CopperIngot));

        // Turning it OFF is what turning off any machine is: stop feeding the
        // fire. No handle, no switch -- it simply stops when the fuel runs out.
        bm[p].fuel.remove(ItemId::Charcoal, bm[p].fuel.count(ItemId::Charcoal));
        bm[p].burnLeft = 0.0f;
        const int made = bm[p].output.count(ItemId::CopperIngot);
        for (int i = 0; i < 400; ++i) MachineSystem::tickPowered(bw, bm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(bm[p].output.count(ItemId::CopperIngot) == made);
    }

    // ---- Buffers have a bottom, and a full one stops the line --------------
    // Every Inventory is unbounded, so before these caps a machine's output
    // swallowed everything and nothing in a factory could ever be wrong. The
    // checks that matter are the ones about what a jam must NOT do: eat inputs,
    // burn fuel, or touch the shared RNG counter.
    {
        World jw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> jm;
        PowerState dead; // a Furnace burns fuel and asks for no power
        const glm::ivec3 p{60, 20, 60};
        jw.setBlock(p.x, p.y, p.z, BlockId::Furnace);
        Machine& f = jm[p];
        f.type = BlockId::Furnace;
        f.input.add(ItemId::CopperOre, 8);
        f.fuel.add(ItemId::Charcoal, 4);
        f.output.add(ItemId::CopperIngot, vg::kMachineOutputCap); // nowhere to put one more

        std::uint32_t rc = 7;
        const std::uint32_t rcBefore = rc;
        for (int i = 0; i < 200; ++i) MachineSystem::tickPowered(jw, jm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(jm[p].jammed);
        SELFTEST_CHECK(jm[p].output.count(ItemId::CopperIngot) == vg::kMachineOutputCap);
        SELFTEST_CHECK(jm[p].input.count(ItemId::CopperOre) == 8);   // inputs untouched
        SELFTEST_CHECK(jm[p].fuel.count(ItemId::Charcoal) == 4);     // fire never lit
        SELFTEST_CHECK(rc == rcBefore); // no roll thrown away -- see outputHasRoom

        // Drain it and the same craft resumes; a jam holds, it doesn't cancel.
        jm[p].output.remove(ItemId::CopperIngot, vg::kMachineOutputCap);
        for (int i = 0; i < 200; ++i) MachineSystem::tickPowered(jw, jm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(!jm[p].jammed);
        SELFTEST_CHECK(jm[p].output.count(ItemId::CopperIngot) > 0);
        SELFTEST_CHECK(jm[p].input.count(ItemId::CopperOre) < 8);

        // A belt facing a machine that can take no more KEEPS its cargo. This
        // is the whole of what makes a feed line back up: beltStep already
        // stalls on a refusal, so capacity needed no belt code of its own.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> bm;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> bb;
        const glm::ivec3 mp{64, 20, 64};
        bm[mp].type = BlockId::Furnace;
        bm[mp].input.add(ItemId::CopperOre, vg::kMachineInputCap); // input full
        const glm::ivec3 bp = mp - glm::ivec3(0, 0, 1);
        bb[bp].facing = {0, 0, 1}; // pointing at the furnace
        bb[bp].item = ItemId::CopperOre;
        MachineSystem::beltStep(bb, bm);
        SELFTEST_CHECK(bb[bp].item == ItemId::CopperOre); // stalled, not voided
        SELFTEST_CHECK(bm[mp].input.count(ItemId::CopperOre) == vg::kMachineInputCap);

        // Room for one, and it moves again.
        bm[mp].input.remove(ItemId::CopperOre, 1);
        MachineSystem::beltStep(bb, bm);
        SELFTEST_CHECK(bb[bp].item == ItemId::None);
        SELFTEST_CHECK(bm[mp].input.count(ItemId::CopperOre) == vg::kMachineInputCap);
    }

    // ---- A crate is feedable, drainable, and therefore also the splitter ----
    // beltStep fills a machine's `input` and drains its `output`, so a crate is
    // one buffer migration and no belt code. The second half is why the roadmap
    // never needed a separate splitter block: every belt pointing AWAY from a
    // crate pulls from it independently, so one line in feeds two lines out.
    {
        World kw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> km;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> kb;
        PowerState dead; // a crate draws no power and is not a power node
        SELFTEST_CHECK(!PowerSystem::isPowerNode(BlockId::StorageCrate));

        const glm::ivec3 c{70, 20, 70};
        kw.setBlock(c.x, c.y, c.z, BlockId::StorageCrate);
        km[c].type = BlockId::StorageCrate;

        // In from the north, out to east and west.
        const glm::ivec3 in = c - glm::ivec3(0, 0, 1);
        kb[in].facing = {0, 0, 1};
        kb[in].item = ItemId::CopperOre;
        const glm::ivec3 outE = c + glm::ivec3(1, 0, 0);
        const glm::ivec3 outW = c - glm::ivec3(1, 0, 0);
        kb[outE].facing = {1, 0, 0};
        kb[outW].facing = {-1, 0, 0};

        std::uint32_t rc = 0;
        MachineSystem::beltStep(kb, km);
        SELFTEST_CHECK(kb[in].item == ItemId::None);          // a crate takes anything
        SELFTEST_CHECK(km[c].input.count(ItemId::CopperOre) == 1);

        // The migration is the whole behaviour: what was fed in becomes stock.
        MachineSystem::tickPowered(kw, km, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(km[c].input.count(ItemId::CopperOre) == 0);
        SELFTEST_CHECK(km[c].output.count(ItemId::CopperOre) == 1);

        // Stock it properly, then prove BOTH outgoing belts draw from it in the
        // same step -- one item each, which is an even split with no splitter.
        km[c].output.add(ItemId::CopperOre, 9); // 10 in the crate
        MachineSystem::beltStep(kb, km);
        SELFTEST_CHECK(kb[outE].item == ItemId::CopperOre);
        SELFTEST_CHECK(kb[outW].item == ItemId::CopperOre);
        SELFTEST_CHECK(km[c].output.count(ItemId::CopperOre) == 8);

        // And it is deep: a crate has to hold far more than the machine whose
        // jam it exists to relieve, or nobody would walk over to build one.
        SELFTEST_CHECK(MachineSystem::inputCap(km[c]) == vg::kChestCap);
        SELFTEST_CHECK(vg::kChestCap > vg::kMachineOutputCap);
        km[c].output.add(ItemId::Stone, vg::kChestCap);
        SELFTEST_CHECK(!MachineSystem::machineAccepts(km[c], ItemId::Stone)); // full
        SELFTEST_CHECK(MachineSystem::machineAccepts(km[c], ItemId::Wood));   // room yet
    }

    // ---- Belt filters: the sorting half -----------------------------------
    // What this replaced was beltStep draining a mixed output by lowest ItemId
    // ordinal -- a rule no player could see or be taught. These checks pin both
    // directions of the filter, because only the pair makes a sorting LANE:
    // pull only your item, and refuse to accept anything else.
    {
        World fw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> fm;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> fb;

        // A crate holding two things, with a filtered belt out of each side.
        const glm::ivec3 c{80, 20, 80};
        fw.setBlock(c.x, c.y, c.z, BlockId::StorageCrate);
        fm[c].type = BlockId::StorageCrate;
        fm[c].output.add(ItemId::CopperOre, 5);
        fm[c].output.add(ItemId::Stone, 5);

        const glm::ivec3 east = c + glm::ivec3(1, 0, 0);
        const glm::ivec3 west = c - glm::ivec3(1, 0, 0);
        fb[east].facing = {1, 0, 0};
        fb[east].filter = ItemId::Stone;      // deliberately NOT the low ordinal
        fb[west].facing = {-1, 0, 0};
        fb[west].filter = ItemId::CopperOre;

        MachineSystem::beltStep(fb, fm);
        SELFTEST_CHECK(fb[east].item == ItemId::Stone);
        SELFTEST_CHECK(fb[west].item == ItemId::CopperOre);
        SELFTEST_CHECK(fm[c].output.count(ItemId::Stone) == 4);
        SELFTEST_CHECK(fm[c].output.count(ItemId::CopperOre) == 4);

        // A filter that names something the machine hasn't got pulls NOTHING --
        // it does not fall back to "whatever is there", which would quietly
        // undo the whole point.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> gm;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> gb;
        const glm::ivec3 g{84, 20, 84};
        gm[g].type = BlockId::StorageCrate;
        gm[g].output.add(ItemId::Stone, 3);
        const glm::ivec3 gout = g + glm::ivec3(1, 0, 0);
        gb[gout].facing = {1, 0, 0};
        gb[gout].filter = ItemId::IronIngot; // none in there
        MachineSystem::beltStep(gb, gm);
        SELFTEST_CHECK(gb[gout].item == ItemId::None);
        SELFTEST_CHECK(gm[g].output.count(ItemId::Stone) == 3);

        // Belt -> belt: a filtered belt refuses cargo it is not for, and the
        // line behind it holds rather than losing the item.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> hm;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> hb;
        const glm::ivec3 a{90, 20, 90};
        const glm::ivec3 nextBelt = a + glm::ivec3(0, 0, 1);
        hb[a].facing = {0, 0, 1};
        hb[a].item = ItemId::Stone;
        hb[nextBelt].facing = {0, 0, 1};
        hb[nextBelt].filter = ItemId::CopperOre; // not stone
        MachineSystem::beltStep(hb, hm);
        SELFTEST_CHECK(hb[a].item == ItemId::Stone); // stalled, not voided
        SELFTEST_CHECK(hb[nextBelt].item == ItemId::None);

        // Matching cargo passes.
        hb[nextBelt].filter = ItemId::Stone;
        MachineSystem::beltStep(hb, hm);
        SELFTEST_CHECK(hb[a].item == ItemId::None);
        SELFTEST_CHECK(hb[nextBelt].item == ItemId::Stone);

        // An unfiltered belt still carries anything -- the fallback has to
        // stay, or every existing factory would stop on load.
        hb[nextBelt].filter = ItemId::None;
        hb[a].item = ItemId::IronIngot;
        MachineSystem::beltStep(hb, hm);
        SELFTEST_CHECK(hb[nextBelt].item == ItemId::Stone); // still occupied this step
        SELFTEST_CHECK(hb[a].item == ItemId::IronIngot);
    }

    // ---- Tubes: a conduit's shape is a property of its CELL ----------------
    // The arms are parts of one baked hub, shown per neighbour, so these masks
    // ARE the geometry. A rule that lived inside the mesher could only be
    // checked by looking at the screen; this is why TubeShape is free
    // functions over the registries instead.
    {
        using TubeShape::conduitArms;
        using TubeShape::wireArms;
        const auto air = [] {
            TubeShape::Neighbours n;
            n.fill(BlockId::Air);
            return n;
        };
        // Face indices, from BlockShape.h's kShapeFaceDirs.
        constexpr int kEast = 0, kWest = 1, kUp = 2, kDown = 3;
        constexpr int kSouth = 4, kNorth = 5;
        const auto bit = [](int f) { return static_cast<std::uint8_t>(1u << f); };

        SELFTEST_CHECK(TubeShape::faceIndex({1, 0, 0}) == kEast);
        SELFTEST_CHECK(TubeShape::faceIndex({0, 0, -1}) == kNorth);
        SELFTEST_CHECK(TubeShape::faceIndex({1, 1, 0}) == -1); // not a cardinal

        std::unordered_map<glm::ivec3, Belt, IVec3Hash> tb;
        const glm::ivec3 c{200, 20, 200};

        // A lone conduit still states its direction: the OUT arm is drawn into
        // open air, because it is what replaced the top-face arrow.
        Belt east;
        east.facing = {1, 0, 0};
        tb[c] = east;
        SELFTEST_CHECK(conduitArms(c, east, tb, air()) == bit(kEast));

        // A straight run: the belt behind aims at us, so we grow an arm back
        // toward it -- two arms, one continuous pipe.
        tb[c - glm::ivec3(1, 0, 0)] = east; // behind, pointing our way
        SELFTEST_CHECK(conduitArms(c, east, tb, air()) == (bit(kEast) | bit(kWest)));

        // A belt alongside that does NOT aim at us shares no arm: nothing
        // passes between two parallel lanes, and the picture should say so.
        tb[c + glm::ivec3(0, 0, 1)] = east;
        SELFTEST_CHECK(conduitArms(c, east, tb, air()) == (bit(kEast) | bit(kWest)));

        // ...but turn that neighbour to face us and it becomes a junction.
        Belt intoUs;
        intoUs.facing = {0, 0, -1}; // from +Z back toward us
        tb[c + glm::ivec3(0, 0, 1)] = intoUs;
        SELFTEST_CHECK(conduitArms(c, east, tb, air()) ==
                       (bit(kEast) | bit(kWest) | bit(kSouth)));

        // A corner: cargo arrives from -Z and leaves east. Two arms on
        // perpendicular faces, which is what makes a corner look like one.
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> cb;
        Belt fromNorth;
        fromNorth.facing = {0, 0, 1}; // sits at -Z, pushes toward +Z
        cb[c - glm::ivec3(0, 0, 1)] = fromNorth;
        SELFTEST_CHECK(conduitArms(c, east, cb, air()) == (bit(kEast) | bit(kNorth)));

        // A machine BEHIND earns an arm, because beltStep pulls out of it.
        TubeShape::Neighbours nb = air();
        nb[kWest] = BlockId::Furnace;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> mb;
        SELFTEST_CHECK(conduitArms(c, east, mb, nb) == (bit(kEast) | bit(kWest)));

        // A machine anywhere else is not on this belt's path at all: it neither
        // feeds it nor takes from it, so no arm.
        TubeShape::Neighbours side = air();
        side[kUp] = BlockId::Furnace;
        SELFTEST_CHECK(conduitArms(c, east, mb, side) == bit(kEast));

        // Wire connects to exactly what the power solver floods into -- the
        // same predicate, so the picture can never lie about the network.
        TubeShape::Neighbours wn = air();
        wn[kEast] = BlockId::Wire;
        wn[kWest] = BlockId::Generator;
        wn[kUp] = BlockId::Grinder;      // demand > 0
        wn[kDown] = BlockId::Stone;      // not a node
        wn[kNorth] = BlockId::RainBarrel; // a machine, but demand 0
        SELFTEST_CHECK(wireArms(wn) == (bit(kEast) | bit(kWest) | bit(kUp)));
        SELFTEST_CHECK(wireArms(air()) == 0);
    }

    // ---- ...and every arm really does point where its name says ------------
    // kConnectParts binds a part NAME to a face. The static_assert proves the
    // name exists; only geometry can prove it is the right lump. A mirrored or
    // re-authored model would otherwise connect correctly and point backwards.
    {
        for (const ConnectPart& cp : kConnectParts) {
            const BlockShape& sh = blockShape(cp.shape);
            const int part = partIndex(cp.shape, cp.part);
            SELFTEST_CHECK(part >= 0);

            glm::vec3 lo(2.0f), hi(-1.0f);
            int quads = 0;
            for (const ShapeQuad& q : sh.quads) {
                if (q.part != static_cast<std::uint8_t>(part)) continue;
                ++quads;
                for (const glm::vec3& v : q.pos) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
            }
            SELFTEST_CHECK(quads > 0); // an arm nothing draws is a dead row

            // The arm must actually reach the wall it names, and must not
            // sprawl to the opposite one.
            const glm::ivec3 d = kShapeFaceDirs[cp.face];
            const int axis = d.x ? 0 : (d.y ? 1 : 2);
            const int sign = d.x + d.y + d.z;
            SELFTEST_CHECK(sign > 0 ? (hi[axis] > 0.98f) : (lo[axis] < 0.02f));
            SELFTEST_CHECK(sign > 0 ? (lo[axis] > 0.02f) : (hi[axis] < 0.98f));
        }
    }

    // ---- ...and a connected shape collides with no arm --------------------
    // Arms are drawn per neighbour but collision cannot see the neighbours
    // (a conduit's depend on belt facings), so its boxes must stay inside the
    // geometry that is ALWAYS drawn -- or you stand on, and aim at, air.
    {
        for (std::size_t si = 0; si < static_cast<std::size_t>(ShapeId::Count); ++si) {
            const ShapeId id = static_cast<ShapeId>(si);
            if (!shapeConnects(id)) continue;
            const BlockShape& sh = blockShape(id);
            glm::vec3 lo(2.0f), hi(-1.0f);
            for (const ShapeQuad& q : sh.quads) {
                if (partFace(id, q.part) >= 0) continue;
                for (const glm::vec3& v : q.pos) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
            }
            SELFTEST_CHECK(!sh.boxes.empty());
            for (const ShapeAabb& b : sh.boxes) {
                SELFTEST_CHECK(glm::all(glm::greaterThanEqual(b.lo, lo - 1e-4f)) &&
                               glm::all(glm::lessThanEqual(b.hi, hi + 1e-4f)));
            }
        }
    }

    // ---- Cargo slides, and only when it actually moved --------------------
    // The visual is one belt step behind the simulation, which is what makes
    // it always right: predicting the next hop would snap back whenever a belt
    // lost a claim to another belt feeding the same cell.
    {
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> fm;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> fb;
        const glm::ivec3 a{300, 20, 300};
        const glm::ivec3 bpos = a + glm::ivec3(1, 0, 0);
        fb[a].facing = {1, 0, 0};
        fb[a].item = ItemId::Stone;
        fb[bpos].facing = {1, 0, 0};

        MachineSystem::beltStep(fb, fm);
        SELFTEST_CHECK(fb[bpos].item == ItemId::Stone);
        // Recorded on the RECEIVER and pointing back the way it came, so the
        // render lerps from the cell behind into this one.
        SELFTEST_CHECK(fb[bpos].cameFrom == glm::ivec3(-1, 0, 0));
        SELFTEST_CHECK(fb[a].item == ItemId::None);

        // Nothing ahead: the item sits still and the record clears, so a
        // stalled line parks its cargo instead of replaying the last slide.
        MachineSystem::beltStep(fb, fm);
        SELFTEST_CHECK(fb[bpos].item == ItemId::Stone);
        SELFTEST_CHECK(fb[bpos].cameFrom == glm::ivec3(0));

        // A pull out of a machine behind is motion too, and from the same
        // direction the arm points.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> pm2;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> pb;
        const glm::ivec3 mp2{310, 20, 310};
        pm2[mp2].type = BlockId::Furnace;
        pm2[mp2].output.add(ItemId::CopperIngot, 1);
        const glm::ivec3 out2 = mp2 + glm::ivec3(0, 0, 1);
        pb[out2].facing = {0, 0, 1};
        MachineSystem::beltStep(pb, pm2);
        SELFTEST_CHECK(pb[out2].item == ItemId::CopperIngot);
        SELFTEST_CHECK(pb[out2].cameFrom == glm::ivec3(0, 0, -1));

        // A corner turns: the direction cargo ARRIVED from is not the
        // direction it will leave by, which is why the record lives on the
        // receiver rather than being derived from its own facing.
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> cm;
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> cb2;
        const glm::ivec3 feed{320, 20, 320};
        const glm::ivec3 corner = feed + glm::ivec3(0, 0, 1);
        cb2[feed].facing = {0, 0, 1};
        cb2[feed].item = ItemId::Stone;
        cb2[corner].facing = {1, 0, 0}; // turns east
        MachineSystem::beltStep(cb2, cm);
        SELFTEST_CHECK(cb2[corner].item == ItemId::Stone);
        SELFTEST_CHECK(cb2[corner].cameFrom == glm::ivec3(0, 0, -1));
    }

    // ---- The master switch: off means FROZEN, not broken -------------------
    // "Off" has to mean the same thing to four different systems at once (the
    // tick, the power solve, the glow, the belts), and the easy bugs are all
    // half-measures: a machine that stops working but still browns out its
    // network, or a generator that stops producing but still counts as lit.
    {
        World sw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> sm;
        PowerState dead;
        const glm::ivec3 p{100, 20, 100};
        sw.setBlock(p.x, p.y, p.z, BlockId::Furnace);
        sm[p].type = BlockId::Furnace;
        sm[p].input.add(ItemId::CopperOre, 8);
        sm[p].fuel.add(ItemId::Charcoal, 4);

        // Off: no product, no ore eaten, no fuel burned, however long it sits.
        sm[p].enabled = false;
        std::uint32_t rc = 0;
        for (int i = 0; i < 400; ++i) MachineSystem::tickPowered(sw, sm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(sm[p].output.count(ItemId::CopperIngot) == 0);
        SELFTEST_CHECK(sm[p].input.count(ItemId::CopperOre) == 8);
        SELFTEST_CHECK(sm[p].fuel.count(ItemId::Charcoal) == 4);
        SELFTEST_CHECK(!sm[p].crafting); // and draws no progress bar

        // ...but it is a PAUSE, not a reset: the buffers are still there and it
        // picks straight back up.
        sm[p].enabled = true;
        for (int i = 0; i < 400; ++i) MachineSystem::tickPowered(sw, sm, dead, 1u, rc, noCrops);
        SELFTEST_CHECK(sm[p].output.count(ItemId::CopperIngot) > 0);

        // An off machine still ACCEPTS and still gives up its output. This is
        // what makes the switch a logistics tool instead of a wall: the feed
        // line backs up on its own once the input hits its cap, with no special
        // case in beltStep at all.
        sm[p].enabled = false;
        SELFTEST_CHECK(MachineSystem::machineAccepts(sm[p], ItemId::CopperOre));
        std::unordered_map<glm::ivec3, Belt, IVec3Hash> sb;
        const glm::ivec3 drain = p + glm::ivec3(1, 0, 0);
        sb[drain].facing = {1, 0, 0};
        const int had = sm[p].output.count(ItemId::CopperIngot);
        MachineSystem::beltStep(sb, sm);
        SELFTEST_CHECK(sb[drain].item == ItemId::CopperIngot);
        SELFTEST_CHECK(sm[p].output.count(ItemId::CopperIngot) == had - 1);
    }

    // ---- ...and the power network agrees ----------------------------------
    {
        World pw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> pm;
        // A generator wired to a grinder: the smallest network with both a
        // producer and a consumer.
        const glm::ivec3 gen{110, 20, 110};
        const glm::ivec3 wire = gen + glm::ivec3(1, 0, 0);
        const glm::ivec3 mac = gen + glm::ivec3(2, 0, 0);
        pw.setBlock(gen.x, gen.y, gen.z, BlockId::Generator);
        pw.setBlock(wire.x, wire.y, wire.z, BlockId::Wire);
        pw.setBlock(mac.x, mac.y, mac.z, BlockId::Grinder);
        pm[gen].type = BlockId::Generator;
        pm[gen].progress = 10.0f; // burning
        pm[mac].type = BlockId::Grinder;

        PowerState st = PowerSystem::solve(pw, pm, nullptr);
        SELFTEST_CHECK(st.energized(mac.x, mac.y, mac.z));
        SELFTEST_CHECK(st.energized(wire.x, wire.y, wire.z));

        // Switch the CONSUMER off: it goes dark, but the wire between them
        // stays live -- an off machine must never split a network, or idling
        // one would black out everything downstream.
        pm[mac].enabled = false;
        st = PowerSystem::solve(pw, pm, nullptr);
        SELFTEST_CHECK(!st.energized(mac.x, mac.y, mac.z));
        SELFTEST_CHECK(st.energized(wire.x, wire.y, wire.z));
        SELFTEST_CHECK(st.energized(gen.x, gen.y, gen.z));

        // An off consumer also stops DEMANDING, so its generator is no longer
        // hungry and stops lighting fresh fuel.
        std::unordered_set<glm::ivec3, IVec3Hash> hungry;
        PowerSystem::solve(pw, pm, &hungry);
        SELFTEST_CHECK(hungry.empty());
        pm[mac].enabled = true;
        PowerSystem::solve(pw, pm, &hungry);
        SELFTEST_CHECK(hungry.count(gen) > 0);

        // Switch the PRODUCER off instead: it stops producing, so the machine
        // it fed goes dark too even though that machine is still on.
        pm[gen].enabled = false;
        st = PowerSystem::solve(pw, pm, nullptr);
        SELFTEST_CHECK(!st.energized(mac.x, mac.y, mac.z));
        SELFTEST_CHECK(!st.energized(gen.x, gen.y, gen.z));
    }

    // ---- Farming: the hoe, and what a plant will sit on --------------------
    {
        World fw;
        MachineSystem::MachineMap fm;
        MachineSystem::BeltMap fb;
        std::unordered_map<glm::ivec3, float, IVec3Hash> fs, fsap;
        CropSystem::CropMap fc;
        const WorldEdit::Registries fr{fm, fb, fs, fsap, fc};

        const glm::ivec3 g{200, 30, 200};
        fw.setBlock(g.x, g.y, g.z, BlockId::Grass);

        // Tilling is a tool RMB transmuting the aimed cell -- the fuseSources
        // shape -- and it only works on plain ground.
        SELFTEST_CHECK(WorldEdit::tillSoil(fw, g));
        SELFTEST_CHECK(fw.getBlock(g.x, g.y, g.z) == BlockId::TilledSoil);
        SELFTEST_CHECK(!WorldEdit::tillSoil(fw, g)); // already worked: no-op
        const glm::ivec3 rock = g + glm::ivec3(1, 0, 0);
        fw.setBlock(rock.x, rock.y, rock.z, BlockId::Stone);
        SELFTEST_CHECK(!WorldEdit::tillSoil(fw, rock)); // stone is not ground

        // Tilling under a placed block would strand it on soil it no longer
        // sits on, so a covered cell refuses.
        const glm::ivec3 covered = g + glm::ivec3(0, 0, 1);
        fw.setBlock(covered.x, covered.y, covered.z, BlockId::Grass);
        fw.setBlock(covered.x, covered.y + 1, covered.z, BlockId::Stone);
        SELFTEST_CHECK(!WorldEdit::tillSoil(fw, covered));

        // A sapling wants soil, and TILLED ground still counts -- `provides`
        // and `needsSoil` are ordered, so worked ground satisfies a plant that
        // only asked for dirt without anything having to say so.
        const glm::ivec3 above = g + glm::ivec3(0, 1, 0);
        SELFTEST_CHECK(WorldEdit::placeBlock(fw, fr, above, BlockId::Sapling, {}).placed);
        SELFTEST_CHECK(fsap.count(above) == 1);

        // ...but stone is not soil, and the refusal is a silent no-op.
        const glm::ivec3 onRock = rock + glm::ivec3(0, 1, 0);
        SELFTEST_CHECK(!WorldEdit::placeBlock(fw, fr, onRock, BlockId::Sapling, {}).placed);
        SELFTEST_CHECK(fw.getBlock(onRock.x, onRock.y, onRock.z) == BlockId::Air);
    }

    // ---- Farming: crops ripen, on tilled soil only -------------------------
    {
        World cw;
        MachineSystem::MachineMap cm;
        MachineSystem::BeltMap cb;
        std::unordered_map<glm::ivec3, float, IVec3Hash> cs, csap;
        CropSystem::CropMap field;
        const WorldEdit::Registries cr{cm, cb, cs, csap, field};

        const glm::ivec3 soil{210, 30, 210};
        const glm::ivec3 plant = soil + glm::ivec3(0, 1, 0);
        cw.setBlock(soil.x, soil.y, soil.z, BlockId::TilledSoil);

        // A seed refuses plain dirt: a field is laid out on purpose.
        const glm::ivec3 dirt{212, 30, 210};
        cw.setBlock(dirt.x, dirt.y, dirt.z, BlockId::Dirt);
        SELFTEST_CHECK(!WorldEdit::placeBlock(cw, cr, dirt + glm::ivec3(0, 1, 0),
                                              BlockId::HerbCrop0, {}).placed);

        SELFTEST_CHECK(WorldEdit::placeBlock(cw, cr, plant, BlockId::HerbCrop0, {}).placed);
        SELFTEST_CHECK(field.count(plant) == 1);

        // One stage per kCropStageSeconds. Run three stages' worth plus slack
        // and the plant must be RIPE and no further -- ripe is the end of the
        // line, so it waits to be picked rather than looping round.
        const int perStage = static_cast<int>(vg::kCropStageSeconds / vg::kTickSeconds) + 1;
        for (int i = 0; i < perStage; ++i) CropSystem::tick(cw, field, false);
        SELFTEST_CHECK(cw.getBlock(plant.x, plant.y, plant.z) == BlockId::HerbCrop1);
        for (int i = 0; i < perStage * 8; ++i) CropSystem::tick(cw, field, false);
        SELFTEST_CHECK(cw.getBlock(plant.x, plant.y, plant.z) == BlockId::HerbCrop3);
        SELFTEST_CHECK(CropSystem::isRipe(cw.getBlock(plant.x, plant.y, plant.z)));

        // Rain is the rate. The same ticks get a second plant further along.
        const glm::ivec3 wetSoil{214, 30, 210};
        const glm::ivec3 wet = wetSoil + glm::ivec3(0, 1, 0);
        cw.setBlock(wetSoil.x, wetSoil.y, wetSoil.z, BlockId::TilledSoil);
        SELFTEST_CHECK(WorldEdit::placeBlock(cw, cr, wet, BlockId::HerbCrop0, {}).placed);
        for (int i = 0; i < perStage; ++i) CropSystem::tick(cw, field, true);
        SELFTEST_CHECK(CropSystem::stageOf(cw.getBlock(wet.x, wet.y, wet.z)) > 1);

        // Dig the soil out from under a crop and it dies rather than ripening
        // in mid-air -- and the registry entry goes with it, so a field cannot
        // leak timers for plants that are not there.
        cw.setBlock(wetSoil.x, wetSoil.y, wetSoil.z, BlockId::Air);
        for (int i = 0; i < perStage; ++i) CropSystem::tick(cw, field, false);
        SELFTEST_CHECK(cw.getBlock(wet.x, wet.y, wet.z) == BlockId::Air);
        SELFTEST_CHECK(field.count(wet) == 0);

        // Breaking a crop unregisters it and hands back what it was worth:
        // the seed while it is growing, the herb once it is ripe.
        SELFTEST_CHECK(WorldEdit::breakBlock(cw, cr, plant).drop.id == ItemId::Herb);
        SELFTEST_CHECK(field.count(plant) == 0);
    }

    // ---- The pebble route must stay a fallback, never a shortcut ----------
    // hand/pebble-stone exists for content::validate()'s renewability closure:
    // EVERY other producer of Stone costs Stone (compactor/stone needs a
    // Compactor or a Tamper, and the Tamper, the Bloomery and the Circle all
    // cost Stone), so without a route off dug topsoil the whole tech tree
    // rests on whatever the island happened to bury. See checkRenewability.
    //
    // The price of having it is that Stone is gated at kTierWood while this is
    // a HAND recipe, so it hands you a gated block with no pickaxe at all.
    // What stops that being a shortcut around the gate is not a rule anywhere
    // -- it is arithmetic, spread across four registry rows nobody edits
    // together. Dirt's hardness, the pickaxe's miningSpeed, this recipe's
    // price, or the pebble drop rate could each flip it in silence, and no
    // other check would notice: reachability and renewability both come back
    // greener the CHEAPER this recipe gets. So the ordering is pinned here.
    {
        const auto handRecipe = [](const std::string& key) -> const Recipe* {
            for (const Recipe& r : handcraftRecipes()) {
                if (r.key == key) return &r;
            }
            return nullptr;
        };
        const auto costOf = [](const Recipe& r, ItemId want) {
            for (const ItemStack& in : r.inputs) {
                if (in.id == want) return in.count;
            }
            return 0;
        };

        const Recipe* stone = handRecipe("hand/pebble-stone");
        const Recipe* pick = handRecipe("hand/wood-pickaxe");
        SELFTEST_CHECK(stone != nullptr);
        SELFTEST_CHECK(pick != nullptr);

        const int price = costOf(*stone, ItemId::Pebble);
        SELFTEST_CHECK(price > 0);

        // A pebble comes off dug topsoil, every time, and Dirt is ungated --
        // so one pebble costs exactly one bare-handed dig. The real loop also
        // pays to re-place the dirt it dug, which this deliberately ignores:
        // the conservative form is the one worth pinning.
        const float loopPerStone = static_cast<float>(price) * blockHardness(BlockId::Dirt);
        // The road the gate intends, mirroring breakSeconds(): the right tool
        // CLASS at the block's TIER divides the by-hand time by its speed.
        SELFTEST_CHECK(itemTool(ItemId::WoodPickaxe) == blockTool(BlockId::Stone));
        SELFTEST_CHECK(itemTier(ItemId::WoodPickaxe) >= blockToolTier(BlockId::Stone));
        const float minedPerStone =
            blockHardness(BlockId::Stone) / itemMiningSpeed(ItemId::WoodPickaxe);

        // Grinding pebbles must be the SLOWER road, or the tool gate on Stone
        // is decorative and the wood tier can be skipped outright.
        SELFTEST_CHECK(loopPerStone > minedPerStone);
        // ...and the pickaxe that beats it must cost less than a single Stone
        // does, or there is a window at the very start where grinding wins
        // anyway, because the tool that would beat it is out of reach.
        SELFTEST_CHECK(costOf(*pick, ItemId::Pebble) < price);
    }

    // ---- A grafted sapling grows a bigger tree ----------------------------
    // Which tree a sapling becomes is a REGISTRY field, not a second
    // `id == BlockId::Sapling` in WorldEdit -- that hardcode is the one
    // SoilKind exists to have removed. The consequence worth pinning is that
    // the block carries the kind, which is why the sapling registry is still a
    // plain pos -> float and the save format did not move.
    {
        SELFTEST_CHECK(blockInfo(BlockId::Sapling).treeSize == 1);
        SELFTEST_CHECK(blockInfo(BlockId::SaplingGrafted).treeSize == 2);
        // Exactly two saplings, or the "is this a sapling" test above silently
        // starts matching something that has no business growing.
        int growers = 0;
        for (const BlockInfo& b : blockRows()) {
            if (b.treeSize > 0) ++growers;
        }
        SELFTEST_CHECK(growers == 2);

        // Both shapes root at the sapling's own cell, which is what lets
        // updateSaplings skip `cell != pos` when it checks for clear space.
        SELFTEST_CHECK(vg::treeCells(1).front().offset == glm::ivec3(0, 0, 0));
        SELFTEST_CHECK(vg::treeCells(2).front().offset == glm::ivec3(0, 0, 0));
        SELFTEST_CHECK(vg::treeCells(2).size() > vg::treeCells(1).size());
        // An out-of-range size CLAMPS rather than indexing past the shapes --
        // a content pack may write any integer into treeSize.
        SELFTEST_CHECK(vg::treeCells(99).size() == vg::treeCells(2).size());
        SELFTEST_CHECK(vg::treeCells(0).size() == vg::treeCells(1).size());

        World tw;
        MachineSystem::MachineMap tm;
        MachineSystem::BeltMap tb;
        std::unordered_map<glm::ivec3, float, IVec3Hash> ts, tsap;
        CropSystem::CropMap tc;
        const WorldEdit::Registries tr{tm, tb, ts, tsap, tc};

        const glm::ivec3 soil{240, 30, 240};
        const glm::ivec3 seat = soil + glm::ivec3(0, 1, 0);
        tw.setBlock(soil.x, soil.y, soil.z, BlockId::Grass);
        SELFTEST_CHECK(
            WorldEdit::placeBlock(tw, tr, seat, BlockId::SaplingGrafted, {}).placed);
        SELFTEST_CHECK(tsap.count(seat) == 1); // registered by treeSize, not by id
        SELFTEST_CHECK(WorldEdit::breakBlock(tw, tr, seat).drop.id ==
                       ItemId::GraftedSaplingItem);
        SELFTEST_CHECK(tsap.count(seat) == 0);

        // The bigger tree really is bigger where it counts: more logs (wood)
        // and more leaves (sticks and the next saplings), on one plot.
        const auto count = [](int size, BlockId want) {
            World w;
            vg::placeTree(w, {0, 40, 0}, size);
            int n = 0;
            for (const vg::TreeCell& c : vg::treeCells(size)) {
                if (w.getBlock(c.offset.x, 40 + c.offset.y, c.offset.z) == want) ++n;
            }
            return n;
        };
        SELFTEST_CHECK(count(2, BlockId::Log) > count(1, BlockId::Log));
        SELFTEST_CHECK(count(2, BlockId::Leaves) > count(1, BlockId::Leaves));
    }

    // ---- Rich Soil is worked ground one rung further up --------------------
    // Compost is the tree's surplus arriving in the field, and enriching is the
    // hoe's shape a step later: a held item RMB transmuting the aimed cell. The
    // ladder is Soil -> Tilled -> Rich, and it only climbs.
    {
        World rw;
        MachineSystem::MachineMap rm;
        MachineSystem::BeltMap rb;
        std::unordered_map<glm::ivec3, float, IVec3Hash> rs, rsap;
        CropSystem::CropMap rc;
        const WorldEdit::Registries rr{rm, rb, rs, rsap, rc};

        const glm::ivec3 g{220, 30, 220};
        rw.setBlock(g.x, g.y, g.z, BlockId::Grass);

        // Compost is not a hoe: it works worked ground, and nothing else. This
        // is the refusal the deny line has to distinguish, since "wrong block"
        // and "already rich" are different mistakes.
        SELFTEST_CHECK(!WorldEdit::enrichSoil(rw, g));
        SELFTEST_CHECK(WorldEdit::tillSoil(rw, g));
        SELFTEST_CHECK(WorldEdit::enrichSoil(rw, g));
        SELFTEST_CHECK(rw.getBlock(g.x, g.y, g.z) == BlockId::RichSoil);
        SELFTEST_CHECK(!WorldEdit::enrichSoil(rw, g)); // already rich: a no-op
        // ...and the ladder does not run backwards: rich ground is not plain
        // soil, so the hoe has nothing to do with it.
        SELFTEST_CHECK(!WorldEdit::tillSoil(rw, g));

        // Same reason tilling refuses a covered cell: enriching under a placed
        // block would strand it on ground it no longer sits on.
        const glm::ivec3 covered = g + glm::ivec3(0, 0, 1);
        rw.setBlock(covered.x, covered.y, covered.z, BlockId::TilledSoil);
        rw.setBlock(covered.x, covered.y + 1, covered.z, BlockId::Stone);
        SELFTEST_CHECK(!WorldEdit::enrichSoil(rw, covered));

        // Rich satisfies everything tilled ground does, for free, because
        // `provides >= needsSoil` is ordered -- a crop AND a sapling, neither
        // of which had to be told about the new rung.
        SELFTEST_CHECK(soilAccepts(BlockId::RichSoil, BlockId::HerbCrop0));
        SELFTEST_CHECK(soilAccepts(BlockId::RichSoil, BlockId::Sapling));
        // The rungs below are unchanged, which is the other half of the claim.
        SELFTEST_CHECK(soilAccepts(BlockId::TilledSoil, BlockId::HerbCrop0));
        SELFTEST_CHECK(!soilAccepts(BlockId::Grass, BlockId::HerbCrop0));
        SELFTEST_CHECK(soilAccepts(BlockId::Grass, BlockId::Sapling));

        // Drops Dirt like the tilled ground it came from: neither tilling nor
        // enriching may be a way to duplicate soil.
        SELFTEST_CHECK(WorldEdit::breakBlock(rw, rr, g).drop.id == ItemId::DirtItem);
    }

    // ---- Nutrition stacks with water; water does not stack with itself -----
    // Rain and irrigation share ONE multiplier because they are the same thing
    // arriving two ways. Rich Soil is a different axis and deliberately DOES
    // stack, so a fed and watered field runs at the product. The last check is
    // the load-bearing one: it pins the rule the new multiplier sits next to,
    // so a later edit cannot read the departure as a licence to stack water.
    {
        // Ticks until ripe, which is monotone in the growth rate -- unlike
        // "stage after N ticks", which piles four different rates up against
        // the same stage-3 ceiling and cannot tell them apart. Each run gets
        // its own world, so a plant from an earlier measurement cannot keep
        // ticking through a later one's weather.
        const auto ticksToRipe = [](BlockId soil, bool rainy, bool irrigate) {
            World w;
            MachineSystem::MachineMap m;
            MachineSystem::BeltMap b;
            std::unordered_map<glm::ivec3, float, IVec3Hash> s, sap;
            CropSystem::CropMap field;
            const WorldEdit::Registries r{m, b, s, sap, field};

            const glm::ivec3 base{230, 30, 230};
            const glm::ivec3 top = base + glm::ivec3(0, 1, 0);
            w.setBlock(base.x, base.y, base.z, soil);
            WorldEdit::placeBlock(w, r, top, BlockId::HerbCrop0, {});
            // The sprinkler sits in the soil cell itself: one below the plant,
            // well inside kIrrigateRadius.
            const std::vector<glm::ivec3> wet =
                irrigate ? std::vector<glm::ivec3>{base} : std::vector<glm::ivec3>{};
            for (int i = 0; i < 20000; ++i) {
                if (CropSystem::isRipe(w.getBlock(top.x, top.y, top.z))) return i;
                CropSystem::tick(w, field, rainy, wet);
            }
            return -1; // never ripened: a failed placement lands here too
        };

        const int dry     = ticksToRipe(BlockId::TilledSoil, false, false);
        const int fed     = ticksToRipe(BlockId::RichSoil,   false, false);
        const int watered = ticksToRipe(BlockId::TilledSoil, true,  false);
        const int both    = ticksToRipe(BlockId::RichSoil,   true,  false);

        SELFTEST_CHECK(dry > 0);
        SELFTEST_CHECK(fed < dry);      // compost is worth something
        SELFTEST_CHECK(watered < fed);  // ...and worth less than rain (2x vs 3x)
        SELFTEST_CHECK(both < watered); // the deliberate departure: they stack

        // The rule the departure sits BESIDE, pinned so a later edit cannot
        // read it as a licence. Irrigating a field it is already raining on
        // must change nothing, and irrigation alone must be exactly rain alone:
        // one multiplier, two ways of earning it.
        SELFTEST_CHECK(ticksToRipe(BlockId::TilledSoil, true, true) == watered);
        SELFTEST_CHECK(ticksToRipe(BlockId::TilledSoil, false, true) == watered);
    }

    // ---- Farming: the Harvester reaps and REPLANTS -------------------------
    {
        World hw;
        MachineSystem::MachineMap hm;
        MachineSystem::BeltMap hb;
        std::unordered_map<glm::ivec3, float, IVec3Hash> hs, hsap;
        CropSystem::CropMap field;
        const WorldEdit::Registries hr{hm, hb, hs, hsap, field};
        PowerState dead;
        std::uint32_t rc = 0;

        const glm::ivec3 mac{220, 30, 220};
        hw.setBlock(mac.x, mac.y, mac.z, BlockId::Harvester);
        hm[mac].type = BlockId::Harvester;

        // One ripe plant and one still growing, both in reach.
        const glm::ivec3 ripeSoil = mac + glm::ivec3(2, 0, 0);
        const glm::ivec3 ripe = ripeSoil + glm::ivec3(0, 1, 0);
        const glm::ivec3 youngSoil = mac + glm::ivec3(-2, 0, 0);
        const glm::ivec3 young = youngSoil + glm::ivec3(0, 1, 0);
        hw.setBlock(ripeSoil.x, ripeSoil.y, ripeSoil.z, BlockId::TilledSoil);
        hw.setBlock(youngSoil.x, youngSoil.y, youngSoil.z, BlockId::TilledSoil);
        hw.setBlock(ripe.x, ripe.y, ripe.z, BlockId::HerbCrop3);
        SELFTEST_CHECK(WorldEdit::placeBlock(hw, hr, young, BlockId::HerbCrop0, {}).placed);

        // An UNPOWERED harvester does nothing: it is a powered machine, because
        // the point of a farm is that it runs while you are elsewhere.
        for (int i = 0; i < 200; ++i) {
            MachineSystem::tickPowered(hw, hm, dead, 1u, rc, field);
        }
        SELFTEST_CHECK(hw.getBlock(ripe.x, ripe.y, ripe.z) == BlockId::HerbCrop3);

        PowerState live;
        live.setEnergized(mac);
        for (int i = 0; i < 200; ++i) {
            MachineSystem::tickPowered(hw, hm, live, 1u, rc, field);
        }
        // The ripe one is banked as Herb...
        SELFTEST_CHECK(hm[mac].output.count(ItemId::Herb) > 0);
        // ...and the cell is a SEEDLING on intact tilled soil, not Air and not
        // bare dirt. Untilling on harvest would mean re-tilling every automated
        // field by hand forever, which is the opposite of automation.
        SELFTEST_CHECK(hw.getBlock(ripe.x, ripe.y, ripe.z) == BlockId::HerbCrop0);
        SELFTEST_CHECK(hw.getBlock(ripeSoil.x, ripeSoil.y, ripeSoil.z) == BlockId::TilledSoil);
        // ...and it is REGISTERED, or the field would reap once and stand still.
        SELFTEST_CHECK(field.count(ripe) == 1);

        // The unripe plant was never touched.
        SELFTEST_CHECK(hw.getBlock(young.x, young.y, young.z) == BlockId::HerbCrop0);

        // A full output jams and holds, like every other machine: no reaping
        // into a bottomless bucket.
        hw.setBlock(ripe.x, ripe.y, ripe.z, BlockId::HerbCrop3);
        hm[mac].hasTarget = false;
        hm[mac].rescanCooldown = 0;
        hm[mac].output.add(ItemId::Herb, 10000);
        for (int i = 0; i < 200; ++i) {
            MachineSystem::tickPowered(hw, hm, live, 1u, rc, field);
        }
        SELFTEST_CHECK(hm[mac].jammed);
        SELFTEST_CHECK(hw.getBlock(ripe.x, ripe.y, ripe.z) == BlockId::HerbCrop3);
    }

    // ---- Farming: irrigation buys weather independence --------------------
    {
        World iw;
        MachineSystem::MachineMap im;
        MachineSystem::BeltMap ib;
        std::unordered_map<glm::ivec3, float, IVec3Hash> is, isap;
        CropSystem::CropMap field;
        const WorldEdit::Registries ir{im, ib, is, isap, field};
        PowerState dead;
        std::uint32_t rc = 0;

        const glm::ivec3 pump{230, 30, 230};
        iw.setBlock(pump.x, pump.y, pump.z, BlockId::Irrigator);
        im[pump].type = BlockId::Irrigator;

        // Dry: no water in, nothing running, so the field is on the slow rate.
        MachineSystem::tickPowered(iw, im, dead, 1u, rc, field);
        SELFTEST_CHECK(MachineSystem::activeIrrigators(im).empty());

        // One Rain Water buys kIrrigateSeconds of wetness. It draws no power at
        // all: what it spends is water, so a Barrel and a belt are the whole
        // supply chain and no grid is needed.
        im[pump].input.add(ItemId::SpringWater, 1);
        MachineSystem::tickPowered(iw, im, dead, 1u, rc, field);
        SELFTEST_CHECK(im[pump].input.count(ItemId::SpringWater) == 0);
        SELFTEST_CHECK(MachineSystem::activeIrrigators(im).size() == 1);

        // A crop in reach grows at the RAIN rate while it runs...
        const glm::ivec3 soil = pump + glm::ivec3(3, 0, 0);
        const glm::ivec3 plant = soil + glm::ivec3(0, 1, 0);
        iw.setBlock(soil.x, soil.y, soil.z, BlockId::TilledSoil);
        SELFTEST_CHECK(WorldEdit::placeBlock(iw, ir, plant, BlockId::HerbCrop0, {}).placed);

        // ...and one out of reach does not, on the very same ticks. Same world,
        // same weather: the only difference is the water.
        const glm::ivec3 farSoil = pump + glm::ivec3(vg::kIrrigateRadius + 4, 0, 0);
        const glm::ivec3 far = farSoil + glm::ivec3(0, 1, 0);
        iw.setBlock(farSoil.x, farSoil.y, farSoil.z, BlockId::TilledSoil);
        SELFTEST_CHECK(WorldEdit::placeBlock(iw, ir, far, BlockId::HerbCrop0, {}).placed);

        const int ticks = static_cast<int>(vg::kCropStageSeconds / vg::kTickSeconds) + 1;
        for (int i = 0; i < ticks; ++i) {
            MachineSystem::tickPowered(iw, im, dead, 1u, rc, field);
            CropSystem::tick(iw, field, /*rainy=*/false,
                             MachineSystem::activeIrrigators(im));
        }
        SELFTEST_CHECK(CropSystem::stageOf(iw.getBlock(plant.x, plant.y, plant.z)) >
                       CropSystem::stageOf(iw.getBlock(far.x, far.y, far.z)));

        // Switching it OFF has to stop the water, not merely stop it drinking.
        im[pump].enabled = false;
        SELFTEST_CHECK(MachineSystem::activeIrrigators(im).empty());
    }

    // ---- Every species' model is actually there -----------------------------
    // The one asset check in here, and it earns the exception. Creature models
    // load leniently by design -- a missing one disables that species with a
    // log line and never crashes -- which meant boss #1 was absent from a fresh
    // clone for weeks while every build stayed green, because its .bbmodel had
    // never been committed. Leniency is right for a player and wrong for CI, so
    // the same files a launch pillar depends on are parsed here (no window, no
    // GL) and a problem fails the build. See CreatureSystem::checkModels.
    {
        const char* base = SDL_GetBasePath(); // owned by SDL; works pre-init
        const std::vector<std::string> problems =
            CreatureSystem::checkModels(base ? std::string(base) : std::string());
        for (const std::string& msg : problems) std::printf("selftest: %s\n", msg.c_str());
        SELFTEST_CHECK(problems.empty());
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
        std::printf("| `%s` | %s | %s |\n", r.key.c_str(), stackList(r.inputs).c_str(),
                    out.c_str());
    }

    std::printf("\n## Machines\n\n");
    for (const MachineTraits& t : machineTraitRows()) {
        if (t.recipeGroup != BlockId::Air) continue; // twins share the rows below
        const auto rows = recipesForMachine(t.block);

        std::printf("\n### %s\n\n", blockName(t.block));
        if (t.burnsFuel) std::printf("Burns fuel. ");
        else if (t.demand > 0) std::printf("Draws %d power. ", t.demand);
        else std::printf("Runs unpowered. ");
        // Name the hand-cranked twin, if it has one: the two tiers run the
        // same rows, so listing them twice would be a lie about the data.
        for (const MachineTraits& twin : machineTraitRows()) {
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
            std::printf("| `%s` | %s | %s | %.1f |\n", r->key.c_str(),
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
        std::printf("| `%s` | %s | %s | %s | %.1f |\n", r.key.c_str(), centre.c_str(),
                    ring.c_str(), out.c_str(), static_cast<double>(r.seconds));
    }

    std::printf("\n## Fuels\n\n| item | seconds |\n|---|---|\n");
    for (const FuelInfo& f : fuelRows()) {
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

    // ---- Content packs, before anything has read the tables ---------------
    // Two ways in, and they are deliberately not the same way.
    //
    // The `packs/` folder beside the executable is the PLAYER'S installation,
    // so it applies to the game and to nothing else. `--pack <file>` is the
    // AUTHOR'S, and applies to whatever it is asked of -- which is what lets a
    // generator run `--pack draft.json --validate` on its own output without
    // installing it anywhere.
    //
    // Keeping the folder out of the headless tools is what stops an installed
    // pack from silently rewriting the answers: --selftest asserts against the
    // content compiled into the build, and --dump-content is the build's own
    // spec. A pack changing either of those out from under CI would be a very
    // confusing failure.
    std::vector<std::string> packs;
    const char* mode = "";
    const char* modelPath = nullptr; // --check-bbmodel's argument
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pack") == 0 && i + 1 < argc) packs.push_back(argv[++i]);
        else if (std::strcmp(argv[i], "--check-bbmodel") == 0) {
            // Matched with or without its argument: falling through to the
            // bare-mode branch below would run this mode with no file.
            mode = argv[i];
            if (i + 1 < argc) modelPath = argv[++i];
        }
        else if (!*mode) mode = argv[i];
    }
    const bool headless = std::strcmp(mode, "--selftest") == 0 ||
                          std::strcmp(mode, "--check-bbmodel") == 0 ||
                          std::strcmp(mode, "--dump-recipes") == 0 ||
                          std::strcmp(mode, "--dump-content") == 0 ||
                          std::strcmp(mode, "--validate") == 0;
    if (packs.empty() && !headless) {
        if (const char* base = SDL_GetBasePath()) { // owned by SDL, do not free
            packs = content::findPacks(std::string(base) + "packs");
        }
    }
    const std::vector<std::string> packProblems = content::applyPacks(packs);
    if (!packProblems.empty() && headless) {
        // A tool was asked about a named pack and the pack is not usable. Say
        // why and stop, rather than quietly answer about the fallback.
        for (const std::string& msg : packProblems) std::printf("%s\n", msg.c_str());
        return 1;
    }

    // Headless save round-trip for CI; runs before any window/GL setup.
    if (std::strcmp(mode, "--selftest") == 0) {
        return runSelfTest();
    }
    // Regenerates RECIPES.md from the live tables; also headless.
    if (std::strcmp(mode, "--dump-recipes") == 0) {
        return dumpRecipes();
    }
    // The whole content set as JSON, by key. The pack format's own spec, its
    // vocabulary, and a worked example -- see ContentPack.h.
    if (std::strcmp(mode, "--dump-content") == 0) {
        const std::string doc = content::dumpContent();
        std::fwrite(doc.data(), 1, doc.size(), stdout);
        return 0;
    }
    // Load a creature .bbmodel with the ENGINE's own loader and report what it
    // made of it -- the ground truth behind tools/modelkit, whose renderer is a
    // replica of BbModel.cpp and could otherwise drift from it unnoticed.
    // Exit 0 only if it loads and carries the clips every creature needs.
    if (std::strcmp(mode, "--check-bbmodel") == 0) {
        if (!modelPath) {
            std::printf("usage: voxel-factory --check-bbmodel <file.bbmodel>\n");
            return 1;
        }
        engine::BbModel model;
        if (!engine::loadBbModel(modelPath, model, vg::kMaxEntityBones)) {
            std::printf("FAILED to load %s (see the log line above)\n", modelPath);
            return 1;
        }
        std::printf("%s: %zu bones, %zu vertices, texture %dx%d\n", modelPath,
                    model.bones.size(), model.vertexData.size() / 9,
                    model.texture.width, model.texture.height);
        for (const engine::BbBone& b : model.bones) {
            std::printf("  bone %-16s parent %2d  pivot (%.3f, %.3f, %.3f)\n", b.name.c_str(),
                        b.parent, b.pivot.x, b.pivot.y, b.pivot.z);
        }
        for (const engine::BbAnimation& a : model.animations) {
            std::printf("  clip %-28s %.2fs %s, %zu tracks\n", a.name.c_str(), a.length,
                        a.loop ? "loop" : "once", a.tracks.size());
        }
        bool ok = true;
        for (const char* need : {"idle", "walk"}) {
            if (model.findAnimation(need) < 0) {
                std::printf("  MISSING required clip '%s'\n", need);
                ok = false;
            }
        }
        return ok ? 0 : 1;
    }
    // Is the content set coherent? The --selftest checks that are about
    // CONTENT rather than about code, on their own and without the save
    // round-trip: the answer a pack author (or a generator repairing its own
    // output) actually wants, printed one problem per line.
    if (std::strcmp(mode, "--validate") == 0) {
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

    // What content is this session actually running? A bug report from a
    // player with packs installed is unreadable without it, and it is the one
    // record that the folder was read at all.
    for (const std::string& path : packs) {
        SDL_Log("content pack: %s%s", path.c_str(),
                packProblems.empty() ? "" : " (REFUSED)");
    }

    // A refused pack is not fatal -- applyPacks already put the compiled
    // content back, so the game below is the ordinary one. But it must not be
    // silent either: someone installed a pack and is about to not see it, and
    // the reason is the one thing that lets them fix it. Logged in full (the
    // log is what a bug report carries), shown as the first problem plus a
    // count, because a broken pack can produce a great many.
    if (!packProblems.empty()) {
        for (const std::string& msg : packProblems) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "content pack: %s", msg.c_str());
        }
        std::string box = "A content pack was refused, so the game is running without it.\n\n" +
                          packProblems.front();
        if (packProblems.size() > 1) {
            box += "\n\n(and " + std::to_string(packProblems.size() - 1) +
                   " more -- see logs/game.log)";
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Voxel Factory - Content Pack",
                                 box.c_str(), nullptr);
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
