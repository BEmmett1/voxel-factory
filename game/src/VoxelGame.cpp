#include "engine/GL.h" // GL calls in onStart/onRender

#include "game/VoxelGame.h"
#include "game/ChunkMesher.h"
#include "game/Raycast.h"
#include "game/Block.h"
#include "game/Atlas.h"
#include "game/SaveSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
    constexpr float kLookSensitivity = 0.12f; // degrees per pixel

    // ---- Player physics: the feel knobs. Tune freely. ----
    constexpr float kWalkSpeed    = 4.5f;   // blocks per second
    constexpr float kSprintMult   = 1.6f;   // LCtrl multiplier
    constexpr float kAccel        = 40.0f;  // blocks/s^2 spin-up toward wanted speed
    constexpr float kDecel        = 30.0f;  // blocks/s^2 friction when no input
    constexpr float kGravity      = 28.8f;  // blocks per second^2
    constexpr float kJumpSpeed    = 8.5f;   // initial jump velocity (~1.3 block jump)
    constexpr float kTerminalVel  = 50.0f;  // max fall speed
    constexpr float kPlayerHalfW  = 0.30f;  // half width of the player's box
    constexpr float kPlayerHeight = 1.80f;
    constexpr float kEyeHeight    = 1.62f;  // camera above the feet
    constexpr float kVoidY        = -8.0f;  // fall below this: pack lost, respawn
    // -------------------------------------------------------

    constexpr float kReach = 8.0f;             // how far you can target blocks
    constexpr int   kWorldChunks = 6;          // NxN chunks => 96x96 area
    constexpr float kIslandRadius = 34.0f;     // base coastline radius (noise-wobbled)
    constexpr int   kSurfaceY = 14;            // base island surface height
    constexpr float kPlateauRadius = 10.0f;    // flattened spawn/demo area
    constexpr int   kPlateauY = kSurfaceY + 4; // plateau (and demo) surface height
    constexpr float kTickSeconds = 1.0f / 20.0f; // matches Application's tick rate
    constexpr int   kLoadPerAction = 8;        // recipe sets loaded per panel action
    constexpr int   kBeltStepTicks = 4;        // ticks between belt advances (~0.2s)

    // Machine-panel layout: computed once per frame from the window size and
    // content counts; the single source of truth for hit-testing AND drawing.
    struct PanelLayout {
        static constexpr float RowH = 26.0f;   // action row height
        static constexpr float Cell = 40.0f;   // item cell size
        static constexpr float Gap  = 6.0f;    // between cells
        static constexpr float StripH = Cell + 12.0f; // IN / OUT band height
        static constexpr int   InvCols = 10;

        float px = 0, py = 0, panelW = 640.0f, panelH = 0;
        int   rows = 0;      // action rows (AUTO + recipes + TAKE)
        float rowsY = 0;     // top of the action rows
        float inY = 0;       // top of the IN cell strip
        float outY = 0;      // top of the OUT cell strip
        float stripCellsX = 0; // first cell x in the IN/OUT strips
        float barY = 0;      // progress bar
        float invLabelY = 0;
        float invY = 0;      // top of the inventory grid
        int   invRows = 0;
        float tooltipY = 0;
        float footerY = 0;
    };

    PanelLayout panelLayout(int w, int h, int actionRows, int invCount) {
        PanelLayout L;
        L.rows = actionRows;
        L.invRows = std::max(1, (invCount + PanelLayout::InvCols - 1) / PanelLayout::InvCols);
        const float headerH = 44.0f, barH = 20.0f, invLabelH = 20.0f;
        const float tooltipH = 18.0f, footerH = 24.0f;
        const float invH = L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        L.panelH = headerH + actionRows * PanelLayout::RowH + 2.0f * PanelLayout::StripH +
                   barH + invLabelH + invH + tooltipH + footerH + 12.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.inY = L.rowsY + actionRows * PanelLayout::RowH + 6.0f;
        L.outY = L.inY + PanelLayout::StripH;
        L.stripCellsX = L.px + 64.0f;
        L.barY = L.outY + PanelLayout::StripH + 2.0f;
        L.invLabelY = L.barY + barH;
        L.invY = L.invLabelY + invLabelH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.footerY = L.py + L.panelH - footerH + 2.0f;
        return L;
    }

    // The item types present in an inventory, with counts, in enum order.
    std::vector<std::pair<ItemId, int>> itemsOf(const Inventory& inv) {
        std::vector<std::pair<ItemId, int>> out;
        for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            if (inv.count(id) > 0) out.emplace_back(id, inv.count(id));
        }
        return out;
    }

    // Which cell of a grid at (originX, originY) is under the mouse? -1 = none.
    int hitCell(float mx, float my, float originX, float originY, int count, int cols) {
        for (int i = 0; i < count; ++i) {
            const float cx = originX + (i % cols) * (PanelLayout::Cell + PanelLayout::Gap);
            const float cy = originY + (i / cols) * (PanelLayout::Cell + PanelLayout::Gap);
            if (mx >= cx && mx < cx + PanelLayout::Cell &&
                my >= cy && my < cy + PanelLayout::Cell) {
                return i;
            }
        }
        return -1;
    }

    // One item cell: dark slab, icon, count (shared by machine panel + menu).
    void drawItemCell(engine::UiRenderer& ui, const engine::Texture& atlas,
                      float x, float y, ItemId id, int count) {
        ui.rect(x, y, PanelLayout::Cell, PanelLayout::Cell, glm::vec4(0.16f, 0.16f, 0.19f, 1.0f));
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(itemInfo(id).atlasTile, uv0, uv1);
        ui.icon(atlas, x + 4, y + 4, PanelLayout::Cell - 8, PanelLayout::Cell - 8, uv0, uv1);
        const std::string cnt = std::to_string(count);
        ui.text(x + PanelLayout::Cell - ui.textWidth(11.0f, cnt) - 3,
                y + PanelLayout::Cell - 13, 11.0f, cnt, glm::vec4(1.0f));
    }

    // Crafting-menu layout (rows + a materials grid); shared by update + draw.
    struct CraftLayout {
        static constexpr float RowH = 22.0f;
        float px = 0, py = 0, panelW = 700.0f, panelH = 0;
        float rowsY = 0;
        int   rows = 0;
        float invLabelY = 0, invY = 0;
        int   invRows = 0;
        float tooltipY = 0, footerY = 0;
    };

    CraftLayout craftLayout(int w, int h, int nRecipes, int invCount) {
        CraftLayout L;
        L.rows = nRecipes;
        L.invRows = std::max(1, (invCount + PanelLayout::InvCols - 1) / PanelLayout::InvCols);
        const float headerH = 40.0f, invLabelH = 20.0f, tooltipH = 16.0f, footerH = 22.0f;
        const float invH = L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        L.panelH = headerH + nRecipes * CraftLayout::RowH + invLabelH + invH +
                   tooltipH + footerH + 8.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.invLabelY = L.rowsY + nRecipes * CraftLayout::RowH + 2.0f;
        L.invY = L.invLabelY + invLabelH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.footerY = L.py + L.panelH - footerH + 2.0f;
        return L;
    }
    constexpr float kSourceSpawnSeconds = 7.0f; // time between a source's node spawns
    constexpr int   kPatchRadius = 4;          // how far a source spreads its nodes
    constexpr int   kPatchCap = 5;             // max live nodes per source patch
    constexpr float kMineSeconds = 4.0f;       // miner: seconds per harvested node
    constexpr int   kMineRadius = 4;           // miner reach (matches patch radius)

    // Forestry. Chopped leaves are the sapling supply; the pity counter
    // guarantees a drop before a whole canopy can come up empty-handed.
    constexpr float kSaplingDropChance = 0.25f; // sapling chance per chopped leaf
    constexpr int   kSaplingPityLeaves = 4;     // guaranteed drop after N dry leaves
    constexpr float kTreeGrowSeconds  = 45.0f;  // sapling -> tree (space permitting)
    constexpr float kLeafDecaySeconds = 0.6f;   // cadence of orphaned-leaf decay passes
    constexpr float kLeafDecayChance  = 0.5f;   // per orphaned leaf per pass (staggers)
    constexpr int   kLeafReach        = 2;      // leaves survive within this of a log

    // The tree shape as offsets from the sapling cell: a 3-log trunk, a 3x3
    // leaf ring around the top log, a full 3x3 layer, and a plus-shaped cap.
    // Single source of truth for world-gen, sapling growth, and space checks.
    struct TreeCell {
        glm::ivec3 offset;
        BlockId    block;
    };

    const std::vector<TreeCell>& treeCells() {
        static const std::vector<TreeCell> cells = [] {
            std::vector<TreeCell> c;
            for (int y = 0; y <= 2; ++y) c.push_back({{0, y, 0}, BlockId::Log});
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx != 0 || dz != 0) c.push_back({{dx, 2, dz}, BlockId::Leaves});
                    c.push_back({{dx, 3, dz}, BlockId::Leaves});
                }
            }
            const glm::ivec3 cap[5] = {{0, 4, 0}, {1, 4, 0}, {-1, 4, 0}, {0, 4, 1}, {0, 4, -1}};
            for (const glm::ivec3& o : cap) c.push_back({o, BlockId::Leaves});
            return c;
        }();
        return cells;
    }

    // Stamp a grown tree whose trunk base is at `base` (the sapling cell).
    void placeTree(World& world, const glm::ivec3& base) {
        for (const TreeCell& c : treeCells()) {
            world.setBlock(base.x + c.offset.x, base.y + c.offset.y,
                           base.z + c.offset.z, c.block);
        }
    }

    // The first raw item in a miner's input buffer; None = unfiltered (mine
    // anything nearby). The filter item is a reference sample, never consumed.
    ItemId minerFilter(const Machine& mac) {
        for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            if (mac.input.count(id) > 0 && nodeForRaw(id) != BlockId::Air) return id;
        }
        return ItemId::None;
    }

    // Does this machine use `item` as an input? A machine locked to a specific
    // recipe only accepts that recipe's inputs (so belts can't overfill it
    // with ingredients it will never consume).
    bool machineAccepts(const Machine& mac, ItemId item) {
        // Miners take raw items as mining filters (not consumed).
        if (mac.type == BlockId::Miner) {
            return nodeForRaw(item) != BlockId::Air;
        }
        const auto recipes = recipesForMachine(mac.type);
        for (std::size_t i = 0; i < recipes.size(); ++i) {
            if (mac.selectedRecipe >= 0 && static_cast<int>(i) != mac.selectedRecipe) continue;
            for (const ItemStack& in : recipes[i]->inputs) {
                if (in.id == item) return true;
            }
        }
        return false;
    }
    const glm::vec3 kLightDir = glm::normalize(glm::vec3{-0.4f, -1.0f, -0.3f});

    // Cheap deterministic per-texel noise for the procedural atlas.
    float texelNoise(int id, int px, int py) {
        std::uint32_t h = static_cast<std::uint32_t>(px) * 73856093u ^
                          static_cast<std::uint32_t>(py) * 19349663u ^
                          static_cast<std::uint32_t>(id) * 83492791u;
        return (static_cast<float>(h % 1000u) / 1000.0f - 0.5f) * 0.25f;
    }

    // Deterministic hash of a 2D lattice point and a seed.
    std::uint32_t hash2(int x, int z, std::uint32_t seed = 0) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                          static_cast<std::uint32_t>(z) * 668265263u + seed * 2654435761u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    // Smooth value noise in [0,1]: bilinear interpolation of hashed lattice
    // values with a smoothstep fade. Used for the island heightmap + coastline.
    float valueNoise(float x, float z, std::uint32_t seed) {
        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(x0);
        const float fz = z - static_cast<float>(z0);
        const float sx = fx * fx * (3.0f - 2.0f * fx);
        const float sz = fz * fz * (3.0f - 2.0f * fz);

        auto lattice = [seed](int a, int b) {
            return static_cast<float>(hash2(a, b, seed) % 1024u) / 1023.0f;
        };
        const float a = lattice(x0, z0);
        const float b = lattice(x0 + 1, z0);
        const float c = lattice(x0, z0 + 1);
        const float d = lattice(x0 + 1, z0 + 1);
        return glm::mix(glm::mix(a, b, sx), glm::mix(c, d, sx), sz);
    }

    // Does the player's box (feet at `feet`) overlap any solid block?
    bool boxCollides(const World& w, const glm::vec3& feet) {
        const int x0 = static_cast<int>(std::floor(feet.x - kPlayerHalfW));
        const int x1 = static_cast<int>(std::floor(feet.x + kPlayerHalfW));
        const int y0 = static_cast<int>(std::floor(feet.y));
        const int y1 = static_cast<int>(std::floor(feet.y + kPlayerHeight));
        const int z0 = static_cast<int>(std::floor(feet.z - kPlayerHalfW));
        const int z1 = static_cast<int>(std::floor(feet.z + kPlayerHalfW));
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    if (isSolid(w.getBlock(x, y, z))) return true;
                }
            }
        }
        return false;
    }

    // Where the player stands after spawning or falling off the island.
    glm::vec3 spawnFeet() {
        const float c = kWorldChunks * CHUNK_SIZE * 0.5f;
        return {c, static_cast<float>(kPlateauY) + 1.0f, c + 6.0f};
    }

    // HSV (h,s,v in [0,1]) -> RGB, for distinct material icon colors.
    glm::vec3 hsvColor(float h, float s, float v) {
        const float i = std::floor(h * 6.0f);
        const float f = h * 6.0f - i;
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - f * s);
        const float t = v * (1.0f - (1.0f - f) * s);
        switch (static_cast<int>(i) % 6) {
            case 0:  return {v, t, p};
            case 1:  return {q, v, p};
            case 2:  return {p, v, t};
            case 3:  return {p, q, v};
            case 4:  return {t, p, v};
            default: return {v, p, q};
        }
    }
}

