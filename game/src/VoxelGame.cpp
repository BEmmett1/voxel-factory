#include "engine/GL.h" // GL calls in onStart/onRender

#include "game/VoxelGame.h"
#include "game/ChunkMesher.h"
#include "game/Raycast.h"
#include "game/Block.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

namespace {
    constexpr float kLookSensitivity = 0.12f; // degrees per pixel
    constexpr float kMoveSpeed = 14.0f;        // blocks per second
    constexpr float kBoostMultiplier = 3.0f;
    constexpr float kReach = 8.0f;             // how far you can target blocks
    const glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
    const glm::vec3 kLightDir = glm::normalize(glm::vec3{-0.4f, -1.0f, -0.3f});
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

    m_chunk = std::make_unique<Chunk>();
    buildWorld();
    rebuildMesh();
    buildHighlightMesh();

    // Stand near the demo structures so blocks are within reach to edit.
    camera().position = {8.0f, 5.0f, 10.0f};
    camera().yaw = -90.0f;
    camera().pitch = -20.0f;

    updateTitle();
}

void VoxelGame::buildWorld() {
    // Ground: stone, then dirt, then a grass top across the whole chunk.
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            m_chunk->set(x, 0, z, BlockId::Stone);
            m_chunk->set(x, 1, z, BlockId::Dirt);
            m_chunk->set(x, 2, z, BlockId::Grass);
        }
    }

    // A preview of the automation theme sitting on top of the grass (y = 3):
    // generator -> wire -> machine, with a parallel belt line.
    m_chunk->set(3, 3, 3, BlockId::Generator);
    for (int x = 4; x <= 8; ++x) {
        m_chunk->set(x, 3, 3, BlockId::Wire);
    }
    m_chunk->set(9, 3, 3, BlockId::Machine);

    for (int x = 3; x <= 9; ++x) {
        m_chunk->set(x, 3, 6, BlockId::Belt);
    }

    // A couple of stone pillars for vertical reference.
    for (int y = 3; y <= 6; ++y) {
        m_chunk->set(12, y, 12, BlockId::Stone);
        m_chunk->set(13, y, 12, BlockId::Stone);
    }
}

void VoxelGame::rebuildMesh() {
    const std::vector<float> data = ChunkMesher::build(*m_chunk, glm::vec3(0.0f));
    m_mesh.upload(data, {3, 3, 3}); // position, normal, color
    m_chunk->clearDirty();
}

void VoxelGame::buildHighlightMesh() {
    // A wireframe cube centered on the origin (edges of [-0.5, 0.5]^3). Normal
    // and color slots are present to match the shared vertex layout but unused
    // (the highlight draws with the shader's flat-color path).
    const float h = 0.5f;
    const glm::vec3 corner[8] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h,  h}, {h, -h,  h}, {h, h,  h}, {-h, h,  h},
    };
    const int edge[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // bottom
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // top
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // verticals
    };

    std::vector<float> data;
    data.reserve(12 * 2 * 9);
    for (const auto& e : edge) {
        for (int k = 0; k < 2; ++k) {
            const glm::vec3& p = corner[e[k]];
            data.insert(data.end(), {p.x, p.y, p.z, 0, 0, 0, 0, 0, 0});
        }
    }
    m_highlightMesh.upload(data, {3, 3, 3});
}

void VoxelGame::updateTitle() {
    window().setTitle(std::string("Voxel Factory  —  [") + blockName(m_selectedBlock) +
                      "]   (LMB break / RMB place / 1-7 select)");
}

void VoxelGame::onUpdate(float dt) {
    auto& cam = camera();

    // Mouse look.
    cam.addLook(input().mouseRelX() * kLookSensitivity,
                -input().mouseRelY() * kLookSensitivity);

    // Fly movement. Forward follows the look direction; vertical is world up.
    glm::vec3 dir(0.0f);
    if (input().isKeyDown(SDL_SCANCODE_W)) dir += cam.front();
    if (input().isKeyDown(SDL_SCANCODE_S)) dir -= cam.front();
    if (input().isKeyDown(SDL_SCANCODE_D)) dir += cam.right();
    if (input().isKeyDown(SDL_SCANCODE_A)) dir -= cam.right();
    if (input().isKeyDown(SDL_SCANCODE_SPACE)) dir += kWorldUp;
    if (input().isKeyDown(SDL_SCANCODE_LCTRL)) dir -= kWorldUp;

    if (glm::dot(dir, dir) > 0.0f) {
        float speed = kMoveSpeed;
        if (input().isKeyDown(SDL_SCANCODE_LSHIFT)) speed *= kBoostMultiplier;
        cam.position += glm::normalize(dir) * speed * dt;
    }

    // Block selection: number keys 1-7 map to Grass..Belt.
    for (int n = 1; n <= 7; ++n) {
        const SDL_Scancode sc = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (n - 1));
        if (input().wasKeyPressed(sc)) {
            m_selectedBlock = static_cast<BlockId>(n);
            updateTitle();
        }
    }

    // Aim: cast from the eye along the look direction to the first solid block.
    const RaycastHit aim = raycastVoxel(*m_chunk, cam.position, cam.front(), kReach);
    m_hasTarget = aim.hit;
    m_targetBlock = aim.block;

    if (aim.hit) {
        // Break the targeted block.
        if (input().wasMousePressed(SDL_BUTTON_LEFT)) {
            m_chunk->set(aim.block.x, aim.block.y, aim.block.z, BlockId::Air);
        }
        // Place the selected block against the face we are looking at.
        if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
            const glm::ivec3 p = aim.block + aim.normal;
            if (m_chunk->inBounds(p.x, p.y, p.z) && !isSolid(m_chunk->get(p.x, p.y, p.z))) {
                m_chunk->set(p.x, p.y, p.z, m_selectedBlock);
            }
        }
    }

    if (m_chunk->dirty()) {
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

    // World mesh: lit.
    m_shader.setInt("uUseFlatColor", 0);
    m_shader.setMat4("uModel", glm::mat4(1.0f));
    m_mesh.draw();

    // Target outline: a flat-colored wireframe cube around the aimed block,
    // scaled out slightly to sit just proud of the block faces.
    if (m_hasTarget) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m_targetBlock) + glm::vec3(0.5f));
        model = glm::scale(model, glm::vec3(1.01f));
        m_shader.setMat4("uModel", model);
        m_shader.setInt("uUseFlatColor", 1);
        m_shader.setVec3("uFlatColor", glm::vec3(0.04f));
        m_highlightMesh.draw(GL_LINES);
    }
}
