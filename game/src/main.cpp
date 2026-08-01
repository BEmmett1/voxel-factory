// We run our own main(), so tell SDL not to hijack it.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <SDL3/SDL.h>

#include "game/VoxelGame.h"
#include "game/AlchemyCircle.h"
#include "game/MachineSystem.h"
#include "game/Recipes.h"
#include "game/SaveSystem.h"
#include "game/Settings.h"
#include "game/World.h"
#include "VoxelGameInternal.h" // vg::kOrgName / kAppName

#include "engine/CrashHandler.h"
#include "engine/Log.h"
#include "engine/Paths.h"

#include <array>
#include <cstdint>
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

    // ---- Recipe keys ------------------------------------------------------
    // The keys ARE the save format for a locked machine, so a duplicate makes
    // two recipes indistinguishable on load and an empty one makes a lock
    // unsaveable. Nothing else in the build catches either.
    {
        auto uniqueKeys = [](const std::vector<const char*>& keys) {
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (!keys[i] || !*keys[i]) return false;
                for (std::size_t j = i + 1; j < keys.size(); ++j) {
                    if (std::strcmp(keys[i], keys[j]) == 0) return false;
                }
            }
            return true;
        };
        std::vector<const char*> hand, mach, circ;
        for (const Recipe& r : handcraftRecipes()) hand.push_back(r.key);
        for (const MachineRecipe& r : machineRecipes()) mach.push_back(r.key);
        for (const CircleRecipe& r : circleRecipes()) circ.push_back(r.key);
        SELFTEST_CHECK(uniqueKeys(hand));
        SELFTEST_CHECK(uniqueKeys(mach));
        SELFTEST_CHECK(uniqueKeys(circ));

        // Round-trip: a key resolves back to the row it names, for every row
        // of every machine -- including the manual twins, which reach their
        // powered counterpart's list through MachineTraits::recipeGroup.
        for (int b = 1; b < static_cast<int>(BlockId::Count); ++b) {
            const BlockId type = static_cast<BlockId>(b);
            if (!isMachine(type)) continue;
            const auto rows = recipesForMachine(type);
            for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
                SELFTEST_CHECK(recipeIndexForKey(type, recipeKeyFor(type, i)) == i);
            }
            // A key that no longer names anything lands on AUTO, never on
            // whatever row happens to sit at some index today. This is the
            // whole promise that lets the tables be edited freely.
            SELFTEST_CHECK(recipeIndexForKey(type, "no/such/recipe") == -1);
            SELFTEST_CHECK(recipeIndexForKey(type, "") == -1);
        }
        for (int i = 0; i < static_cast<int>(circleRecipes().size()); ++i) {
            SELFTEST_CHECK(circleIndexForKey(circleKeyFor(i)) == i);
        }
        SELFTEST_CHECK(circleIndexForKey("no/such/recipe") == -1);

        // A manual twin must run EXACTLY its powered counterpart's rows, or
        // the two tiers would drift and a lock would not survive an upgrade.
        for (const MachineTraits& traits : kMachineTraits) {
            if (traits.recipeGroup == BlockId::Air) continue;
            const auto mine = recipesForMachine(traits.block);
            const auto theirs = recipesForMachine(traits.recipeGroup);
            SELFTEST_CHECK(mine.size() == theirs.size());
            for (std::size_t i = 0; i < mine.size() && i < theirs.size(); ++i) {
                SELFTEST_CHECK(mine[i] == theirs[i]);
            }
        }
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

    // ---- Circle patterns are unambiguous ---------------------------------
    // Ring slots match on "holds AT LEAST this many", so one pattern can be a
    // superset of another and silently shadow it -- a recipe you can lay
    // perfectly and never get. Order is the fix, and this is what checks it:
    // lay each pattern exactly and confirm the matcher returns THAT recipe.
    {
        World cw;
        std::unordered_map<glm::ivec3, Machine, IVec3Hash> cm;
        const glm::ivec3 core{80, 30, 80};
        cw.setBlock(core.x, core.y, core.z, BlockId::RuneCore);
        cm[core].type = BlockId::RuneCore;
        for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
            const glm::ivec3 p = AlchemyCircle::slotPos(core, sl);
            cw.setBlock(p.x, p.y, p.z, BlockId::Pedestal);
            cm[p].type = BlockId::Pedestal;
        }

        const auto& all = circleRecipes();
        for (std::size_t k = 0; k < all.size(); ++k) {
            const CircleRecipe& want = all[k];
            for (int sl = 0; sl < AlchemyCircle::kRingSlots; ++sl) {
                cm[AlchemyCircle::slotPos(core, sl)].input = Inventory{};
            }
            // A 4-slot pattern lists the CARDINALS (the even ring slots).
            const int stride = want.ring.size() == 4 ? 2 : 1;
            for (std::size_t ringIdx = 0; ringIdx < want.ring.size(); ++ringIdx) {
                if (want.ring[ringIdx].id == ItemId::None) continue;
                cm[AlchemyCircle::slotPos(core, static_cast<int>(ringIdx) * stride)]
                    .input.add(want.ring[ringIdx].id, want.ring[ringIdx].count);
            }
            Inventory centre;
            if (want.center.id != ItemId::None) centre.add(want.center.id, want.center.count);

            const auto ring2 = AlchemyCircle::ringContents(cw, cm, core);
            const auto got = AlchemyCircle::findMatch(ring2, centre,
                                                      AlchemyCircle::Tier::Greater, true);
            if (!got || got.recipe != &want) {
                std::printf("selftest: circle pattern '%s' is shadowed by '%s'\n",
                            want.key, got ? got.recipe->key : "(nothing)");
                return 1;
            }
        }
    }

    // ---- Tech-tree reachability (the deadlock check) ----------------------
    // This is what replaces "the recipe tables are append-only". They can now
    // be edited freely, so the guardrail has to be about MEANING rather than
    // ordering: starting from nothing but what the world hands you, the
    // closure over all three recipe surfaces must reach every machine and
    // every recipe input. Edit a recipe into a deadlock and this fails.
    {
        std::array<bool, static_cast<std::size_t>(ItemId::Count)> have{};
        auto known = [&](ItemId id) { return have[static_cast<std::size_t>(id)]; };
        auto gain = [&](ItemId id) {
            if (id == ItemId::None || known(id)) return false;
            have[static_cast<std::size_t>(id)] = true;
            return true;
        };

        // Seed: everything the world yields to a bare hand or a tool -- block
        // drops (ore, wood, sand, stone, leaves' sticks), plus the two rain
        // items. Machines you PLACE drop themselves, so seeding block drops
        // would beg the question; only naturally-occurring blocks count.
        for (int b = 1; b < static_cast<int>(BlockId::Count); ++b) {
            const BlockId id = static_cast<BlockId>(b);
            if (isMachine(id) || isSource(id)) continue;
            gain(blockDrop(id).id);
        }
        gain(ItemId::Stick);
        gain(ItemId::Pebble);
        gain(ItemId::SpringWater); // the Bucket in the rain, and the barrel
        // Boss drops enter the economy through COMBAT rather than a recipe, so
        // the closure has to be told about them (kSpecies is private to
        // CreatureSystem.cpp). Anything gated on these is gated on a fight,
        // which is the design, not a deadlock.
        gain(ItemId::VoidCatalyst);
        gain(ItemId::StormCore);

        // Fixpoint over the three surfaces. A machine recipe is only usable
        // once the machine ITSELF is reachable, which is the part that makes
        // this a real bootstrap test rather than a shopping list.
        for (bool changed = true; changed;) {
            changed = false;
            for (const Recipe& r : handcraftRecipes()) {
                bool ok = true;
                for (const ItemStack& in : r.inputs) ok = ok && known(in.id);
                if (ok) changed |= gain(r.output.id);
            }
            for (const CircleRecipe& r : circleRecipes()) {
                bool ok = known(ItemId::RuneCoreItem) && known(ItemId::PedestalItem) &&
                          (r.center.id == ItemId::None || known(r.center.id));
                for (const ItemStack& in : r.ring) ok = ok && (in.id == ItemId::None || known(in.id));
                if (ok) changed |= gain(r.output.id);
            }
            for (const MachineRecipe& r : machineRecipes()) {
                // ANY machine that runs this list will do. The manual twins
                // are the whole point: a Bloomery smelts the Furnace's
                // recipes, which is what breaks the circularity of "ingots
                // need a Furnace, a Furnace needs ingots".
                bool ok = false;
                for (const MachineTraits& mt : kMachineTraits) {
                    if (recipeGroupFor(mt.block) != r.machine) continue;
                    if (known(blockDrop(mt.block).id)) { ok = true; break; }
                }
                for (const ItemStack& in : r.inputs) ok = ok && known(in.id);
                if (!ok) continue;
                for (const RecipeOutput& o : r.outputs) changed |= gain(o.stack.id);
            }
        }

        // Every machine must be buildable, and every recipe input obtainable.
        for (const MachineTraits& traits : kMachineTraits) {
            if (known(blockDrop(traits.block).id)) continue;
            std::printf("selftest: %s can never be built\n", blockName(traits.block));
            return 1;
        }
        for (const MachineRecipe& r : machineRecipes()) {
            for (const ItemStack& in : r.inputs) {
                if (known(in.id)) continue;
                std::printf("selftest: recipe '%s' needs unreachable %s\n",
                            r.key, itemName(in.id));
                return 1;
            }
        }
        for (const CircleRecipe& r : circleRecipes()) {
            if (r.center.id != ItemId::None && !known(r.center.id)) {
                std::printf("selftest: circle '%s' needs unreachable %s\n",
                            r.key, itemName(r.center.id));
                return 1;
            }
            for (const ItemStack& in : r.ring) {
                if (in.id == ItemId::None || known(in.id)) continue;
                std::printf("selftest: circle '%s' needs unreachable %s\n",
                            r.key, itemName(in.id));
                return 1;
            }
        }

        // The bootstrap itself: the Alchemy Circle is where nearly every
        // recipe now lives, and its two parts cost Copper Ingots, which cost
        // a fire. So SOME machine that needs neither power nor a circle must
        // be hand-craftable, or a fresh world is stuck at sticks and pebbles.
        SELFTEST_CHECK(known(ItemId::CopperIngot));
        SELFTEST_CHECK(known(ItemId::RuneCoreItem) && known(ItemId::PedestalItem));
        SELFTEST_CHECK(known(ItemId::MachineFrame));
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
    std::printf("\nA machine never burns an item its own recipes consume, which is why\n"
                "a Furnace fed wood chars it instead of eating it.\n");
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