VoxelGame::VoxelGame()
    : engine::Application("Voxel Factory", 1280, 720) {}

void VoxelGame::onStart() {
    glEnable(GL_DEPTH_TEST);
    window().setRelativeMouse(true); // capture the cursor for FPS look

    const char* base = SDL_GetBasePath(); // owned by SDL, do not free
    const std::string dir = base ? base : "";
    if (!m_shader.loadFromFiles(dir + "shaders/voxel.vert", dir + "shaders/voxel.frag")) {
        SDL_Log("Failed to load voxel shaders from '%sshaders/'", dir.c_str());
    }
    m_shader.use();
    m_shader.setInt("uAtlas", 0); // atlas lives on texture unit 0

    buildAtlas();

    // The save lives in the OS-preferred data directory.
    if (char* pref = SDL_GetPrefPath("benny", "voxel-factory")) {
        m_savePath = std::string(pref) + "save.vxf";
        SDL_free(pref);
    }

    // Hotbar: every placeable item, in enum order. Keys 1-9 and 0 jump to the
    // first ten slots; the mouse wheel cycles through all of them.
    m_hotbar.clear();
    for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemId id = static_cast<ItemId>(i);
        if (itemInfo(id).placeable) m_hotbar.push_back(id);
    }

    m_world = std::make_unique<World>();
    if (!loadGame()) {
        // No (valid) save: fresh island + the starting kit of raw materials.
        // Everything placeable is hand-crafted from these.
        buildWorld();

        camera().position = spawnFeet() + glm::vec3(0.0f, kEyeHeight, 0.0f);
        camera().yaw = -90.0f;   // looking toward -Z (the demo row)
        camera().pitch = -15.0f;

        m_inventory.add(ItemId::CopperOre, 30);
        m_inventory.add(ItemId::Stone, 12);
        m_inventory.add(ItemId::Sand, 10);
        m_inventory.add(ItemId::Crystal, 8);
        m_inventory.add(ItemId::Herb, 6);
        m_inventory.add(ItemId::SpringWater, 4);
        m_inventory.add(ItemId::Essence, 2);
    }

    rebuildMesh();
    buildHighlightMesh();
    buildCrosshairMesh();
    m_ui.init();

    updateTitle();
}

bool VoxelGame::saveGame() {
    if (m_savePath.empty() || !m_world) return false;
    int slot = m_selectedSlot;
    SaveData d{*m_world, m_inventory, m_machines, m_belts, m_sources, m_saplings,
               camera().position, camera().yaw, camera().pitch,
               m_worldSeed, m_sourceRng, slot};
    return SaveSystem::save(m_savePath, d);
}

bool VoxelGame::loadGame() {
    if (m_savePath.empty()) return false;
    int slot = 0;
    SaveData d{*m_world, m_inventory, m_machines, m_belts, m_sources, m_saplings,
               camera().position, camera().yaw, camera().pitch,
               m_worldSeed, m_sourceRng, slot};
    if (!SaveSystem::load(m_savePath, d)) {
        // A partial read may have dirtied state; start clean.
        m_world = std::make_unique<World>();
        m_inventory = Inventory{};
        m_machines.clear();
        m_belts.clear();
        m_sources.clear();
        m_saplings.clear();
        return false;
    }
    m_selectedSlot = std::clamp(slot, 0, static_cast<int>(m_hotbar.size()) - 1);
    return true;
}

void VoxelGame::onExit() {
    saveGame(); // runs on every quit path (Esc, window close)
}

void VoxelGame::buildAtlas() {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(Atlas::WidthPx) * Atlas::HeightPx * 4, 0);

    // Fill one atlas tile with a noisy, edge-darkened swatch of `baseColor`.
    auto fillTile = [&](int tile, const glm::vec3& baseColor) {
        const int x0 = (tile % Atlas::Cols) * Atlas::TilePx;
        const int y0 = (tile / Atlas::Cols) * Atlas::TilePx;
        for (int py = 0; py < Atlas::TilePx; ++py) {
            for (int px = 0; px < Atlas::TilePx; ++px) {
                const bool edge = px == 0 || py == 0 ||
                                  px == Atlas::TilePx - 1 || py == Atlas::TilePx - 1;
                const float edgeMul = edge ? 0.6f : 1.0f;
                const float n = 1.0f + texelNoise(tile, px, py);
                const glm::vec3 c = glm::clamp(baseColor * n * edgeMul, 0.0f, 1.0f);
                const std::size_t idx =
                    (static_cast<std::size_t>(y0 + py) * Atlas::WidthPx + (x0 + px)) * 4;
                pixels[idx + 0] = static_cast<unsigned char>(c.r * 255.0f);
                pixels[idx + 1] = static_cast<unsigned char>(c.g * 255.0f);
                pixels[idx + 2] = static_cast<unsigned char>(c.b * 255.0f);
                pixels[idx + 3] = 255;
            }
        }
    };

    // Block tiles (indexed by block enum value).
    for (int id = 1; id < static_cast<int>(BlockId::Count); ++id) {
        fillTile(id, blockInfo(static_cast<BlockId>(id)).color);
    }

    // Item icon tiles: placeables reuse their block color; materials get a
    // distinct hue spaced around the wheel.
    for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemInfo& info = itemInfo(static_cast<ItemId>(i));
        const glm::vec3 color = info.placeable
            ? blockInfo(info.placesBlock).color
            : hsvColor(std::fmod(static_cast<float>(i) * 0.61803398875f, 1.0f), 0.55f, 0.85f);
        fillTile(info.atlasTile, color);
    }

    // Conduit direction arrow: the belt's dark base with a bright arrow
    // pointing toward +v (down the tile); the mesher rotates UVs per facing.
    fillTile(Atlas::BeltArrowTile, blockInfo(BlockId::Belt).color);
    {
        const int x0 = (Atlas::BeltArrowTile % Atlas::Cols) * Atlas::TilePx;
        const int y0 = (Atlas::BeltArrowTile / Atlas::Cols) * Atlas::TilePx;
        auto stamp = [&](int px, int py) {
            const std::size_t idx =
                (static_cast<std::size_t>(y0 + py) * Atlas::WidthPx + (x0 + px)) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 214;
            pixels[idx + 2] = 51;
            pixels[idx + 3] = 255;
        };
        for (int py = 2; py <= 8; ++py) {   // shaft
            stamp(7, py);
            stamp(8, py);
        }
        for (int k = 0; k < 5; ++k) {       // chevron head, tip at py = 13
            for (int px = 3 + k; px <= 12 - k; ++px) {
                stamp(px, 9 + k);
            }
        }
    }

    m_atlas.createFromPixels(Atlas::WidthPx, Atlas::HeightPx, pixels.data());
}

