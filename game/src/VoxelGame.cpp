#include "engine/GL.h" // GL calls in onStart/onRender

#include "game/VoxelGame.h"
#include "game/ChunkMesher.h"
#include "game/Block.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>

namespace {
    constexpr float kLookSensitivity = 0.12f; // degrees per pixel
    constexpr float kMoveSpeed = 14.0f;        // blocks per second
    constexpr float kBoostMultiplier = 3.0f;
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

    // Look down at the demo scene from one corner.
    camera().position = {8.0f, 18.0f, 36.0f};
    camera().yaw = -90.0f;
    camera().pitch = -28.0f;
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
    if (input().isKeyDown(SDL_SCANCODE_SPACE))  dir += kWorldUp;
    if (input().isKeyDown(SDL_SCANCODE_LCTRL))   dir -= kWorldUp;

    if (glm::dot(dir, dir) > 0.0f) {
        float speed = kMoveSpeed;
        if (input().isKeyDown(SDL_SCANCODE_LSHIFT)) speed *= kBoostMultiplier;
        cam.position += glm::normalize(dir) * speed * dt;
    }
}

void VoxelGame::onRender() {
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f); // sky
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.use();
    m_shader.setMat4("uProj", camera().projection());
    m_shader.setMat4("uView", camera().view());
    m_shader.setMat4("uModel", glm::mat4(1.0f));
    m_shader.setVec3("uLightDir", kLightDir);

    m_mesh.draw();
}
