// We run our own main(), so tell SDL not to hijack it.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>

#include "game/VoxelGame.h"
#include "game/SaveSystem.h"
#include "game/Settings.h"
#include "game/World.h"
#include "VoxelGameInternal.h" // vg::kOrgName / kAppName

#include "engine/CrashHandler.h"
#include "engine/Log.h"
#include "engine/Paths.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
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

    SELFTEST_CHECK(machines2.size() == 1);
    const Machine& m2 = machines2.at(glm::ivec3{1, 3, 3});
    SELFTEST_CHECK(m2.type == BlockId::Grinder);
    SELFTEST_CHECK(m2.selectedRecipe == 1);
    SELFTEST_CHECK(m2.progress == 0.75f);
    SELFTEST_CHECK(m2.input.count(ItemId::Herb) == 3);
    SELFTEST_CHECK(m2.output.count(ItemId::GroundHerb) == 2);

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
    std::printf("selftest OK\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();

    // Headless save round-trip for CI; runs before any window/GL setup.
    if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) {
        return runSelfTest();
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