void VoxelGame::buildWorld() {
    // Fresh island layout every launch.
    m_worldSeed = static_cast<std::uint32_t>(SDL_GetPerformanceCounter());

    const int extent = kWorldChunks * CHUNK_SIZE;
    const float cx = extent * 0.5f;
    const float cz = extent * 0.5f;

    // A floating island: irregular coastline, gentle hills toward the center,
    // and an underside that tapers so it reads as a landmass adrift in the sky.
    for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dz = static_cast<float>(z) - cz;
            const float dist = std::sqrt(dx * dx + dz * dz);

            // Irregular coastline: wobble the radial distance with
            // low-frequency noise so the circle grows bays and headlands.
            const float wobble = (valueNoise(x * 0.06f, z * 0.06f, m_worldSeed) - 0.5f) * 14.0f;
            const float coastDist = dist + wobble;
            if (coastDist > kIslandRadius) continue; // open air beyond the coast

            // Gentle hills, rising toward the interior.
            const float inland = 1.0f - glm::clamp(coastDist / kIslandRadius, 0.0f, 1.0f);
            const float hills = valueNoise(x * 0.09f, z * 0.09f, m_worldSeed ^ 0x9e3779b9u);
            int surfaceY = kSurfaceY + static_cast<int>(inland * 4.0f + hills * 3.0f);

            // Flatten the spawn plateau, blending into the terrain at its rim.
            if (dist < kPlateauRadius) {
                surfaceY = kPlateauY;
            } else if (dist < kPlateauRadius + 5.0f) {
                const float t = (dist - kPlateauRadius) / 5.0f;
                surfaceY = static_cast<int>(glm::mix(static_cast<float>(kPlateauY),
                                                     static_cast<float>(surfaceY), t) + 0.5f);
            }

            // Tapered underside: thicker toward the center.
            const int thickness = 3 + static_cast<int>(inland * 8.0f + hills * 2.0f);
            const int bottomY = surfaceY - thickness + 1;

            for (int y = bottomY; y <= surfaceY; ++y) {
                BlockId b = BlockId::Stone;
                if (y == surfaceY) b = BlockId::Grass;
                else if (y >= surfaceY - 2) b = BlockId::Dirt;
                m_world->setBlock(x, y, z, b);
            }
        }
    }

    // Automation demo on the plateau: generator -> wire -> grinder, then a
    // conduit line auto-carries the grinder's ground herb into a cauldron.
    const int cxi = static_cast<int>(cx);
    const int dy = kPlateauY + 1;              // on top of the plateau grass
    const int dzRow = static_cast<int>(cz) - 4; // demo row, just north of center

    m_world->setBlock(cxi - 5, dy, dzRow, BlockId::Generator);
    for (int x = cxi - 4; x <= cxi; ++x) {
        m_world->setBlock(x, dy, dzRow, BlockId::Wire);
    }
    m_world->setBlock(cxi + 1, dy, dzRow, BlockId::Grinder);
    registerMachine({cxi + 1, dy, dzRow}, BlockId::Grinder);
    m_machines[{cxi + 1, dy, dzRow}].input.add(ItemId::Herb, 20); // fuel for the demo

    for (int x = cxi + 2; x <= cxi + 4; ++x) {
        m_world->setBlock(x, dy, dzRow, BlockId::Belt);
        registerBelt({x, dy, dzRow}, {1, 0, 0}); // carry items toward +x
    }
    m_world->setBlock(cxi + 5, dy, dzRow, BlockId::Cauldron);
    registerMachine({cxi + 5, dy, dzRow}, BlockId::Cauldron);

    // Wire spur alongside the belts so the cauldron is powered too (belts are
    // not power nodes, so the network can't reach it through them).
    for (int x = cxi + 1; x <= cxi + 5; ++x) {
        m_world->setBlock(x, dy, dzRow - 1, BlockId::Wire);
    }

    // Mining demo on the plateau's south side: a herb source grows a patch,
    // a powered miner harvests it, and belts carry the herb away.
    const int mz = static_cast<int>(cz) + 3;
    m_world->setBlock(cxi - 7, dy, mz, BlockId::SourceHerb);
    m_sources[{cxi - 7, dy, mz}] = 0.0f;
    m_world->setBlock(cxi - 5, dy, mz, BlockId::Miner);
    registerMachine({cxi - 5, dy, mz}, BlockId::Miner);
    m_world->setBlock(cxi - 4, dy, mz, BlockId::Generator);
    for (int i = 1; i <= 2; ++i) {
        m_world->setBlock(cxi - 5, dy, mz - i, BlockId::Belt);
        registerBelt({cxi - 5, dy, mz - i}, {0, 0, -1}); // carry toward the demo row
    }

    // Scatter glowing resource sources across the island (seeded random),
    // keeping the plateau clear. Each will grow a patch of its node type.
    const BlockId sourceTypes[6] = {BlockId::SourceHerb, BlockId::SourceCopper,
                                    BlockId::SourceSand, BlockId::SourceCrystal,
                                    BlockId::SourceWater, BlockId::SourceEssence};
    const int sourceCounts[6] = {4, 4, 3, 3, 2, 2};

    for (int t = 0; t < 6; ++t) {
        int placed = 0;
        for (int attempt = 0; attempt < 500 && placed < sourceCounts[t]; ++attempt) {
            const std::uint32_t h = hash2(t * 977 + attempt, attempt * 131 + 7,
                                          m_worldSeed ^ 0xABCD1234u);
            const int x = static_cast<int>(h % static_cast<std::uint32_t>(extent));
            const int z = static_cast<int>((h >> 10) % static_cast<std::uint32_t>(extent));

            const float ddx = static_cast<float>(x) - cx;
            const float ddz = static_cast<float>(z) - cz;
            if (std::sqrt(ddx * ddx + ddz * ddz) < kPlateauRadius + 6.0f) continue;

            // Needs a grass surface with air above.
            int gy = -1;
            for (int y = kSurfaceY + 8; y >= kSurfaceY - 2; --y) {
                if (m_world->getBlock(x, y, z) == BlockId::Grass &&
                    m_world->getBlock(x, y + 1, z) == BlockId::Air) {
                    gy = y;
                    break;
                }
            }
            if (gy < 0) continue;

            m_world->setBlock(x, gy + 1, z, sourceTypes[t]);
            m_sources[{x, gy + 1, z}] = 0.0f;
            ++placed;
        }
    }

    // One grown tree near the middle seeds forestry: chopping its leaves is
    // the only starting supply of saplings. Seeded random spot on plateau
    // grass, clear of the demo rows and the spawn point.
    const glm::vec3 spawn = spawnFeet();
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::uint32_t h = hash2(attempt * 53 + 11, attempt * 197 + 3,
                                      m_worldSeed ^ 0x07EE5EEDu);
        const int tx = cxi + static_cast<int>(h % 17u) - 8;
        const int tz = static_cast<int>(cz) + static_cast<int>((h >> 8) % 17u) - 8;
        if (std::abs(tz - dzRow) <= 1 || std::abs(tz - mz) <= 1) continue;
        if (std::abs(tx - static_cast<int>(spawn.x)) <= 1 &&
            std::abs(tz - static_cast<int>(spawn.z)) <= 1) continue;

        const int ty = kPlateauY + 1;
        if (m_world->getBlock(tx, ty - 1, tz) != BlockId::Grass) continue;
        bool clear = true;
        for (const TreeCell& c : treeCells()) {
            if (m_world->getBlock(tx + c.offset.x, ty + c.offset.y,
                                  tz + c.offset.z) != BlockId::Air) {
                clear = false;
                break;
            }
        }
        if (!clear) continue;

        placeTree(*m_world, {tx, ty, tz});
        break;
    }

    // Pre-grow each patch a little so raws are minable immediately.
    for (int round = 0; round < 3; ++round) {
        for (auto& [pos, timer] : m_sources) timer = kSourceSpawnSeconds;
        updateSources();
    }
}

void VoxelGame::rebuildMesh() {
    // Power state depends only on topology, so recompute it whenever geometry
    // changes. One combined buffer holds the whole world for now.
    m_power = PowerSystem::solve(*m_world);

    std::vector<float> data;
    for (const auto& [coord, chunk] : m_world->chunks()) {
        (void)chunk;
        ChunkMesher::appendChunk(data, *m_world, coord, m_power, m_belts);
    }
    m_mesh.upload(data, {3, 3, 2, 1}); // position, normal, uv, emissive
}

void VoxelGame::registerMachine(const glm::ivec3& pos, BlockId type) {
    Machine m;
    m.type = type;
    m_machines[pos] = m;
}

void VoxelGame::unregisterMachine(const glm::ivec3& pos) {
    const auto it = m_machines.find(pos);
    if (it == m_machines.end()) return;
    // Return any buffered items to the player so nothing is lost.
    for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemId id = static_cast<ItemId>(i);
        m_inventory.add(id, it->second.input.count(id));
        m_inventory.add(id, it->second.output.count(id));
    }
    m_machines.erase(it);
}

void VoxelGame::registerBelt(const glm::ivec3& pos, const glm::ivec3& facing) {
    Belt b;
    b.facing = facing;
    m_belts[pos] = b;
}

void VoxelGame::unregisterBelt(const glm::ivec3& pos) {
    const auto it = m_belts.find(pos);
    if (it == m_belts.end()) return;
    if (it->second.item != ItemId::None) m_inventory.add(it->second.item, 1);
    m_belts.erase(it);
}

