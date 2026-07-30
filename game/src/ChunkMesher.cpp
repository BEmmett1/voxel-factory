#include "game/ChunkMesher.h"

#include "game/World.h"
#include "game/Chunk.h"
#include "game/Block.h"
#include "game/BlockShape.h"
#include "game/Atlas.h"

#include <array>

namespace {

    struct Face {
        glm::ivec3 offset;              // neighbor to test for occlusion
        glm::vec3 normal;
        std::array<glm::vec3, 4> corners; // CCW around the quad, in [0,1]^3
    };

    // The six cube faces. Corners walk the perimeter of each unit square so the
    // two triangles (0,1,2) and (0,2,3) tile it without gaps.
    const std::array<Face, 6> kFaces = {{
        {{1, 0, 0}, {1, 0, 0},
         {glm::vec3{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
        {{-1, 0, 0}, {-1, 0, 0},
         {glm::vec3{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
        {{0, 1, 0}, {0, 1, 0},
         {glm::vec3{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
        {{0, -1, 0}, {0, -1, 0},
         {glm::vec3{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
        {{0, 0, 1}, {0, 0, 1},
         {glm::vec3{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
        {{0, 0, -1}, {0, 0, -1},
         {glm::vec3{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}},
    }};

    void pushVertex(std::vector<float>& out, const glm::vec3& pos, const glm::vec3& normal,
                    const glm::vec2& uv, float emissive) {
        out.insert(out.end(), {pos.x, pos.y, pos.z, normal.x, normal.y, normal.z,
                               uv.x, uv.y, emissive});
    }

    // Emit one shaped block's baked quads at `base`. `neighbor(fi)` supplies
    // the block across face fi, consulted only for quads flush with a cell
    // wall -- the only ones a full-cube neighbor can legally hide.
    //
    // Shaped vertices carry one extra float the cube path doesn't: `bank`,
    // which is how the vertex shader finds this block's animation frame offset
    // in uAnimV[]. It rides the vertex because a chunk's shaped mesh mixes
    // shapes, and they animate at different rates -- or, unpowered, not at all.
    template <typename NeighborFn>
    void appendShaped(std::vector<float>& out, const BlockShape& shape,
                      const glm::vec3& base, float emissive, float bank,
                      NeighborFn neighbor) {
        for (const ShapeQuad& q : shape.quads) {
            if (q.cull && isFullCube(neighbor(q.face))) continue;

            // Corners arrive baked and correctly wound; nothing to reconstruct.
            const auto push = [&](int k) {
                pushVertex(out, base + q.pos[k], q.normal, q.uv[k], emissive);
                out.push_back(bank);
            };
            push(0); push(1); push(2);
            push(0); push(2); push(3);
        }
    }

} // namespace

namespace ChunkMesher {

    void appendChunk(std::vector<float>& out, std::vector<float>& shapedOut,
                     const World& world,
                     const Chunk& chunk, const glm::ivec3& chunkCoord,
                     const PowerState& power, const BeltMap& belts) {
        constexpr float kEnergizedEmissive = 0.7f;
        const glm::ivec3 originBlock = chunkCoord * CHUNK_SIZE;

        // Prefetch the six neighbor chunks once so every occlusion test is a
        // plain array read instead of a hash-map lookup.
        const Chunk* neighbors[6];
        for (int fi = 0; fi < 6; ++fi) {
            const auto it = world.chunks().find(chunkCoord + kFaces[fi].offset);
            neighbors[fi] = it != world.chunks().end() ? it->second.get() : nullptr;
        }

        // The block across face `fi` from local cell (lx,ly,lz). Shared by the
        // cube and shaped paths.
        const auto neighborAt = [&](int lx, int ly, int lz, int fi) -> BlockId {
            const glm::ivec3 nl = glm::ivec3(lx, ly, lz) + kFaces[fi].offset;
            if (chunk.inBounds(nl.x, nl.y, nl.z)) {
                return chunk.get(nl.x, nl.y, nl.z);
            }
            if (const Chunk* nc = neighbors[fi]) {
                // Exactly one component stepped out; wrap it into the
                // neighbor's local space.
                return nc->get((nl.x + CHUNK_SIZE) % CHUNK_SIZE,
                               (nl.y + CHUNK_SIZE) % CHUNK_SIZE,
                               (nl.z + CHUNK_SIZE) % CHUNK_SIZE);
            }
            return BlockId::Air; // ungenerated space
        };

        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    const glm::ivec3 w = originBlock + glm::ivec3(lx, ly, lz);
                    const BlockId id = chunk.get(lx, ly, lz);
                    if (!isSolid(id)) continue;

                    const bool energized = power.energized(w.x, w.y, w.z);

                    // Powered network glow or the block's own glow (sources).
                    const float emissive = energized ? kEnergizedEmissive
                                                     : blockInfo(id).emissive;

                    const glm::vec3 base(w);

                    // Shaped blocks emit their baked quads into the second
                    // buffer (different sheet) and skip the unit-cube path.
                    const BlockShape& shape = blockShape(id);
                    if (!shape.quads.empty()) {
                        // A dead machine sits still. Gating the animation on
                        // power costs nothing extra because power is ALREADY a
                        // mesh input: solvePowerAndMarkDirty dirties exactly
                        // the chunks whose glow flipped, so a machine losing
                        // power re-meshes for the glow regardless. Bank 0 is
                        // ShapeId::FullCube, whose offset is permanently zero,
                        // so an unpowered machine parks on frame 0.
                        const float bank = energized
                            ? static_cast<float>(blockInfo(id).shape)
                            : 0.0f;
                        appendShaped(shapedOut, shape, base, emissive, bank,
                                     [&](int fi) { return neighborAt(lx, ly, lz, fi); });
                        continue;
                    }

                    for (int fi = 0; fi < 6; ++fi) {
                        const Face& f = kFaces[fi];
                        const BlockId nb = neighborAt(lx, ly, lz, fi);
                        // Only a neighbor that FILLS its cell can hide this
                        // face; a sub-cube shape leaves gaps to see through.
                        if (isFullCube(nb)) continue;

                        // Blocks can wear a different tile per face
                        // (grass top vs. side, log rings vs. bark, ...).
                        glm::vec2 uvMin, uvMax;
                        Atlas::uvForBlockFace(id, f.normal, uvMin, uvMax);

                        glm::vec2 faceUv[4];
                        if (f.normal.y == 0.0f) {
                            // Side faces stand upright: u runs along the face
                            // horizontally, v runs down from the block's top,
                            // so a tile's top row (grass lip, machine rim) is
                            // at the top on all four sides. Opposing faces are
                            // mirror images of each other, which the art
                            // doesn't mind.
                            for (int k = 0; k < 4; ++k) {
                                const float h = (f.normal.x != 0.0f)
                                    ? f.corners[k].z
                                    : f.corners[k].x;
                                faceUv[k] = {glm::mix(uvMin.x, uvMax.x, h),
                                             glm::mix(uvMin.y, uvMax.y,
                                                      1.0f - f.corners[k].y)};
                            }
                        } else {
                            // Top/bottom: plain rect in corner order.
                            faceUv[0] = {uvMin.x, uvMin.y};
                            faceUv[1] = {uvMax.x, uvMin.y};
                            faceUv[2] = {uvMax.x, uvMax.y};
                            faceUv[3] = {uvMin.x, uvMax.y};
                        }

                        // Belt direction arrows. Horizontal belts show the
                        // arrow on the top face, rotated to the facing (each
                        // corner's UV is its offset from the block center in
                        // right/forward axes). Vertical belts show it on all
                        // four side faces, pointing up or down the block.
                        if (id == BlockId::Belt) {
                            const auto bit = belts.find(w);
                            if (bit != belts.end()) {
                                const glm::ivec3& bf = bit->second.facing;
                                glm::vec2 aMin, aMax;
                                Atlas::uvForTile(Atlas::BeltArrowTile, aMin, aMax);
                                if (bf.y == 0 && f.normal.y > 0.5f) {
                                    const glm::vec2 fwd(static_cast<float>(bf.x),
                                                        static_cast<float>(bf.z));
                                    const glm::vec2 right(-fwd.y, fwd.x);
                                    for (int k = 0; k < 4; ++k) {
                                        const glm::vec2 p(f.corners[k].x - 0.5f,
                                                          f.corners[k].z - 0.5f);
                                        const float u = 0.5f + glm::dot(p, right);
                                        const float v = 0.5f + glm::dot(p, fwd);
                                        faceUv[k] = {glm::mix(aMin.x, aMax.x, u),
                                                     glm::mix(aMin.y, aMax.y, v)};
                                    }
                                } else if (bf.y != 0 && f.normal.y == 0.0f) {
                                    // Side face: u along the face's horizontal
                                    // axis, v along y scaled by the facing sign.
                                    const bool xVaries = (f.normal.x == 0.0f);
                                    for (int k = 0; k < 4; ++k) {
                                        const float h = xVaries ? f.corners[k].x
                                                                : f.corners[k].z;
                                        const float u = h;
                                        const float v = 0.5f +
                                            (f.corners[k].y - 0.5f) * static_cast<float>(bf.y);
                                        faceUv[k] = {glm::mix(aMin.x, aMax.x, u),
                                                     glm::mix(aMin.y, aMax.y, v)};
                                    }
                                }
                            }
                        }

                        const glm::vec3 c0 = base + f.corners[0];
                        const glm::vec3 c1 = base + f.corners[1];
                        const glm::vec3 c2 = base + f.corners[2];
                        const glm::vec3 c3 = base + f.corners[3];

                        pushVertex(out, c0, f.normal, faceUv[0], emissive);
                        pushVertex(out, c1, f.normal, faceUv[1], emissive);
                        pushVertex(out, c2, f.normal, faceUv[2], emissive);

                        pushVertex(out, c0, f.normal, faceUv[0], emissive);
                        pushVertex(out, c2, f.normal, faceUv[2], emissive);
                        pushVertex(out, c3, f.normal, faceUv[3], emissive);
                    }
                }
            }
        }
    }

} // namespace ChunkMesher
