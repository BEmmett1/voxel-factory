// Everything that touches the GPU: the texture atlas (painted or generated),
// per-chunk remeshing, the static highlight/crosshair meshes, the per-frame
// rain mesh, and the onRender pass itself.

#include "engine/GL.h" // must precede other GL-touching headers

#include "engine/Image.h"
#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/ChunkMesher.h"
#include "game/Atlas.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace vg;

namespace {

    // Cheap deterministic per-texel noise for the procedural atlas.
    float texelNoise(int id, int px, int py) {
        std::uint32_t h = static_cast<std::uint32_t>(px) * 73856093u ^
                          static_cast<std::uint32_t>(py) * 19349663u ^
                          static_cast<std::uint32_t>(id) * 83492791u;
        return (static_cast<float>(h % 1000u) / 1000.0f - 0.5f) * 0.25f;
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

} // namespace

// The shaped-block sheet, baked by tools/bbmodel_to_shape.py. Unlike the
// atlas there is no generated fallback and no expected size (shape UVs are
// absolute, so any dimensions load): if it is missing, shaped blocks simply
// don't draw and say so once. Silently substituting the atlas would texture
// them with whatever tiles happened to line up, which is harder to diagnose
// than nothing at all.
void VoxelGame::buildShapeSheet() {
    const char* base = SDL_GetBasePath(); // owned by SDL, do not free
    const std::string path = (base ? std::string(base) : "") + "assets/shapes.png";
    engine::Image img;
    if (!engine::loadImage(path, img)) {
        SDL_Log("assets/shapes.png missing or unreadable -- shaped blocks will not draw");
        return;
    }
    m_shapes.createFromPixels(img.width, img.height, img.rgba.data());
    m_shapesReady = true;
}

void VoxelGame::buildAtlas() {
    // Prefer the hand-paintable atlas file; fall back to generated color
    // swatches so the game always runs (and a broken PNG is loud, not fatal).
    {
        const char* base = SDL_GetBasePath(); // owned by SDL, do not free
        const std::string path = (base ? std::string(base) : "") + "assets/atlas.png";
        engine::Image img;
        if (engine::loadImage(path, img)) {
            if (img.width == Atlas::WidthPx && img.height == Atlas::HeightPx) {
                m_atlas.createFromPixels(img.width, img.height, img.rgba.data());
                return;
            }
            SDL_Log("assets/atlas.png is %dx%d, want %dx%d -- using generated tiles",
                    img.width, img.height, Atlas::WidthPx, Atlas::HeightPx);
        }
    }

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

    // Block tiles: every face tile gets the block's color (a tile shared by
    // several faces or blocks is just filled more than once).
    for (int id = 1; id < static_cast<int>(BlockId::Count); ++id) {
        const Atlas::BlockTiles& t = Atlas::tilesForBlock(static_cast<BlockId>(id));
        const glm::vec3 color = blockInfo(static_cast<BlockId>(id)).color;
        fillTile(t.top, color);
        fillTile(t.side, color);
        fillTile(t.bottom, color);
    }

    // Material icon tiles get a distinct hue spaced around the wheel
    // (placeables have no tile of their own; iconTile() borrows the block's).
    for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
        const ItemInfo& info = itemInfo(static_cast<ItemId>(i));
        if (info.atlasTile < 0) continue;
        fillTile(info.atlasTile,
                 hsvColor(std::fmod(static_cast<float>(i) * 0.61803398875f, 1.0f), 0.55f, 0.85f));
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

// Rain: short world-space streaks falling around the camera, skipping covered
// columns so weather stays outside. Rebuilt every frame while visible.
void VoxelGame::buildRainMesh() {
    m_rainScratch.clear();
    // Rain follows the Overworld weather — except the Tempest's arena, whose
    // storm rages at full intensity for the whole fight.
    const float intensity = m_dimension == DimensionId::Overworld
        ? m_weather.intensity : (m_arenaStorm ? 1.0f : 0.0f);
    const int count = static_cast<int>(static_cast<float>(kRainStreaks) * intensity);
    if (count > 0) {
        const glm::vec3 cam = camera().position;
        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        for (int i = 0; i < count; ++i) {
            const float ox = (static_cast<float>(hash2(i, 3, 51) % 1024u) / 1023.0f - 0.5f) *
                             2.0f * kRainRadius;
            const float oz = (static_cast<float>(hash2(7, i, 52) % 1024u) / 1023.0f - 0.5f) *
                             2.0f * kRainRadius;
            const float phase = static_cast<float>(hash2(i, i, 53) % 1024u) / 1023.0f * kRainSpan;
            const float x = cam.x + ox;
            const float z = cam.z + oz;
            const float y = cam.y + kRainSpan * 0.5f -
                            std::fmod(t * kRainFallSpeed + phase, kRainSpan);
            if (!skyVisible(*m_world, static_cast<int>(std::floor(x)),
                            static_cast<int>(std::floor(y)),
                            static_cast<int>(std::floor(z)))) {
                continue; // under a roof/canopy: the drop already landed
            }
            m_rainScratch.insert(m_rainScratch.end(),
                                 {x, y, z, x, y + kRainStreakLen, z});
        }
    }
    m_rainMesh.upload(m_rainScratch, {3}, GL_DYNAMIC_DRAW);
}

// Rebuild only the chunks whose contents changed. Runs once per frame (top of
// onRender), so any number of tick/edit mutations in the frame collapse into
// at most one rebuild per touched chunk.
void VoxelGame::remeshDirtyChunks() {
    const std::uint64_t t0 = SDL_GetPerformanceCounter();
    int chunks = 0;
    // The power glow and belt arrows are Overworld state; arena chunks mesh
    // against empty sets (coordinates overlap numerically across dimensions).
    static const PowerState kNoPower;
    static const ChunkMesher::BeltMap kNoBelts;
    const bool home = m_dimension == DimensionId::Overworld;
    const PowerState& power = home ? m_power : kNoPower;
    const ChunkMesher::BeltMap& belts = home ? m_belts : kNoBelts;
    for (const auto& [coord, chunk] : m_world->chunks()) {
        if (!chunk->dirty()) continue;
        m_meshScratch.clear();
        m_shapeScratch.clear();
        ChunkMesher::appendChunk(m_meshScratch, m_shapeScratch, *m_world, *chunk,
                                 coord, power, belts);
        // Empty chunks keep their (vertexless) entry; draw() skips them.
        m_chunkMeshes[coord].upload(m_meshScratch, {3, 3, 2, 1}, // pos, normal, uv, emissive
                                    GL_DYNAMIC_DRAW);
        m_chunkShapeMeshes[coord].upload(m_shapeScratch, {3, 3, 2, 1},
                                         GL_DYNAMIC_DRAW);
        chunk->clearDirty();
        ++chunks;
    }
    if (chunks > 0) {
        m_perf.lastRemeshMs = msBetween(t0, SDL_GetPerformanceCounter());
        m_perf.chunksRemeshed = chunks;
        ++m_perf.remeshCount;
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

void VoxelGame::onRender() {
    // Everything the ticks and the update dirtied this frame, in one sweep.
    remeshDirtyChunks();
    buildRainMesh();

    // Sky: fair-weather blue easing toward storm grey — or the arena's flat
    // void purple-black. Rain dimming applies at home only.
    const bool home = m_dimension == DimensionId::Overworld;
    const glm::vec3 sky = home
        ? glm::mix(glm::vec3(0.53f, 0.81f, 0.92f),
                   glm::vec3(0.44f, 0.47f, 0.52f), m_weather.intensity)
        : (m_arenaStorm ? glm::vec3(0.16f, 0.17f, 0.26f)   // storm-lashed slate
                        : glm::vec3(0.09f, 0.05f, 0.14f)); // dead void purple
    const float rainDim = home ? m_weather.intensity * kRainDimMax
                               : (m_arenaStorm ? kRainDimMax : 0.0f);
    glClearColor(sky.r, sky.g, sky.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.use();
    m_shader.setMat4("uProj", camera().projection());
    m_shader.setMat4("uView", camera().view());
    m_shader.setVec3("uLightDir", kLightDir);
    m_shader.setFloat("uRainDim", rainDim);
    m_atlas.bind(0);

    // World: textured + lit, one small draw per chunk (world-space vertices).
    m_shader.setInt("uUseFlatColor", 0);
    m_shader.setMat4("uModel", glm::mat4(1.0f));
    for (auto& [coord, mesh] : m_chunkMeshes) {
        mesh.draw();
    }

    // Shaped blocks: same shader and uniforms, second sheet. Skipped whole
    // when nothing in the world carries a ShapeId.
    if (m_shapesReady) {
        m_shapes.bind(0);
        for (auto& [coord, mesh] : m_chunkShapeMeshes) {
            mesh.draw();
        }
        m_atlas.bind(0);
    }

    // Target outline: flat wireframe cube around the aimed block.
    if (m_hasTarget) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m_targetBlock) + glm::vec3(0.5f));
        model = glm::scale(model, glm::vec3(1.01f));
        m_shader.setMat4("uModel", model);
        m_shader.setInt("uUseFlatColor", 1);
        m_shader.setVec3("uFlatColor", glm::vec3(0.04f));
        m_highlightMesh.draw(GL_LINES);
    }

    // Rain streaks: flat-colored world-space lines, hidden behind geometry by
    // the depth test (indoors stays dry-looking).
    if (!m_rainMesh.empty()) {
        m_shader.setMat4("uModel", glm::mat4(1.0f));
        m_shader.setInt("uUseFlatColor", 1);
        m_shader.setVec3("uFlatColor", glm::vec3(0.62f, 0.68f, 0.78f));
        m_rainMesh.draw(GL_LINES);
    }

    // Creatures: skinned Blockbench models, depth-tested with the world.
    m_creatures.render(camera(), rainDim, m_dimension);
    m_shader.use(); // the crosshair pass below assumes the voxel shader

    // Main menu shell (launch): the empty world above is just a sky backdrop —
    // draw the menu, no crosshair or HUD behind it.
    if (m_shellOpen) {
        if (m_settingsOpen) drawSettingsUi();
        else if (m_slotPickerOpen) drawSlotPicker();
        else drawMainMenu();
        if (m_debugOpen) drawDebugOverlay();
        return;
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
    if (m_invOpen) drawInventoryUi();
    if (m_helpOpen) drawHelp();
    if (m_machineUiOpen) drawMachineUi();
    if (m_pauseOpen) {
        if (m_settingsOpen) drawSettingsUi();
        else drawPauseMenu();
    }
    if (m_debugOpen) drawDebugOverlay();
}