void VoxelGame::beltStep() {
    // 1. Belts deliver their item into a machine directly ahead (if it accepts).
    for (auto& [pos, b] : m_belts) {
        if (b.item == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto mit = m_machines.find(front);
        if (mit != m_machines.end() && machineAccepts(mit->second, b.item)) {
            mit->second.input.add(b.item, 1);
            b.item = ItemId::None;
        }
    }

    // 2. Hop items belt -> belt. Use a snapshot of pre-step contents so an item
    //    advances at most one belt, and claim targets so two items never merge.
    std::unordered_map<glm::ivec3, ItemId, IVec3Hash> before;
    before.reserve(m_belts.size());
    for (const auto& [pos, b] : m_belts) before[pos] = b.item;

    std::unordered_set<glm::ivec3, IVec3Hash> claimed;
    for (auto& [pos, b] : m_belts) {
        const ItemId carried = before[pos];
        if (carried == ItemId::None) continue;
        const glm::ivec3 front = pos + b.facing;
        const auto tb = m_belts.find(front);
        if (tb == m_belts.end()) continue;       // ahead is not a belt
        if (before[front] != ItemId::None) continue; // target was occupied
        if (claimed.count(front)) continue;       // already filled this step
        tb->second.item = carried;
        b.item = ItemId::None;
        claimed.insert(front);
    }

    // 3. Empty belts pull one item from a machine's output directly behind them.
    for (auto& [pos, b] : m_belts) {
        if (b.item != ItemId::None) continue;
        const glm::ivec3 back = pos - b.facing;
        const auto mit = m_machines.find(back);
        if (mit == m_machines.end()) continue;
        Inventory& out = mit->second.output;
        for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            if (out.count(id) > 0) {
                out.remove(id, 1);
                b.item = id;
                break;
            }
        }
    }
}

bool VoxelGame::updateSources() {
    bool anySpawned = false;

    for (auto& [pos, timer] : m_sources) {
        timer += kTickSeconds;
        if (timer < kSourceSpawnSeconds) continue;
        timer = 0.0f;

        const BlockId node = sourceSpawnsNode(m_world->getBlock(pos.x, pos.y, pos.z));
        if (node == BlockId::Air) continue; // source block was removed under us

        // Patch is capped: count this source's live nodes nearby.
        int liveNodes = 0;
        for (int dz = -kPatchRadius; dz <= kPatchRadius; ++dz) {
            for (int dx = -kPatchRadius; dx <= kPatchRadius; ++dx) {
                for (int dy = -3; dy <= 3; ++dy) {
                    if (m_world->getBlock(pos.x + dx, pos.y + dy, pos.z + dz) == node) {
                        ++liveNodes;
                    }
                }
            }
        }
        if (liveNodes >= kPatchCap) continue;

        // Try a few random nearby columns for grass with air above.
        bool placedNode = false;
        for (int attempt = 0; attempt < 8 && !placedNode; ++attempt) {
            const std::uint32_t h = hash2(pos.x * 31 + attempt, pos.z * 17, m_worldSeed + m_sourceRng++);
            const int dx = static_cast<int>(h % (2 * kPatchRadius + 1)) - kPatchRadius;
            const int dz = static_cast<int>((h >> 8) % (2 * kPatchRadius + 1)) - kPatchRadius;
            if (dx == 0 && dz == 0) continue;

            const int x = pos.x + dx;
            const int z = pos.z + dz;
            for (int y = pos.y + 2; y >= pos.y - 3; --y) {
                if (m_world->getBlock(x, y, z) == BlockId::Grass &&
                    m_world->getBlock(x, y + 1, z) == BlockId::Air) {
                    m_world->setBlock(x, y + 1, z, node);
                    placedNode = true;
                    anySpawned = true;
                    break;
                }
            }
        }
    }

    return anySpawned;
}

// Would a solid block in this cell intersect the player's box?
bool VoxelGame::cellOverlapsPlayer(const glm::ivec3& p) {
    const glm::vec3 feet = camera().position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    return static_cast<float>(p.x + 1) > feet.x - kPlayerHalfW &&
           static_cast<float>(p.x) < feet.x + kPlayerHalfW &&
           static_cast<float>(p.z + 1) > feet.z - kPlayerHalfW &&
           static_cast<float>(p.z) < feet.z + kPlayerHalfW &&
           static_cast<float>(p.y + 1) > feet.y &&
           static_cast<float>(p.y) < feet.y + kPlayerHeight;
}

bool VoxelGame::updateSaplings() {
    bool anyGrown = false;
    std::vector<glm::ivec3> done;

    for (auto& [pos, timer] : m_saplings) {
        if (timer < kTreeGrowSeconds) {
            timer += kTickSeconds;
            continue;
        }
        if (m_world->getBlock(pos.x, pos.y, pos.z) != BlockId::Sapling) {
            done.push_back(pos); // the block went away; drop the stale timer
            continue;
        }

        // Grow only into open space -- and never onto the player, who must
        // not wake up entombed in a canopy.
        bool clear = true;
        for (const TreeCell& c : treeCells()) {
            const glm::ivec3 cell = pos + c.offset;
            if (cell != pos && m_world->getBlock(cell.x, cell.y, cell.z) != BlockId::Air) {
                clear = false;
                break;
            }
            if (cellOverlapsPlayer(cell)) {
                clear = false;
                break;
            }
        }
        if (!clear) continue; // blocked: stay ripe and retry next tick

        placeTree(*m_world, pos);
        done.push_back(pos);
        anyGrown = true;
    }

    for (const glm::ivec3& p : done) m_saplings.erase(p);
    return anyGrown;
}

bool VoxelGame::updateLeafDecay() {
    m_leafDecayTimer += kTickSeconds;
    if (m_leafDecayTimer < kLeafDecaySeconds) return false;
    m_leafDecayTimer = 0.0f;

    // Leaves with no log in reach wither, a random fraction per pass so a
    // felled canopy crumbles away rather than popping. Collect first: the
    // chunk map must not grow mid-iteration.
    std::vector<glm::ivec3> dying;
    for (const auto& [coord, chunk] : m_world->chunks()) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    if (chunk->get(x, y, z) != BlockId::Leaves) continue;
                    const glm::ivec3 p = coord * CHUNK_SIZE + glm::ivec3(x, y, z);

                    bool nearLog = false;
                    for (int dy = -kLeafReach; dy <= kLeafReach && !nearLog; ++dy) {
                        for (int dz = -kLeafReach; dz <= kLeafReach && !nearLog; ++dz) {
                            for (int dx = -kLeafReach; dx <= kLeafReach && !nearLog; ++dx) {
                                if (m_world->getBlock(p.x + dx, p.y + dy, p.z + dz) ==
                                    BlockId::Log) {
                                    nearLog = true;
                                }
                            }
                        }
                    }
                    if (nearLog) continue;

                    const std::uint32_t h =
                        hash2(p.x * 31 + p.y, p.z * 17, m_worldSeed + m_sourceRng++);
                    if (h % 100u <
                        static_cast<std::uint32_t>(kLeafDecayChance * 100.0f + 0.5f)) {
                        dying.push_back(p);
                    }
                }
            }
        }
    }

    for (const glm::ivec3& p : dying) {
        m_world->setBlock(p.x, p.y, p.z, BlockId::Air); // decayed leaves drop nothing
    }
    return !dying.empty();
}

void VoxelGame::onTick() {
    // Grow resource patches, pop ripe saplings, wither orphaned leaves.
    bool worldChanged = updateSources();
    worldChanged |= updateSaplings();
    worldChanged |= updateLeafDecay();
    if (worldChanged) {
        rebuildMesh();
    }

    // Powered machines process their input buffer into outputs over time.
    bool minedNode = false;
    for (auto& [pos, m] : m_machines) {
        m.crafting = false;
        if (!m_power.energized(pos.x, pos.y, pos.z)) continue;

        // Miners harvest the nearest grown resource node in reach instead of
        // running recipes; the patch regrows from its source, bounding the rate.
        if (m.type == BlockId::Miner) {
            // A raw item in the input buffer acts as a filter: mine only that
            // node type. Empty input = mine anything nearby.
            const BlockId filterNode = nodeForRaw(minerFilter(m));

            glm::ivec3 best{0};
            int bestDist2 = INT_MAX;
            for (int dz = -kMineRadius; dz <= kMineRadius; ++dz) {
                for (int dx = -kMineRadius; dx <= kMineRadius; ++dx) {
                    for (int dy = -3; dy <= 3; ++dy) {
                        const glm::ivec3 c = pos + glm::ivec3(dx, dy, dz);
                        const BlockId node = m_world->getBlock(c.x, c.y, c.z);
                        if (!isResourceNode(node)) continue;
                        if (filterNode != BlockId::Air && node != filterNode) continue;
                        const int d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 < bestDist2) {
                            bestDist2 = d2;
                            best = c;
                        }
                    }
                }
            }
            if (bestDist2 == INT_MAX) {
                m.progress = 0.0f; // nothing in reach; idle until the patch regrows
                continue;
            }

            m.crafting = true;
            m.craftTime = kMineSeconds;
            m.progress += kTickSeconds;
            if (m.progress >= kMineSeconds) {
                m.progress = 0.0f;
                const ItemStack drop = blockDrop(m_world->getBlock(best.x, best.y, best.z));
                m.output.add(drop.id, drop.count);
                m_world->setBlock(best.x, best.y, best.z, BlockId::Air);
                minedNode = true;
            }
            continue;
        }

        const MachineRecipe* active = nullptr;
        const auto candidates = recipesForMachine(m.type);
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            // A selected recipe locks the machine to it; -1 = first ready one.
            if (m.selectedRecipe >= 0 && static_cast<int>(i) != m.selectedRecipe) continue;
            bool ok = true;
            for (const ItemStack& in : candidates[i]->inputs) {
                if (!m.input.has(in.id, in.count)) { ok = false; break; }
            }
            if (ok) { active = candidates[i]; break; }
        }
        if (!active) { m.progress = 0.0f; continue; }

        m.crafting = true;
        m.craftTime = active->seconds;
        m.progress += kTickSeconds;
        if (m.progress >= active->seconds) {
            for (const ItemStack& in : active->inputs) m.input.remove(in.id, in.count);
            m.output.add(active->output.id, active->output.count);
            m.progress = 0.0f;
        }
    }

    if (minedNode) {
        rebuildMesh(); // harvested nodes disappear from the world
    }

    // Advance conduits on a slower cadence so items visibly travel.
    if (++m_beltTimer >= kBeltStepTicks) {
        m_beltTimer = 0;
        beltStep();
    }
}

bool VoxelGame::projectToScreen(const glm::vec3& world, glm::vec2& outPx) {
    const glm::vec4 clip = camera().projection() * camera().view() * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) return false; // behind the camera
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outPx.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(window().width());
    outPx.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(window().height());
    return true;
}

