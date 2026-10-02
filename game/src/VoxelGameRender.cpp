// Everything that touches the GPU: the texture atlas (painted or generated),
// per-chunk remeshing, the static highlight/crosshair meshes, the per-frame
// rain mesh, and the onRender pass itself.

#include "engine/GL.h" // must precede other GL-touching headers

#include "engine/Image.h"
#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/ChunkMesher.h"
#include "game/AlchemyCircle.h"
#include "game/Atlas.h"
#include "game/BlockShape.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

using namespace vg;

// The shader indexes uAnimV[] by ShapeId with no bounds check, so growing
// ShapeId past the bank cap has to be a compile error, not a silent read off
// the end of a uniform array.
static_assert(static_cast<int>(ShapeId::Count) <= kMaxShapeBanks,
              "uAnimV[] in voxel.vert needs one slot per ShapeId — "
              "raise vg::kMaxShapeBanks and the array size in the shader");

// Same shape of hazard for uPartRot[]: the shader indexes it by the slot baked
// into the vertex, so one animated part too many would read off the end of a
// uniform array. Slot 0 is identity, hence the + 1.
static_assert(static_cast<int>(std::size(kPartAnims)) + 1 <= kMaxShapeParts,
              "uPartRot[] in voxel.vert needs a slot per animated part — "
              "raise vg::kMaxShapeParts and the array size in the shader");

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
    for (int id = 1; id < static_cast<int>(blockCount()); ++id) {
        const Atlas::BlockTiles& t = Atlas::tilesForBlock(static_cast<BlockId>(id));
        const glm::vec3 color = blockInfo(static_cast<BlockId>(id)).color;
        fillTile(t.top, color);
        fillTile(t.side, color);
        fillTile(t.bottom, color);
    }

    // Material icon tiles get a distinct hue spaced around the wheel
    // (placeables have no tile of their own; iconTile() borrows the block's).
    for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
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

