#include "engine/GL.h" // GL calls in onStart/onRender

#include "game/VoxelGame.h"
#include "game/ChunkMesher.h"
#include "game/Raycast.h"
#include "game/Block.h"
#include "game/Atlas.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

    // Stand near the demo structures so blocks are within reach to edit.
    camera().position = {8.0f, 5.0f, 10.0f};
    camera().yaw = -90.0f;
    camera().pitch = -20.0f;

    // Placeable hotbar (number keys) + a starting stock to build with until the
    // crafting menu exists.
    m_hotbar = {ItemId::Conduit, ItemId::WireItem, ItemId::GeneratorItem,
                ItemId::GrinderItem, ItemId::CauldronItem, ItemId::InfuserItem,
                ItemId::AlembicItem, ItemId::MinerItem};
    m_inventory.add(ItemId::Conduit, 40);
    m_inventory.add(ItemId::WireItem, 40);
    m_inventory.add(ItemId::GeneratorItem, 8);
    m_inventory.add(ItemId::GrinderItem, 8);
    m_inventory.add(ItemId::CauldronItem, 6);
    m_inventory.add(ItemId::InfuserItem, 4);
    m_inventory.add(ItemId::AlembicItem, 4);
    m_inventory.add(ItemId::MinerItem, 8);

    updateTitle();
}

void VoxelGame::buildAtlas() {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(Atlas::WidthPx) * Atlas::HeightPx * 4, 0);

    for (int id = 1; id < static_cast<int>(BlockId::Count); ++id) {
        const glm::vec3 baseColor = blockInfo(static_cast<BlockId>(id)).color;
        const int col = id % Atlas::Cols;
        const int row = id / Atlas::Cols;
        const int x0 = col * Atlas::TilePx;
        const int y0 = row * Atlas::TilePx;

        for (int py = 0; py < Atlas::TilePx; ++py) {
            for (int px = 0; px < Atlas::TilePx; ++px) {
                // Darken a 1px border so block edges read clearly.
                const bool edge = px == 0 || py == 0 ||
                                  px == Atlas::TilePx - 1 || py == Atlas::TilePx - 1;
                const float edgeMul = edge ? 0.6f : 1.0f;
                const float n = 1.0f + texelNoise(id, px, py);

                glm::vec3 c = glm::clamp(baseColor * n * edgeMul, 0.0f, 1.0f);

                const std::size_t idx =
                    (static_cast<std::size_t>(y0 + py) * Atlas::WidthPx + (x0 + px)) * 4;
                pixels[idx + 0] = static_cast<unsigned char>(c.r * 255.0f);
                pixels[idx + 1] = static_cast<unsigned char>(c.g * 255.0f);
                pixels[idx + 2] = static_cast<unsigned char>(c.b * 255.0f);
                pixels[idx + 3] = 255;
            }
        }
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
}