void VoxelGame::buildHighlightMesh() {
    // Wireframe cube centered on the origin (edges of [-0.5, 0.5]^3).
    const float h = 0.5f;
    const glm::vec3 corner[8] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h,  h}, {h, -h,  h}, {h, h,  h}, {-h, h,  h},
    };
    const int edge[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    std::vector<float> data;
    for (const auto& e : edge) {
        for (int k = 0; k < 2; ++k) {
            const glm::vec3& p = corner[e[k]];
            data.insert(data.end(), {p.x, p.y, p.z});
        }
    }
    m_highlightMesh.upload(data, {3}); // position only
}

void VoxelGame::buildCrosshairMesh() {
    // A filled '+' built from two thin quads (triangles), so it can be drawn
    // with a dark outline pass behind a light fill pass and stay visible on any
    // background. Coordinates are in NDC.
    const float l = 0.022f; // half length
    const float t = 0.0035f; // half thickness

    auto quad = [](std::vector<float>& d, float x0, float y0, float x1, float y1) {
        d.insert(d.end(), {x0, y0, 0, x1, y0, 0, x1, y1, 0,
                           x0, y0, 0, x1, y1, 0, x0, y1, 0});
    };

    std::vector<float> data;
    quad(data, -l, -t, l, t); // horizontal bar
    quad(data, -t, -l, t, l); // vertical bar
    m_crosshairMesh.upload(data, {3});
}

void VoxelGame::updateTitle() {
    const ItemId held = m_hotbar.empty() ? ItemId::None : m_hotbar[m_selectedSlot];
    window().setTitle(std::string("Voxel Factory  —  Holding: ") + itemName(held) +
                      " x" + std::to_string(m_inventory.count(held)) +
                      "   (F1 help / E craft / LMB mine / RMB place)");
}

void VoxelGame::onUpdate(float dt) {
    auto& cam = camera();

    // Machine panel: owns all input while open.
    if (m_machineUiOpen) {
        updateMachineUi();
        return;
    }

    // F5: quick-save (also happens automatically on quit).
    if (input().wasKeyPressed(SDL_SCANCODE_F5)) {
        if (saveGame()) {
            window().setTitle("Voxel Factory  —  SAVED");
        }
    }

    // Help overlay: toggle with F1. While open it freezes the world.
    if (input().wasKeyPressed(SDL_SCANCODE_F1)) {
        m_helpOpen = !m_helpOpen;
        if (m_menuOpen) {
            m_menuOpen = false;
            window().setRelativeMouse(true);
        }
    }
    if (m_helpOpen) {
        return;
    }

    // Crafting menu: toggle with E. While open it owns the input and freezes
    // the world; the cursor is released for hover/click.
    if (input().wasKeyPressed(SDL_SCANCODE_E)) {
        m_menuOpen = !m_menuOpen;
        window().setRelativeMouse(!m_menuOpen);
    }
    if (m_menuOpen) {
        updateMenu();
        return;
    }

    // Mouse look.
    cam.addLook(input().mouseRelX() * kLookSensitivity,
                -input().mouseRelY() * kLookSensitivity);

    // Walking physics: WASD on the ground plane, gravity, Space to jump.
    // There is no flight -- verticality is scaffolds, hills, and falling.
    glm::vec3 flatFront(cam.front().x, 0.0f, cam.front().z);
    if (glm::dot(flatFront, flatFront) > 1e-6f) flatFront = glm::normalize(flatFront);

    glm::vec3 wish(0.0f);
    if (input().isKeyDown(SDL_SCANCODE_W)) wish += flatFront;
    if (input().isKeyDown(SDL_SCANCODE_S)) wish -= flatFront;
    if (input().isKeyDown(SDL_SCANCODE_D)) wish += cam.right();
    if (input().isKeyDown(SDL_SCANCODE_A)) wish -= cam.right();
    float targetSpeed = 0.0f;
    if (glm::dot(wish, wish) > 0.0f) {
        targetSpeed = kWalkSpeed;
        if (input().isKeyDown(SDL_SCANCODE_LCTRL)) targetSpeed *= kSprintMult; // sprint
        wish = glm::normalize(wish);
    }

    // Momentum: accelerate toward the wanted velocity; friction to a stop
    // when there's no input.
    const glm::vec3 targetVel = wish * targetSpeed;
    const glm::vec3 delta = targetVel - m_velXZ;
    const float deltaLen = glm::length(delta);
    if (deltaLen > 0.0001f) {
        const float rate = (targetSpeed > 0.0f) ? kAccel : kDecel;
        m_velXZ += delta * (std::min(rate * dt, deltaLen) / deltaLen);
    }

    if (m_grounded && input().isKeyDown(SDL_SCANCODE_SPACE)) {
        m_velY = kJumpSpeed;
    }
    m_velY = std::max(m_velY - kGravity * dt, -kTerminalVel);

    // Axis-separated move-and-slide against the voxel grid.
    glm::vec3 feet = cam.position - glm::vec3(0.0f, kEyeHeight, 0.0f);
    glm::vec3 next = feet;
    next.x += m_velXZ.x * dt;
    if (!boxCollides(*m_world, next)) feet.x = next.x;
    else m_velXZ.x = 0.0f; // ran into a wall
    next = feet;
    next.z += m_velXZ.z * dt;
    if (!boxCollides(*m_world, next)) feet.z = next.z;
    else m_velXZ.z = 0.0f;

    m_grounded = false;
    next = feet;
    next.y += m_velY * dt;
    if (!boxCollides(*m_world, next)) {
        feet.y = next.y;
    } else if (m_velY <= 0.0f) {
        feet.y = std::floor(next.y) + 1.0f; // land: snap feet onto the block top
        m_velY = 0.0f;
        m_grounded = true;
    } else {
        m_velY = 0.0f; // bumped our head
    }

    // Fell off the island: everything in your pack is gone.
    if (feet.y < kVoidY) {
        m_inventory = Inventory{};
        feet = spawnFeet();
        m_velY = 0.0f;
        m_velXZ = glm::vec3(0.0f);
        window().setTitle("Voxel Factory  —  YOU FELL. YOUR PACK IS LOST.");
    }

    cam.position = feet + glm::vec3(0.0f, kEyeHeight, 0.0f);

    // Hotbar selection: keys 1-9 and 0 jump to the first ten slots; the mouse
    // wheel cycles through all of them (scroll up = previous).
    const int keySlots = std::min(10, static_cast<int>(m_hotbar.size()));
    for (int n = 0; n < keySlots; ++n) {
        const SDL_Scancode sc = (n < 9)
            ? static_cast<SDL_Scancode>(SDL_SCANCODE_1 + n)
            : SDL_SCANCODE_0;
        if (input().wasKeyPressed(sc)) {
            m_selectedSlot = n;
            updateTitle();
        }
    }
    const int wheel = input().wheelSteps();
    if (wheel != 0 && !m_hotbar.empty()) {
        const int n = static_cast<int>(m_hotbar.size());
        m_selectedSlot = ((m_selectedSlot - wheel) % n + n) % n;
        updateTitle();
    }

    // Aim and edit.
    const RaycastHit aim = raycastVoxel(*m_world, cam.position, cam.front(), kReach);
    m_hasTarget = aim.hit;
    m_targetBlock = aim.block;

    bool edited = false;
    if (aim.hit) {
        const glm::ivec3 tb = aim.block;

        // Mine: break the block and collect its drop.
        if (input().wasMousePressed(SDL_BUTTON_LEFT)) {
            const BlockId broken = m_world->getBlock(tb.x, tb.y, tb.z);
            if (isMachine(broken)) unregisterMachine(tb);     // returns buffered items
            if (broken == BlockId::Belt) unregisterBelt(tb);  // returns carried item
            if (isSource(broken)) m_sources.erase(tb);        // its item drops below
            if (broken == BlockId::Sapling) m_saplings.erase(tb);
            const ItemStack drop = blockDrop(broken);
            m_inventory.add(drop.id, drop.count);
            if (broken == BlockId::Leaves) {
                const std::uint32_t h = hash2(tb.x * 31 + tb.y, tb.z * 17,
                                              m_worldSeed + m_sourceRng++);
                const bool lucky = (h % 100u) <
                    static_cast<std::uint32_t>(kSaplingDropChance * 100.0f + 0.5f);
                if (lucky || ++m_leafPity >= kSaplingPityLeaves) {
                    m_leafPity = 0;
                    m_inventory.add(ItemId::SaplingItem, 1);
                }
            }
            m_world->setBlock(tb.x, tb.y, tb.z, BlockId::Air);
            edited = true;
            updateTitle();
        }
        // RMB: on a machine, open its panel (Shift+RMB to place against it
        // instead); otherwise place the held item into the empty target cell.
        if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
            const bool aimedMachine = m_machines.find(tb) != m_machines.end();
            if (aimedMachine && !input().isKeyDown(SDL_SCANCODE_LSHIFT)) {
                openMachineUi(tb);
            } else {
                const ItemId held = m_hotbar.empty() ? ItemId::None : m_hotbar[m_selectedSlot];
                const glm::ivec3 p = aim.block + aim.normal;
                const bool insidePlayer = cellOverlapsPlayer(p);
                // Saplings only take root in soil.
                const BlockId under = m_world->getBlock(p.x, p.y - 1, p.z);
                const bool soilOk = itemInfo(held).placesBlock != BlockId::Sapling ||
                                    under == BlockId::Grass || under == BlockId::Dirt;
                if (m_inventory.has(held) && !insidePlayer && soilOk &&
                    !isSolid(m_world->getBlock(p.x, p.y, p.z))) {
                    const BlockId placed = itemInfo(held).placesBlock;
                    m_world->setBlock(p.x, p.y, p.z, placed);
                    m_inventory.remove(held, 1);
                    if (isMachine(placed)) registerMachine(p, placed);
                    if (isSource(placed)) m_sources[p] = 0.0f; // starts growing a patch
                    if (placed == BlockId::Sapling) m_saplings[p] = 0.0f; // starts the grow timer
                    if (placed == BlockId::Belt) {
                        // The conduit carries items the way the player is
                        // facing -- straight up/down when looking steeply.
                        const glm::vec3 f = camera().front();
                        glm::ivec3 facing;
                        if (std::abs(f.y) > 0.7f) {
                            facing = {0, f.y > 0 ? 1 : -1, 0};
                        } else if (std::abs(f.x) > std::abs(f.z)) {
                            facing = {f.x > 0 ? 1 : -1, 0, 0};
                        } else {
                            facing = {0, 0, f.z > 0 ? 1 : -1};
                        }
                        registerBelt(p, facing);
                    }
                    edited = true;
                    updateTitle();
                }
            }
        }

        // Wrench: R re-aims the targeted conduit, cycling six directions.
        if (input().wasKeyPressed(SDL_SCANCODE_R) && m_inventory.has(ItemId::Wrench)) {
            const auto bit = m_belts.find(tb);
            if (bit != m_belts.end()) {
                static const glm::ivec3 kCycle[6] = {
                    {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
                int cur = 0;
                for (int i = 0; i < 6; ++i) {
                    if (bit->second.facing == kCycle[i]) { cur = i; break; }
                }
                bit->second.facing = kCycle[(cur + 1) % 6];
                edited = true; // re-mesh so the arrow re-aims
            }
        }
    }

    if (edited) {
        rebuildMesh();
    }
}

void VoxelGame::onRender() {
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // sky
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.use();
    m_shader.setMat4("uProj", camera().projection());
    m_shader.setMat4("uView", camera().view());
    m_shader.setVec3("uLightDir", kLightDir);
    m_atlas.bind(0);

    // World: textured + lit.
    m_shader.setInt("uUseFlatColor", 0);
    m_shader.setMat4("uModel", glm::mat4(1.0f));
    m_mesh.draw();

    // Target outline: flat wireframe cube around the aimed block.
    if (m_hasTarget) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m_targetBlock) + glm::vec3(0.5f));
        model = glm::scale(model, glm::vec3(1.01f));
        m_shader.setMat4("uModel", model);
        m_shader.setInt("uUseFlatColor", 1);
        m_shader.setVec3("uFlatColor", glm::vec3(0.04f));
        m_highlightMesh.draw(GL_LINES);
    }

    // Crosshair: screen-space '+', drawn on top with identity transforms. A
    // slightly larger dark pass forms an outline behind the light fill so it
    // stays readable over both bright sky and dark blocks.
    glDisable(GL_DEPTH_TEST);
    const float invAspect = 1.0f / camera().aspect;
    m_shader.setMat4("uProj", glm::mat4(1.0f));
    m_shader.setMat4("uView", glm::mat4(1.0f));
    m_shader.setInt("uUseFlatColor", 1);

    m_shader.setMat4("uModel", glm::scale(glm::mat4(1.0f), glm::vec3(1.7f * invAspect, 1.7f, 1.0f)));
    m_shader.setVec3("uFlatColor", glm::vec3(0.0f)); // outline
    m_crosshairMesh.draw();

    m_shader.setMat4("uModel", glm::scale(glm::mat4(1.0f), glm::vec3(invAspect, 1.0f, 1.0f)));
    m_shader.setVec3("uFlatColor", glm::vec3(0.95f)); // fill
    m_crosshairMesh.draw();
    glEnable(GL_DEPTH_TEST);

    drawHud();
    if (m_menuOpen) drawCraftMenu();
    if (m_helpOpen) drawHelp();
    if (m_machineUiOpen) drawMachineUi();
}

