#include "engine/GL.h" // GL calls in onStart/onRender

#include "game/VoxelGame.h"
#include "game/ChunkMesher.h"
#include "game/Raycast.h"
#include "game/Block.h"
#include "game/Atlas.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {
    constexpr float kLookSensitivity = 0.12f; // degrees per pixel
    constexpr float kMoveSpeed = 14.0f;        // blocks per second
    constexpr float kBoostMultiplier = 3.0f;
    constexpr float kReach = 8.0f;             // how far you can target blocks
    constexpr int   kWorldChunks = 4;          // NxN ground chunks => 64x64 area
    const glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
    const glm::vec3 kLightDir = glm::normalize(glm::vec3{-0.4f, -1.0f, -0.3f});

    // Cheap deterministic per-texel noise for the procedural atlas.
    float texelNoise(int id, int px, int py) {
        std::uint32_t h = static_cast<std::uint32_t>(px) * 73856093u ^
                          static_cast<std::uint32_t>(py) * 19349663u ^
                          static_cast<std::uint32_t>(id) * 83492791u;
        return (static_cast<float>(h % 1000u) / 1000.0f - 0.5f) * 0.25f;
    }

    // Deterministic hash for scattering resource nodes by world (x, z).
    std::uint32_t hash2(int x, int z) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                          static_cast<std::uint32_t>(z) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
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

    m_world = std::make_unique<World>();
    buildWorld();
    rebuildMesh();
    buildHighlightMesh();
    buildCrosshairMesh();
    m_ui.init();

    // Stand near the demo structures so blocks are within reach to edit.
    camera().position = {8.0f, 5.0f, 10.0f};
    camera().yaw = -90.0f;
    camera().pitch = -20.0f;

    // Placeable hotbar (number keys). All placeables are hand-crafted, so the
    // player starts with raw materials and a head start of a few of each raw.
    m_hotbar = {ItemId::Conduit, ItemId::WireItem, ItemId::GeneratorItem,
                ItemId::GrinderItem, ItemId::CauldronItem, ItemId::InfuserItem,
                ItemId::AlembicItem, ItemId::MinerItem};
    m_inventory.add(ItemId::CopperOre, 30);
    m_inventory.add(ItemId::Stone, 12);
    m_inventory.add(ItemId::Sand, 10);
    m_inventory.add(ItemId::Crystal, 8);
    m_inventory.add(ItemId::Herb, 6);
    m_inventory.add(ItemId::SpringWater, 4);
    m_inventory.add(ItemId::Essence, 2);

    updateTitle();
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

    m_atlas.createFromPixels(Atlas::WidthPx, Atlas::HeightPx, pixels.data());
}

void VoxelGame::buildWorld() {
    const int extent = kWorldChunks * CHUNK_SIZE;

    // Flat terrain: stone, dirt, grass top.
    for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
            m_world->setBlock(x, 0, z, BlockId::Stone);
            m_world->setBlock(x, 1, z, BlockId::Dirt);
            m_world->setBlock(x, 2, z, BlockId::Grass);
        }
    }

    // Automation preview on top of the grass (y = 3):
    // generator -> wire -> grinder, plus a parallel conduit line.
    m_world->setBlock(3, 3, 3, BlockId::Generator);
    for (int x = 4; x <= 8; ++x) {
        m_world->setBlock(x, 3, 3, BlockId::Wire);
    }
    m_world->setBlock(9, 3, 3, BlockId::Grinder);

    for (int x = 3; x <= 9; ++x) {
        m_world->setBlock(x, 3, 6, BlockId::Belt);
    }

    // Scatter mineable resource nodes across the surface, keeping the demo
    // area clear so the power preview stays readable.
    for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
            if (x >= 2 && x <= 14 && z >= 2 && z <= 13) continue;
            const std::uint32_t h = hash2(x, z);
            BlockId node = BlockId::Air;
            if      (h % 17 == 0)  node = BlockId::HerbBush;
            else if (h % 23 == 0)  node = BlockId::CopperOre;
            else if (h % 47 == 0)  node = BlockId::SandNode;
            else if (h % 89 == 0)  node = BlockId::CrystalNode;
            else if (h % 131 == 0) node = BlockId::WaterSource;
            else if (h % 211 == 0) node = BlockId::EssenceVent;
            if (node != BlockId::Air) m_world->setBlock(x, 3, z, node);
        }
    }
}