// Conduit cargo, as world geometry: one camera-facing quad per carried item,
// through the ordinary voxel shader so it is DEPTH-TESTED like everything else.
// It used to be a UiRenderer billboard drawn after the world, which meant an
// item behind a wall drew straight through it, at any distance.
//
// Two things it gets for free by being here. Alpha cutout finally does the job
// it shipped for: an item icon's transparent surround is discarded rather than
// drawn as a black card. And the distance cull is the ground drops' own knob,
// so cargo and dropped items disappear at the same range instead of one of
// them being visible across the island.
//
// Alchemy Circle contents ride the same buffer and the same draw: whatever a
// Pedestal holds floats over it as a small fanned stack, and the Rune Core's
// centre catalyst floats over the core, so a laid pattern reads from across
// the room without opening the panel.
void VoxelGame::buildCargoMesh() {
    m_cargoScratch.clear();
    if (m_dimension != DimensionId::Overworld) return; // no belts in the arena

    const glm::vec3 cam = camera().position;
    const glm::vec3 right = camera().right();
    const glm::vec3 up = camera().up();

    // One camera-facing icon quad centred on `p`, `size` blocks on a side.
    const auto pushIcon = [&](const glm::vec3& p, ItemId item, float size, float emissive) {
        const glm::vec3 toCam = cam - p;
        if (glm::dot(toCam, toCam) > kDropRenderDist * kDropRenderDist) return;

        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(item), uv0, uv1);
        const glm::vec3 n = glm::normalize(toCam);
        const glm::vec3 rx = right * (size * 0.5f);
        const glm::vec3 ry = up * (size * 0.5f);

        // Corners CCW seen from the camera, with v flipped so the icon is not
        // upside down (atlas v grows downward).
        const glm::vec3 c[4] = {p - rx - ry, p + rx - ry, p + rx + ry, p - rx + ry};
        const glm::vec2 t4[4] = {{uv0.x, uv1.y}, {uv1.x, uv1.y},
                                 {uv1.x, uv0.y}, {uv0.x, uv0.y}};
        const auto push = [&](int k) {
            m_cargoScratch.insert(m_cargoScratch.end(),
                                  {c[k].x, c[k].y, c[k].z, n.x, n.y, n.z,
                                   t4[k].x, t4[k].y, emissive});
        };
        push(0); push(1); push(2);
        push(0); push(2); push(3);
    };

    // 0..1 across the belt step: how far this item has slid into its cell.
    const float step = static_cast<float>(kBeltStepTicks) * kTickSeconds;
    const float t = step > 0.0f ? glm::clamp(m_beltLerp / step, 0.0f, 1.0f) : 1.0f;

    for (const auto& [pos, b] : m_belts) {
        if (b.item == ItemId::None) continue;

        // Ride ON the hub rather than inside it: the authored tube texture is
        // opaque everywhere it is painted, so an item at the centre would be
        // hidden by its own pipe. Drop kCargoLift to 0 the day the art gets
        // windows and the item moves inside with nothing else to change.
        const glm::vec3 centre = glm::vec3(pos) + glm::vec3(0.5f, 0.5f + kCargoLift, 0.5f);
        // Slide in from the cell it came from. cameFrom is zero when the item
        // did not move, which parks it dead centre -- a stalled line reads as
        // stalled.
        const glm::vec3 from = centre + glm::vec3(b.cameFrom);
        pushIcon(glm::mix(from, centre, t), b.item, kCargoSize, kCargoEmissive);
    }

    // How far along each running ritual is, keyed by every cell of its circle,
    // so the items it is about to consume can rise and brighten with it.
    std::unordered_map<glm::ivec3, float, IVec3Hash> ritualFrac;
    for (const auto& [pos, m] : m_machines) {
        if (machineTraits(m.type).kind != MachineKind::RuneCore) continue;
        if (!m.crafting || m.jammed || m.selectedRecipe < 0 || m.craftTime <= 0.0f) continue;
        const float frac = glm::clamp(m.progress / m.craftTime, 0.0f, 1.0f);
        ritualFrac[pos] = frac;
        for (int s = 0; s < AlchemyCircle::kRingSlots; ++s) {
            ritualFrac[AlchemyCircle::slotPos(pos, s)] = frac;
        }
    }

    // Circle contents. A pedestal holds one item TYPE, so its first non-empty
    // entry is the whole story; the core's input is its centre catalyst.
    for (const auto& [pos, m] : m_machines) {
        const MachineKind kind = machineTraits(m.type).kind;
        if (kind != MachineKind::Pedestal && kind != MachineKind::RuneCore) continue;
        ItemId item = ItemId::None;
        int count = 0;
        for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
            const int c = m.input.count(static_cast<ItemId>(i));
            if (c > 0) { item = static_cast<ItemId>(i); count = c; break; }
        }
        if (item == ItemId::None) continue;

        const BlockId block = m_world->getBlock(pos.x, pos.y, pos.z);
        const float top = blockBounds(block).hi.y;
        // Each cell bobs on its own phase so a laid ring shimmers rather than
        // marching in step. The clock is pause-aware: a paused game holds still.
        const float phase = static_cast<float>((pos.x * 7 + pos.z * 13) & 15) * 0.4f;
        const float bob = kCircleItemBob * std::sin(m_animClock * kCircleItemBobRate + phase);
        // A running ritual lifts what it will consume, eased so the rise is
        // slow at first and gathers toward the finish.
        const auto rf = ritualFrac.find(pos);
        const float frac = rf == ritualFrac.end() ? 0.0f : rf->second;
        const float lift = kRitualItemLift * frac * frac;
        const float emissive = glm::mix(kCircleItemEmissive, kRitualItemEmissive, frac);
        const glm::vec3 base = glm::vec3(pos) +
                               glm::vec3(0.5f, top + kCircleItemLift + lift + bob, 0.5f);

        // A fanned stack: one icon per item up to kCircleItemStack, each a
        // little up, a little right, and a hair nearer the camera than the one
        // behind it, so two ingots read as two from outside the panel and the
        // depth test never has to break a tie between coplanar cards.
        const int shown = std::min(count, kCircleItemStack);
        const glm::vec3 toCam = glm::normalize(cam - base);
        for (int k = 0; k < shown; ++k) {
            const float f = static_cast<float>(k) - static_cast<float>(shown - 1) * 0.5f;
            const glm::vec3 p = base + right * (f * kCircleItemFan) +
                                up * (f * kCircleItemFan * 0.6f) +
                                toCam * (static_cast<float>(k) * 0.01f);
            pushIcon(p, item, kCircleItemSize, emissive);
        }
    }

    // A finished ritual's result, popping up over the core: it overshoots to
    // full size, hangs while it drifts upward, then shrinks away. Cutout icons
    // cannot fade, so shrinking IS the fade.
    for (const RitualPop& pop : m_ritualPops) {
        const float age = glm::clamp(pop.age / kRitualPopSeconds, 0.0f, 1.0f);
        float scale;
        if (age < 0.12f)      scale = 1.35f * (age / 0.12f);
        else if (age < 0.25f) scale = glm::mix(1.35f, 1.0f, (age - 0.12f) / 0.13f);
        else if (age < 0.8f)  scale = 1.0f;
        else                  scale = (1.0f - age) / 0.2f;
        if (scale <= 0.01f) continue;
        pushIcon(pop.pos + glm::vec3(0.0f, 0.15f + 0.45f * age, 0.0f), pop.item,
                 kRitualPopSize * scale, 1.0f);
    }

    if (!m_cargoScratch.empty()) {
        m_cargoMesh.upload(m_cargoScratch, {3, 3, 2, 1}, GL_DYNAMIC_DRAW);
    }
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
    static const ChunkMesher::CellSet kNoCells;
    const bool home = m_dimension == DimensionId::Overworld;
    const PowerState& power = home ? m_power : kNoPower;
    const ChunkMesher::BeltMap& belts = home ? m_belts : kNoBelts;
    const ChunkMesher::CellSet& cranking = home ? m_cranking : kNoCells;
    for (const auto& [coord, chunk] : m_world->chunks()) {
        if (!chunk->dirty()) continue;
        m_meshScratch.clear();
        m_shapeScratch.clear();
        ChunkMesher::appendChunk(m_meshScratch, m_shapeScratch, *m_world, *chunk,
                                 coord, power, belts, cranking);
        // Empty chunks keep their (vertexless) entry; draw() skips them.
        m_chunkMeshes[coord].upload(m_meshScratch, {3, 3, 2, 1}, // pos, normal, uv, emissive
                                    GL_DYNAMIC_DRAW);
        // Five floats wider than the plain mesh: shaped vertices name their
        // ShapeId so the shader can find their animation frame, and carry the
        // offset from their part's pivot plus that part's slot so the part can
        // turn. See ChunkMesher.h for why the offset is baked, not the pivot.
        m_chunkShapeMeshes[coord].upload(m_shapeScratch, {3, 3, 2, 1, 1, 3, 1},
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

// Pick the animation frame every shape is showing right now, as a UV v-offset
// per ShapeId. A shaped block's texture may be a strip of frames stacked down
// shapes.png (the bake emits `vStride`, the height of one band), so playing it
// back is a shift in v -- no geometry changes, so no chunk is ever dirtied and
// an animated machine costs exactly this loop plus one uniform upload.
//
// Rates are per shape (Blockbench's frame_time, in ticks, and this game ticks
// at the same 20 Hz), which is why the vertex carries a bank index rather than
// its own stride: the auger runs at 3 ticks a frame while the cauldron runs at
// 2, and a single global frame counter could not serve both.
void VoxelGame::updateShapeAnim() {
    m_shapeAnimV.assign(kMaxShapeBanks, 0.0f);
    for (int i = 0; i < static_cast<int>(ShapeId::Count); ++i) {
        const ShapeAnim& a = blockShape(static_cast<ShapeId>(i)).anim;
        if (a.frames <= 1 || a.vStride == 0.0f) continue; // still texture
        const float secs = static_cast<float>(std::max(a.frameTime, 1)) * kTickSeconds;
        const float cycle = static_cast<float>(a.frames) * secs;
        const int frame = static_cast<int>(std::fmod(m_animClock, cycle) / secs);
        m_shapeAnimV[static_cast<std::size_t>(i)] = static_cast<float>(frame) * a.vStride;
    }
}

// Pose every moving block part for this frame, as one 3x3 per uPartRot slot.
// The same bargain as updateShapeAnim(): a turning drill is a uniform upload
// for the whole world, never a remesh, so a room full of augers still reads
// X0 PER S on F3. Slot 0 stays identity -- it is what an unpowered machine and
// every static part are meshed with.
//
// Driven by the pause-aware m_animClock, so parts freeze with the simulation
// rather than spinning on over a paused game.
//
// Rows naming the same part share a slot (kPartRowSlots) and COMPOSE: turns
// multiply, moves add, the move applied after the turn -- which is how the
// mortar's pestle grinds round the bowl and presses down in one motion.
// A cranked row reads the handle's turns instead of the clock.
void VoxelGame::updatePartAnim() {
    m_partRot.assign(kMaxShapeParts, glm::mat3(1.0f));
    m_partOff.assign(kMaxShapeParts, glm::vec3(0.0f));
    for (std::size_t i = 0; i < std::size(kPartAnims); ++i) {
        const PartAnim& a = kPartAnims[i];
        const float t = a.cranked ? m_crankTurns : m_animClock;
        const float phase = glm::two_pi<float>() * a.rate * t;
        const std::size_t slot = kPartRowSlots[i];
        glm::mat3 m(1.0f);
        switch (a.motion) {
        case PartMotion::Spin:
            m = glm::mat3(glm::rotate(glm::mat4(1.0f), phase, a.axis));
            break;
        case PartMotion::Rock:
            m = glm::mat3(glm::rotate(glm::mat4(1.0f),
                                      glm::radians(a.amount) * std::sin(phase),
                                      a.axis));
            break;
        case PartMotion::Pulse:
            // Uniform scale about the part's pivot. voxel.frag normalizes, so
            // the lighting survives it untouched.
            m = glm::mat3(1.0f + a.amount * std::sin(phase));
            break;
        case PartMotion::Bob:
            // Rests at zero and reaches `amount` at the bottom of the stroke,
            // so a part at rest sits exactly where it was modelled.
            m_partOff[slot] += a.axis * (a.amount * 0.5f * (1.0f - std::cos(phase)));
            break;
        }
        m_partRot[slot] = m * m_partRot[slot]; // slot 0 is identity
    }
}

void VoxelGame::updateCrankAnim(float dt) {
    // The machine being turned is the hand-cranked one whose panel is open.
    std::unordered_set<glm::ivec3, IVec3Hash> want;
    if (m_machineUiOpen && m_dimension == DimensionId::Overworld) {
        const auto it = m_machines.find(m_machineUiPos);
        if (it != m_machines.end() && machineTraits(it->second.type).handCranked) {
            want.insert(m_machineUiPos);
        }
    }
    // Remesh only on the EDGE: opening the panel wakes the parts, closing it
    // parks them at rest. Nothing per frame.
    if (want != m_cranking) {
        for (const glm::ivec3& p : m_cranking) m_world->markDirtyAt(p.x, p.y, p.z);
        for (const glm::ivec3& p : want) m_world->markDirtyAt(p.x, p.y, p.z);
        m_cranking = std::move(want);
    }
    // Ease after the banked turns, so four taps read as one smooth revolution
    // rather than four jumps.
    const float k = std::min(1.0f, dt * kCrankAnimEase);
    m_crankTurns += (m_crankTarget - m_crankTurns) * k;
    // Whole turns are invisible to every cranked motion (their rates are whole
    // cycles per turn), so shed them before a float this big loses its quarters.
    if (m_crankTurns > 1024.0f) {
        m_crankTurns -= 1024.0f;
        m_crankTarget -= 1024.0f;
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
    updateShapeAnim();
    updatePartAnim();
    buildRainMesh();
    buildCargoMesh();

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
        // The only pass whose vertices carry an animation bank; every other
        // mesh leaves that attribute disabled and so reads bank 0, whose
        // offset this array always holds at zero.
        m_shader.setFloatArray("uAnimV", m_shapeAnimV.data(), kMaxShapeBanks);
        m_shader.setMat3Array("uPartRot", m_partRot.data(), kMaxShapeParts);
        m_shader.setVec3Array("uPartOff", m_partOff.data(), kMaxShapeParts);
        for (auto& [coord, mesh] : m_chunkShapeMeshes) {
            mesh.draw();
        }
        m_atlas.bind(0);
    }

    // Conduit cargo: billboards in WORLD space, so a wall hides them. Drawn
    // after both chunk passes with the atlas bound -- item icons are atlas
    // tiles, and their transparent surround is what the cutout discards.
    if (!m_cargoScratch.empty() && !m_cargoMesh.empty()) {
        m_shader.setMat4("uModel", glm::mat4(1.0f));
        m_cargoMesh.draw();
    }

    // Target outline: flat wireframe cube around the aimed block.
    if (m_hasTarget) {
        // Wrap the block's actual bounds, not an assumed unit cube, so the
        // outline hugs a sub-cube shape instead of floating around its cell.
        const ShapeAabb& b = blockBounds(m_world->getBlock(
            m_targetBlock.x, m_targetBlock.y, m_targetBlock.z));
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f), glm::vec3(m_targetBlock) + (b.lo + b.hi) * 0.5f);
        model = glm::scale(model, (b.hi - b.lo) * 1.01f);
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

    // Particles LAST of the world passes: additive and depth-read-only, so
    // everything that should hide them must already be in the depth buffer.
    m_particles.render(camera());
    m_shader.use(); // the crosshair pass below assumes the voxel shader

    // Main menu shell (launch): the empty world above is just a sky backdrop —
    // draw the menu, no crosshair or HUD behind it.
    if (m_shellOpen) {
        if (m_settingsOpen) drawSettingsUi();
        else if (m_slotPickerOpen) drawSlotPicker();
        else drawMainMenu();
        if (m_debugOpen) drawDebugOverlay();
        drawCursor();
        if (m_screenshotPending) takeScreenshot();
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
    drawCursor(); // over everything, so it is in the screenshot too
    // Last, so the image is the whole frame -- HUD and overlays included.
    if (m_screenshotPending) takeScreenshot();
}

void VoxelGame::takeScreenshot() {
    m_screenshotPending = false;
    if (m_prefDir.empty()) {
        deny("SCREENSHOT FAILED: NO SAVE FOLDER");
        return;
    }
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(m_prefDir) / "screenshots";
    std::error_code ec;
    fs::create_directories(dir, ec);

    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);

    // Two presses inside one second get -2, -3, ... rather than overwriting.
    fs::path path = dir / (std::string("screenshot-") + stamp + ".png");
    for (int n = 2; fs::exists(path, ec); ++n) {
        path = dir / (std::string("screenshot-") + stamp + "-" + std::to_string(n) + ".png");
    }

    if (!window().saveScreenshot(path.string())) {
        SDL_Log("Screenshot failed (%s): %s", path.string().c_str(), SDL_GetError());
        deny("SCREENSHOT FAILED");
        return;
    }
    SDL_Log("Screenshot saved: %s", path.string().c_str());
    window().setTitle("Voxel Factory  —  SCREENSHOT SAVED");
    audio().play("click", kUiVolume);
}