void VoxelGame::drawMachineUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) return;
    const Machine& mac = mit->second;

    const auto recipes = recipesForMachine(mac.type);
    const int rows = static_cast<int>(recipes.size()) + 2;
    const auto invItems = itemsOf(m_inventory);
    const auto inItems = itemsOf(mac.input);
    const auto outItems = itemsOf(mac.output);

    const int w = window().width();
    const int h = window().height();
    const PanelLayout L = panelLayout(w, h, rows, static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    m_ui.begin(w, h);
    m_ui.rect(0, 0, static_cast<float>(w), static_cast<float>(h), glm::vec4(0, 0, 0, 0.45f));
    m_ui.rect(L.px, L.py, L.panelW, L.panelH, glm::vec4(0.08f, 0.08f, 0.10f, 0.96f));

    // Header: machine name + power status.
    const bool powered = m_power.energized(m_machineUiPos.x, m_machineUiPos.y, m_machineUiPos.z);
    m_ui.text(L.px + 16, L.py + 12, 18.0f, blockName(mac.type), glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));
    m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, powered ? "POWERED" : "NO POWER",
              powered ? glm::vec4(0.4f, 0.95f, 0.45f, 1.0f) : glm::vec4(0.95f, 0.4f, 0.35f, 1.0f));

    // Action rows.
    for (int i = 0; i < rows; ++i) {
        const float ry = L.rowsY + i * PanelLayout::RowH;
        const bool selected = (i == m_machineUiSel);
        if (selected) {
            m_ui.rect(L.px + 6, ry, L.panelW - 12, PanelLayout::RowH - 2,
                      glm::vec4(0.9f, 0.75f, 0.15f, 0.85f));
        }

        std::string label;
        bool actionable = false;
        if (i == 0) {
            if (recipes.empty()) { // the Miner: status row shows its filter
                const ItemId filter = minerFilter(mac);
                label = (filter == ItemId::None)
                    ? "  MINES: ANY NEARBY NODE ( RADIUS 4 )"
                    : std::string("  MINES: ") + itemName(filter) + " ONLY ( RADIUS 4 )";
                actionable = true;
            } else {
                label = std::string(mac.selectedRecipe < 0 ? "> " : "  ") +
                        "AUTO ( FIRST READY RECIPE )";
                actionable = true;
            }
        } else if (i <= static_cast<int>(recipes.size())) {
            const MachineRecipe& r = *recipes[i - 1];
            label = std::string(mac.selectedRecipe == i - 1 ? "> " : "  ") +
                    "MAKE " + itemName(r.output.id) + "  (";
            actionable = false; // white if the player can contribute anything
            for (const ItemStack& in : r.inputs) {
                label += " " + std::string(itemName(in.id));
                if (in.count > 1) label += " x" + std::to_string(in.count);
                if (m_inventory.has(in.id, 1)) actionable = true;
            }
            label += " )";
        } else {
            label = "  TAKE OUTPUTS";
            for (int k = 1; k < static_cast<int>(ItemId::Count); ++k) {
                if (mac.output.count(static_cast<ItemId>(k)) > 0) { actionable = true; break; }
            }
        }

        const glm::vec4 col = selected ? glm::vec4(0.05f, 0.05f, 0.05f, 1.0f)
                            : actionable ? glm::vec4(0.90f, 0.90f, 0.92f, 1.0f)
                                         : glm::vec4(0.45f, 0.45f, 0.48f, 1.0f);
        m_ui.text(L.px + 16, ry + 5, 14.0f, label, col);
    }

    auto drawCell = [&](float x, float y, ItemId id, int count) {
        drawItemCell(m_ui, m_atlas, x, y, id, count);
    };

    // IN strip (highlighted as the drop target while dragging from inventory).
    if (m_drag.active() && m_drag.source == Drag::Source::PlayerInv) {
        const bool ok = machineAccepts(mac, m_drag.id);
        m_ui.rect(L.px + 8, L.inY, L.panelW - 16, PanelLayout::StripH - 4,
                  ok ? glm::vec4(0.20f, 0.55f, 0.25f, 0.45f) : glm::vec4(0.55f, 0.20f, 0.20f, 0.45f));
    }
    m_ui.text(L.px + 16, L.inY + 16, 13.0f, "IN:", glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
    for (int i = 0; i < static_cast<int>(inItems.size()); ++i) {
        drawCell(L.stripCellsX + i * (PanelLayout::Cell + PanelLayout::Gap), L.inY + 6.0f,
                 inItems[i].first, inItems[i].second);
    }

    // OUT strip.
    m_ui.text(L.px + 16, L.outY + 16, 13.0f, "OUT:", glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
    for (int i = 0; i < static_cast<int>(outItems.size()); ++i) {
        drawCell(L.stripCellsX + i * (PanelLayout::Cell + PanelLayout::Gap), L.outY + 6.0f,
                 outItems[i].first, outItems[i].second);
    }

    // Progress bar.
    m_ui.rect(L.px + 16, L.barY, L.panelW - 32, 10, glm::vec4(0.0f, 0.0f, 0.0f, 0.8f));
    if (mac.crafting) {
        const float frac = glm::clamp(mac.craftTime > 0 ? mac.progress / mac.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(L.px + 16, L.barY, (L.panelW - 32) * frac, 10, glm::vec4(0.30f, 0.90f, 0.40f, 0.95f));
    }

    // Inventory grid.
    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY", glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));
    for (int i = 0; i < static_cast<int>(invItems.size()); ++i) {
        drawCell(L.px + 16.0f + (i % PanelLayout::InvCols) * (PanelLayout::Cell + PanelLayout::Gap),
                 L.invY + (i / PanelLayout::InvCols) * (PanelLayout::Cell + PanelLayout::Gap),
                 invItems[i].first, invItems[i].second);
    }

    // Tooltip: name of the hovered cell (any of the three regions).
    ItemId hovered = ItemId::None;
    int ci;
    if ((ci = hitCell(mx, my, L.px + 16.0f, L.invY,
                      static_cast<int>(invItems.size()), PanelLayout::InvCols)) >= 0) {
        hovered = invItems[ci].first;
    } else if ((ci = hitCell(mx, my, L.stripCellsX, L.inY + 6.0f,
                             static_cast<int>(inItems.size()), 99)) >= 0) {
        hovered = inItems[ci].first;
    } else if ((ci = hitCell(mx, my, L.stripCellsX, L.outY + 6.0f,
                             static_cast<int>(outItems.size()), 99)) >= 0) {
        hovered = outItems[ci].first;
    }
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), glm::vec4(1.0f, 1.0f, 0.8f, 1.0f));
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "DRAG ITEMS: LMB STACK / RMB ONE   ROWS: CLICK OR W/S + ENTER   ESC CLOSE",
              glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));

    // Drag payload rides the cursor, drawn last so it sits on top.
    if (m_drag.active()) {
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(itemInfo(m_drag.id).atlasTile, uv0, uv1);
        const float s = 34.0f;
        m_ui.icon(m_atlas, mx - s * 0.5f, my - s * 0.5f, s, s, uv0, uv1);
        m_ui.text(mx + s * 0.35f, my + s * 0.2f, 12.0f, std::to_string(m_drag.count),
                  glm::vec4(1.0f));
    }

    m_ui.end();
}