void VoxelGame::rebuildMesh() {
    // Power state depends only on topology, so recompute it whenever geometry
    // changes. One combined buffer holds the whole world for now.
    m_power = PowerSystem::solve(*m_world);

    std::vector<float> data;
    for (const auto& [coord, chunk] : m_world->chunks()) {
        (void)chunk;
        ChunkMesher::appendChunk(data, *m_world, coord, m_power);
    }
    m_mesh.upload(data, {3, 3, 2, 1}); // position, normal, uv, emissive
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
                      "   (LMB mine / RMB place / 1-" + std::to_string(m_hotbar.size()) +
                      " select)");
}

void VoxelGame::onUpdate(float dt) {
    auto& cam = camera();

    // Crafting menu: toggle with E. While open it owns the input and freezes
    // the world (no look / move / mine / place).
    if (input().wasKeyPressed(SDL_SCANCODE_E)) {
        m_menuOpen = !m_menuOpen;
    }
    if (m_menuOpen) {
        updateMenu();
        return;
    }

    // Mouse look.
    cam.addLook(input().mouseRelX() * kLookSensitivity,
                -input().mouseRelY() * kLookSensitivity);

    // Movement: horizontal on WASD, vertical on Space (up) / Left Shift (down).
    glm::vec3 flatFront(cam.front().x, 0.0f, cam.front().z);
    if (glm::dot(flatFront, flatFront) > 1e-6f) flatFront = glm::normalize(flatFront);

    glm::vec3 dir(0.0f);
    if (input().isKeyDown(SDL_SCANCODE_W)) dir += flatFront;
    if (input().isKeyDown(SDL_SCANCODE_S)) dir -= flatFront;
    if (input().isKeyDown(SDL_SCANCODE_D)) dir += cam.right();
    if (input().isKeyDown(SDL_SCANCODE_A)) dir -= cam.right();
    if (input().isKeyDown(SDL_SCANCODE_SPACE))  dir += kWorldUp; // up
    if (input().isKeyDown(SDL_SCANCODE_LSHIFT)) dir -= kWorldUp; // down

    if (glm::dot(dir, dir) > 0.0f) {
        float speed = kMoveSpeed;
        if (input().isKeyDown(SDL_SCANCODE_LCTRL)) speed *= kBoostMultiplier; // sprint
        cam.position += glm::normalize(dir) * speed * dt;
    }

    // Hotbar selection: number keys pick a placeable item.
    for (int n = 1; n <= static_cast<int>(m_hotbar.size()); ++n) {
        const SDL_Scancode sc = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (n - 1));
        if (input().wasKeyPressed(sc)) {
            m_selectedSlot = n - 1;
            updateTitle();
        }
    }

    // Aim and edit.
    const RaycastHit aim = raycastVoxel(*m_world, cam.position, cam.front(), kReach);
    m_hasTarget = aim.hit;
    m_targetBlock = aim.block;

    bool edited = false;
    if (aim.hit) {
        // Mine: break the block and collect its drop.
        if (input().wasMousePressed(SDL_BUTTON_LEFT)) {
            const BlockId broken = m_world->getBlock(aim.block.x, aim.block.y, aim.block.z);
            const ItemStack drop = blockDrop(broken);
            m_inventory.add(drop.id, drop.count);
            m_world->setBlock(aim.block.x, aim.block.y, aim.block.z, BlockId::Air);
            edited = true;
            updateTitle();
        }
        // Place: consume the held item if available and the target cell is empty.
        if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
            const ItemId held = m_hotbar.empty() ? ItemId::None : m_hotbar[m_selectedSlot];
            const glm::ivec3 p = aim.block + aim.normal;
            if (m_inventory.has(held) && !isSolid(m_world->getBlock(p.x, p.y, p.z))) {
                m_world->setBlock(p.x, p.y, p.z, itemInfo(held).placesBlock);
                m_inventory.remove(held, 1);
                edited = true;
                updateTitle();
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
}

void VoxelGame::onEscape() {
    if (m_menuOpen) {
        m_menuOpen = false;
    } else {
        quit();
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

    if (input().wasKeyPressed(SDL_SCANCODE_UP) || input().wasKeyPressed(SDL_SCANCODE_W)) {
        m_menuSelection = (m_menuSelection - 1 + n) % n;
    }
    if (input().wasKeyPressed(SDL_SCANCODE_DOWN) || input().wasKeyPressed(SDL_SCANCODE_S)) {
        m_menuSelection = (m_menuSelection + 1) % n;
    }
    if (input().wasKeyPressed(SDL_SCANCODE_RETURN) ||
        input().wasKeyPressed(SDL_SCANCODE_KP_ENTER)) {
        tryCraft(recipes[m_menuSelection]);
    }
}

void VoxelGame::drawHud() {
    const int w = window().width();
    const int h = window().height();
    m_ui.begin(w, h);

    const int n = static_cast<int>(m_hotbar.size());
    const float slot = 64.0f, gap = 8.0f, pad = 7.0f;
    const float totalW = n * slot + (n - 1) * gap;
    const float x0 = (static_cast<float>(w) - totalW) * 0.5f;
    const float y = static_cast<float>(h) - slot - 24.0f;

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

        // Slot number (top-left) and inventory count (bottom-right).
        m_ui.text(sx + 4, y + 4, 12.0f, std::to_string(i + 1), glm::vec4(0.75f, 0.75f, 0.8f, 1.0f));
        const std::string cnt = std::to_string(m_inventory.count(item));
        const float th = 16.0f;
        m_ui.text(sx + slot - m_ui.textWidth(th, cnt) - 5, y + slot - th - 4, th, cnt,
                  glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    m_ui.end();
}

void VoxelGame::drawCraftMenu() {
    const int w = window().width();
    const int h = window().height();
    const auto& recipes = handcraftRecipes();
    const int n = static_cast<int>(recipes.size());

    m_ui.begin(w, h);

    // Dim the world behind the menu.
    m_ui.rect(0, 0, static_cast<float>(w), static_cast<float>(h), glm::vec4(0, 0, 0, 0.5f));

    const float rowH = 26.0f, headerH = 44.0f, footerH = 30.0f, panelW = 680.0f;
    const float panelH = headerH + n * rowH + footerH;
    const float px = (static_cast<float>(w) - panelW) * 0.5f;
    const float py = (static_cast<float>(h) - panelH) * 0.5f;

    m_ui.rect(px, py, panelW, panelH, glm::vec4(0.08f, 0.08f, 0.10f, 0.96f));
    m_ui.text(px + 16, py + 14, 20.0f, "CRAFTING", glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));

    for (int i = 0; i < n; ++i) {
        const Recipe& r = recipes[i];
        const float ry = py + headerH + i * rowH;
        const bool affordable = canCraft(r);
        const bool selected = (i == m_menuSelection);

        if (selected) {
            m_ui.rect(px + 6, ry - 2, panelW - 12, rowH - 2, glm::vec4(0.9f, 0.75f, 0.15f, 0.85f));
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
        m_ui.text(px + 14, ry + 3, 16.0f, s, col);
    }

    m_ui.text(px + 16, py + panelH - footerH + 6, 13.0f,
              "W/S SELECT   ENTER CRAFT   E CLOSE", glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));

    m_ui.end();
}