void VoxelGame::onEscape() {
    if (m_machineUiOpen) {
        if (m_drag.active()) {
            cancelDrag(); // first Esc returns the payload; second closes
        } else {
            closeMachineUi();
        }
    } else if (m_helpOpen) {
        m_helpOpen = false;
    } else if (m_menuOpen) {
        m_menuOpen = false;
        window().setRelativeMouse(true);
    } else {
        quit();
    }
}

void VoxelGame::openMachineUi(const glm::ivec3& pos) {
    m_machineUiOpen = true;
    m_machineUiPos = pos;
    m_machineUiSel = 0;
    window().setRelativeMouse(false); // release the cursor for hover/click
}

void VoxelGame::closeMachineUi() {
    cancelDrag(); // never close with items in hand
    m_machineUiOpen = false;
    window().setRelativeMouse(true);
}

void VoxelGame::cancelDrag() {
    if (!m_drag.active()) {
        m_drag = Drag{};
        return;
    }
    if (m_drag.source == Drag::Source::PlayerInv) {
        m_inventory.add(m_drag.id, m_drag.count);
    } else {
        const auto mit = m_machines.find(m_machineUiPos);
        if (mit != m_machines.end()) {
            Inventory& buf = (m_drag.source == Drag::Source::MachineIn) ? mit->second.input
                                                                        : mit->second.output;
            buf.add(m_drag.id, m_drag.count);
        } else {
            m_inventory.add(m_drag.id, m_drag.count); // machine vanished
        }
    }
    m_drag = Drag{};
}

void VoxelGame::updateMachineUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) { // machine no longer exists
        cancelDrag();
        closeMachineUi();
        return;
    }
    Machine& mac = mit->second;

    // Action rows: AUTO, one MAKE row per recipe, then TAKE OUTPUTS.
    const auto recipes = recipesForMachine(mac.type);
    const int rows = static_cast<int>(recipes.size()) + 2;
    const auto invItems = itemsOf(m_inventory);
    const auto inItems = itemsOf(mac.input);
    const auto outItems = itemsOf(mac.output);
    const PanelLayout L = panelLayout(window().width(), window().height(), rows,
                                      static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();
    const bool inPanelX = mx >= L.px && mx <= L.px + L.panelW;

    if (input().wasKeyPressed(SDL_SCANCODE_W) || input().wasKeyPressed(SDL_SCANCODE_UP)) {
        m_machineUiSel = (m_machineUiSel - 1 + rows) % rows;
    }
    if (input().wasKeyPressed(SDL_SCANCODE_S) || input().wasKeyPressed(SDL_SCANCODE_DOWN)) {
        m_machineUiSel = (m_machineUiSel + 1) % rows;
    }

    // Hover: while the cursor moves over the rows area, it picks the row.
    if ((input().mouseRelX() != 0.0f || input().mouseRelY() != 0.0f) && inPanelX) {
        const int row = static_cast<int>((my - L.rowsY) / PanelLayout::RowH);
        if (row >= 0 && row < rows) m_machineUiSel = row;
    }
    m_machineUiSel = std::min(m_machineUiSel, rows - 1);

    const bool lmb = input().wasMousePressed(SDL_BUTTON_LEFT);
    const bool rmb = input().wasMousePressed(SDL_BUTTON_RIGHT);
    bool clickConsumed = false;

    // --- Drag pickup: LMB = whole stack, RMB = one item. ---
    if (!m_drag.active() && (lmb || rmb)) {
        int ci;
        if ((ci = hitCell(mx, my, L.px + 16.0f, L.invY,
                          static_cast<int>(invItems.size()), PanelLayout::InvCols)) >= 0) {
            const auto [id, cnt] = invItems[ci];
            const int take = lmb ? cnt : 1;
            m_inventory.remove(id, take);
            m_drag = {Drag::Source::PlayerInv, id, take};
            clickConsumed = true;
        } else if ((ci = hitCell(mx, my, L.stripCellsX, L.inY + 6.0f,
                                 static_cast<int>(inItems.size()), 99)) >= 0) {
            const auto [id, cnt] = inItems[ci];
            const int take = lmb ? cnt : 1;
            mac.input.remove(id, take);
            m_drag = {Drag::Source::MachineIn, id, take};
            clickConsumed = true;
        } else if ((ci = hitCell(mx, my, L.stripCellsX, L.outY + 6.0f,
                                 static_cast<int>(outItems.size()), 99)) >= 0) {
            const auto [id, cnt] = outItems[ci];
            const int take = lmb ? cnt : 1;
            mac.output.remove(id, take);
            m_drag = {Drag::Source::MachineOut, id, take};
            clickConsumed = true;
        }
    }

    // --- Drag drop. ---
    if (m_drag.active() && (input().wasMouseReleased(SDL_BUTTON_LEFT) ||
                            input().wasMouseReleased(SDL_BUTTON_RIGHT))) {
        const bool overIn = inPanelX && my >= L.inY && my < L.inY + PanelLayout::StripH;
        const bool overInv = inPanelX && my >= L.invY &&
                             my < L.invY + L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        if (overIn && m_drag.source == Drag::Source::PlayerInv && machineAccepts(mac, m_drag.id)) {
            mac.input.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
        } else if (overInv && m_drag.source != Drag::Source::PlayerInv) {
            m_inventory.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
        } else {
            cancelDrag(); // anywhere else: the payload goes back where it came from
        }
        updateTitle();
        clickConsumed = true;
    }

    // --- Row activation: Enter always; LMB only when over the rows area. ---
    const bool clickOnRows = lmb && !clickConsumed && inPanelX &&
                             my >= L.rowsY && my < L.rowsY + rows * PanelLayout::RowH;
    if (!m_drag.active() &&
        (input().wasKeyPressed(SDL_SCANCODE_RETURN) ||
         input().wasKeyPressed(SDL_SCANCODE_KP_ENTER) || clickOnRows)) {
        if (m_machineUiSel == 0) {
            // AUTO: run whichever recipe's inputs are ready first. (For
            // recipe-less machines like the Miner this row is informational.)
            mac.selectedRecipe = -1;
            if (!recipes.empty()) mac.progress = 0.0f;
        } else if (m_machineUiSel <= static_cast<int>(recipes.size())) {
            // MAKE row: lock the machine to this recipe and load the player's
            // matching ingredients (each input independently, so the player
            // can contribute just what they carry).
            const int idx = m_machineUiSel - 1;
            if (mac.selectedRecipe != idx) {
                mac.selectedRecipe = idx;
                mac.progress = 0.0f; // switching recipes restarts the craft
            }
            for (const ItemStack& in : recipes[idx]->inputs) {
                const int move = std::min(m_inventory.count(in.id), in.count * kLoadPerAction);
                if (move > 0) {
                    m_inventory.remove(in.id, move);
                    mac.input.add(in.id, move);
                }
            }
        } else {
            // Take all outputs.
            for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                const int c = mac.output.count(id);
                if (c > 0) {
                    mac.output.remove(id, c);
                    m_inventory.add(id, c);
                }
            }
        }
        updateTitle();
    }

    // E always closes; RMB closes only when it wasn't a pickup/drop.
    if (input().wasKeyPressed(SDL_SCANCODE_E) || (rmb && !clickConsumed && !m_drag.active())) {
        cancelDrag();
        closeMachineUi();
    }
}

bool VoxelGame::canCraft(const Recipe& r) const {
    for (const ItemStack& in : r.inputs) {
        if (!m_inventory.has(in.id, in.count)) return false;
    }
    return true;
}

void VoxelGame::tryCraft(const Recipe& r) {
    if (!canCraft(r)) return;
    for (const ItemStack& in : r.inputs) {
        m_inventory.remove(in.id, in.count);
    }
    m_inventory.add(r.output.id, r.output.count);
    updateTitle();
}

void VoxelGame::updateMenu() {
    const auto& recipes = handcraftRecipes();
    const int n = static_cast<int>(recipes.size());
    if (n == 0) return;

    const auto invItems = itemsOf(m_inventory);
    const CraftLayout L = craftLayout(window().width(), window().height(), n,
                                      static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    if (input().wasKeyPressed(SDL_SCANCODE_UP) || input().wasKeyPressed(SDL_SCANCODE_W)) {
        m_menuSelection = (m_menuSelection - 1 + n) % n;
    }
    if (input().wasKeyPressed(SDL_SCANCODE_DOWN) || input().wasKeyPressed(SDL_SCANCODE_S)) {
        m_menuSelection = (m_menuSelection + 1) % n;
    }
    const int wheel = input().wheelSteps();
    if (wheel != 0) {
        m_menuSelection = ((m_menuSelection - wheel) % n + n) % n;
    }

    // Hover: while the cursor moves over the rows area, it picks the row.
    const bool overRows = mx >= L.px && mx <= L.px + L.panelW &&
                          my >= L.rowsY && my < L.rowsY + n * CraftLayout::RowH;
    if ((input().mouseRelX() != 0.0f || input().mouseRelY() != 0.0f) && overRows) {
        m_menuSelection = static_cast<int>((my - L.rowsY) / CraftLayout::RowH);
    }

    // Enter always crafts the selection; LMB crafts the row it lands on.
    if (input().wasKeyPressed(SDL_SCANCODE_RETURN) ||
        input().wasKeyPressed(SDL_SCANCODE_KP_ENTER)) {
        tryCraft(recipes[m_menuSelection]);
    } else if (input().wasMousePressed(SDL_BUTTON_LEFT) && overRows) {
        m_menuSelection = static_cast<int>((my - L.rowsY) / CraftLayout::RowH);
        tryCraft(recipes[m_menuSelection]);
    }

    // RMB also closes (E and Esc are handled elsewhere).
    if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
        m_menuOpen = false;
        window().setRelativeMouse(true);
    }
}

void VoxelGame::drawHud() {
    const int w = window().width();
    const int h = window().height();
    m_ui.begin(w, h);

    const int n = static_cast<int>(m_hotbar.size());
    const float slot = 52.0f, gap = 6.0f, pad = 6.0f;
    const float totalW = n * slot + (n - 1) * gap;
    const float x0 = (static_cast<float>(w) - totalW) * 0.5f;
    const float y = static_cast<float>(h) - slot - 22.0f;

    for (int i = 0; i < n; ++i) {
        const ItemId item = m_hotbar[i];
        const float sx = x0 + i * (slot + gap);

        if (i == m_selectedSlot) {
            m_ui.rect(sx - 3, y - 3, slot + 6, slot + 6, glm::vec4(1.0f, 0.85f, 0.2f, 0.95f));
        }
        m_ui.rect(sx, y, slot, slot, glm::vec4(0.10f, 0.10f, 0.12f, 0.85f));

        glm::vec2 uv0, uv1;
        Atlas::uvForTile(itemInfo(item).atlasTile, uv0, uv1);
        m_ui.icon(m_atlas, sx + pad, y + pad, slot - 2 * pad, slot - 2 * pad, uv0, uv1);

        // Key label (first ten slots) and inventory count (bottom-right).
        if (i < 10) {
            const std::string key = (i < 9) ? std::to_string(i + 1) : "0";
            m_ui.text(sx + 4, y + 4, 11.0f, key, glm::vec4(0.75f, 0.75f, 0.8f, 1.0f));
        }
        const std::string cnt = std::to_string(m_inventory.count(item));
        const float th = 14.0f;
        m_ui.text(sx + slot - m_ui.textWidth(th, cnt) - 4, y + slot - th - 4, th, cnt,
                  glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // Floating progress bars over actively-crafting machines.
    for (const auto& [pos, m] : m_machines) {
        if (!m.crafting) continue;
        glm::vec2 sp;
        if (!projectToScreen(glm::vec3(pos) + glm::vec3(0.5f, 1.25f, 0.5f), sp)) continue;
        const float bw = 46.0f, bh = 7.0f;
        const float bx = sp.x - bw * 0.5f, by = sp.y - bh * 0.5f;
        m_ui.rect(bx - 1, by - 1, bw + 2, bh + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
        const float frac = glm::clamp(m.craftTime > 0 ? m.progress / m.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(bx, by, bw * frac, bh, glm::vec4(0.30f, 0.90f, 0.40f, 0.95f));
    }

    // Items currently riding on conduits, drawn as floating icons.
    for (const auto& [pos, b] : m_belts) {
        if (b.item == ItemId::None) continue;
        glm::vec2 sp;
        if (!projectToScreen(glm::vec3(pos) + glm::vec3(0.5f, 0.85f, 0.5f), sp)) continue;
        const float dist = glm::length(camera().position - (glm::vec3(pos) + glm::vec3(0.5f)));
        const float s = glm::clamp(150.0f / dist, 10.0f, 40.0f);
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(itemInfo(b.item).atlasTile, uv0, uv1);
        m_ui.icon(m_atlas, sp.x - s * 0.5f, sp.y - s * 0.5f, s, s, uv0, uv1);
    }

    // Look-at machine panel (name, input/output buffers, controls).
    if (m_hasTarget) {
        const auto mit = m_machines.find(m_targetBlock);
        if (mit != m_machines.end()) {
            const Machine& m = mit->second;
            const float pw = 380.0f, ph = 98.0f;
            const float pxp = (static_cast<float>(w) - pw) * 0.5f;
            const float pyp = y - ph - 14.0f;
            m_ui.rect(pxp, pyp, pw, ph, glm::vec4(0.07f, 0.07f, 0.09f, 0.92f));
            m_ui.text(pxp + 12, pyp + 8, 16.0f, blockName(m.type), glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));

            std::string in = "IN:";
            std::string out = "OUT:";
            for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                if (m.input.count(id) > 0)
                    in += " " + std::string(itemName(id)) + " x" + std::to_string(m.input.count(id));
                if (m.output.count(id) > 0)
                    out += " " + std::string(itemName(id)) + " x" + std::to_string(m.output.count(id));
            }
            m_ui.text(pxp + 12, pyp + 32, 13.0f, in, glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
            m_ui.text(pxp + 12, pyp + 52, 13.0f, out, glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
            m_ui.text(pxp + 12, pyp + 76, 12.0f, "RMB OPEN", glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
        }
    }

    m_ui.end();
}

void VoxelGame::drawCraftMenu() {
    const int w = window().width();
    const int h = window().height();
    const auto& recipes = handcraftRecipes();
    const int n = static_cast<int>(recipes.size());
    const auto invItems = itemsOf(m_inventory);
    const CraftLayout L = craftLayout(w, h, n, static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    m_ui.begin(w, h);

    // Dim the world behind the menu.
    m_ui.rect(0, 0, static_cast<float>(w), static_cast<float>(h), glm::vec4(0, 0, 0, 0.5f));

    m_ui.rect(L.px, L.py, L.panelW, L.panelH, glm::vec4(0.08f, 0.08f, 0.10f, 0.96f));
    m_ui.text(L.px + 16, L.py + 12, 18.0f, "CRAFTING", glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));

    for (int i = 0; i < n; ++i) {
        const Recipe& r = recipes[i];
        const float ry = L.rowsY + i * CraftLayout::RowH;
        const bool affordable = canCraft(r);
        const bool selected = (i == m_menuSelection);

        if (selected) {
            m_ui.rect(L.px + 6, ry - 1, L.panelW - 12, CraftLayout::RowH - 2,
                      glm::vec4(0.9f, 0.75f, 0.15f, 0.85f));
        }
        const glm::vec4 col = selected ? glm::vec4(0.05f, 0.05f, 0.05f, 1.0f)
                            : affordable ? glm::vec4(0.90f, 0.90f, 0.92f, 1.0f)
                                         : glm::vec4(0.45f, 0.45f, 0.48f, 1.0f);

        std::string s = itemName(r.output.id);
        if (r.output.count > 1) s += " x" + std::to_string(r.output.count);
        s += "  (";
        for (const ItemStack& in : r.inputs) {
            s += " " + std::string(itemName(in.id));
            if (in.count > 1) s += " x" + std::to_string(in.count);
        }
        s += " )   HAVE " + std::to_string(m_inventory.count(r.output.id));
        m_ui.text(L.px + 14, ry + 3, 14.0f, s, col);
    }

    // Materials on hand, with a hover tooltip.
    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY", glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));
    for (int i = 0; i < static_cast<int>(invItems.size()); ++i) {
        drawItemCell(m_ui, m_atlas,
                     L.px + 16.0f + (i % PanelLayout::InvCols) * (PanelLayout::Cell + PanelLayout::Gap),
                     L.invY + (i / PanelLayout::InvCols) * (PanelLayout::Cell + PanelLayout::Gap),
                     invItems[i].first, invItems[i].second);
    }
    const int ci = hitCell(mx, my, L.px + 16.0f, L.invY,
                           static_cast<int>(invItems.size()), PanelLayout::InvCols);
    if (ci >= 0) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(invItems[ci].first),
                  glm::vec4(1.0f, 1.0f, 0.8f, 1.0f));
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "CLICK / WHEEL / W/S + ENTER CRAFT   E CLOSE", glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));

    m_ui.end();
}

void VoxelGame::drawHelp() {
    const int w = window().width();
    const int h = window().height();

    // Each line: text + a style (0 heading, 1 body, 2 dim).
    struct Line { const char* text; int style; };
    static const Line kLines[] = {
        {"HOW TO PLAY", 0},
        {"GOAL: BREW YOUR WAY UP TO THE PHILOSOPHERS STONE, THEN", 1},
        {"TRANSMUTE NEW RESOURCE SOURCES TO EXPAND YOUR ISLAND.", 1},
        {"", 1},
        {"1. MINE NODES (LMB) AT THE GLOWING SOURCE PATCHES. THEY REGROW.", 1},
        {"2. CRAFT GEAR WITH E:  ORE > INGOT > PLATE > MACHINES.", 1},
        {"3. PLACE (RMB) A GENERATOR AND RUN WIRE. POWERED BLOCKS GLOW.", 1},
        {"4. RIGHT-CLICK A MACHINE TO OPEN IT: LOAD INPUTS, TAKE OUTPUTS.", 1},
        {"5. CONDUITS CARRY ITEMS THE WAY THEIR ARROW POINTS.", 1},
        {"6. GRINDER > CAULDRON > INFUSER > ALEMBIC > DISTILLER > TRANSMUTER", 1},
        {"7. CONDUITS ALSO RUN UP / DOWN. CRAFT A WRENCH, AIM, PRESS R TO RE-AIM.", 1},
        {"8. NO FLYING. BUILD WITH CHEAP SCAFFOLD ( STONE ) TO CLIMB.", 1},
        {"9. CHOP TREES: LOGS GIVE WOOD, LEAVES DROP SAPLINGS. REPLANT ON GRASS.", 1},
        {"10. FALL OFF THE ISLAND AND YOUR WHOLE PACK IS LOST. MIND THE EDGE.", 2},
        {"", 1},
        {"CONTROLS", 0},
        {"WASD MOVE   SPACE JUMP   LCTRL SPRINT", 1},
        {"LMB MINE   RMB PLACE   1-0 OR WHEEL SELECT", 1},
        {"E CRAFT MENU   RMB OPEN MACHINE   F5 SAVE   ESC QUIT ( AUTO SAVES )", 1},
        {"", 1},
        {"F1 OR ESC TO CLOSE", 2},
    };
    const int n = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));

    m_ui.begin(w, h);
    m_ui.rect(0, 0, static_cast<float>(w), static_cast<float>(h), glm::vec4(0, 0, 0, 0.55f));

    const float lineH = 24.0f, padY = 20.0f, panelW = 760.0f;
    const float panelH = padY * 2.0f + n * lineH;
    const float px = (static_cast<float>(w) - panelW) * 0.5f;
    const float py = (static_cast<float>(h) - panelH) * 0.5f;

    m_ui.rect(px, py, panelW, panelH, glm::vec4(0.08f, 0.08f, 0.10f, 0.96f));

    for (int i = 0; i < n; ++i) {
        const Line& line = kLines[i];
        if (!line.text[0]) continue;
        const float ly = py + padY + i * lineH;
        const float size = line.style == 0 ? 18.0f : 14.0f;
        const glm::vec4 col = line.style == 0 ? glm::vec4(1.0f, 1.0f, 0.7f, 1.0f)
                            : line.style == 2 ? glm::vec4(0.65f, 0.65f, 0.7f, 1.0f)
                                              : glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
        m_ui.text(px + 22, ly, size, line.text, col);
    }

    m_ui.end();
}
