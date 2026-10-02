#include "game/ChunkMesher.h"

#include "game/World.h"
#include "game/Chunk.h"
#include "game/Block.h"
#include "game/BlockShape.h"
#include "game/Atlas.h"

#include "game/TubeShape.h"
#include "game/PowerSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

    // How brightly a conduit's outgoing arm glows. Enough to read the flow
    // direction of an EMPTY tube at a glance, well under the 0.7 an energized
    // power block carries so the two cues never compete.
    constexpr float kConduitFlowGlow = 0.30f;

    struct Face {
        glm::ivec3 offset;              // neighbor to test for occlusion
        glm::vec3 normal;
        std::array<glm::vec3, 4> corners; // CCW around the quad, in [0,1]^3
    };

    // The six cube faces. Corners walk the perimeter of each unit square so the
    // two triangles (0,1,2) and (0,2,3) tile it without gaps.
    //
    // The offsets are taken from BlockShape.h's kShapeFaceDirs rather than
    // written out again, because a face INDEX is now shared vocabulary --
    // ShapeQuad::face and kConnectParts::face both mean a slot in this order,
    // so a divergence here would connect the right arm to the wrong side with
    // nothing to catch it.
    const std::array<Face, 6> kFaces = {{
        {kShapeFaceDirs[0], {1, 0, 0},
         {glm::vec3{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
        {kShapeFaceDirs[1], {-1, 0, 0},
         {glm::vec3{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
        {kShapeFaceDirs[2], {0, 1, 0},
         {glm::vec3{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
        {kShapeFaceDirs[3], {0, -1, 0},
         {glm::vec3{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
        {kShapeFaceDirs[4], {0, 0, 1},
         {glm::vec3{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
        {kShapeFaceDirs[5], {0, 0, -1},
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
    // Shaped vertices carry extras the cube path doesn't. `bank` is how the
    // vertex shader finds this block's animation frame offset in uAnimV[]; it
    // rides the vertex because a chunk's shaped mesh mixes shapes, and they
    // animate at different rates -- or, unpowered, not at all.
    //
    // `partOff` and the part slot are the same idea for MOVING parts: the
    // offset from the part's pivot is baked (a world-space chunk vertex cannot
    // find its own cell, so the pivot itself would be useless here), and the
    // slot names the part's transform in uPartRot[].
    //
    // `animShape` carries the POWER GATE for both at once: the caller passes
    // ShapeId::FullCube for an unpowered block, whose bank offset is zero and
    // whose every part slot is 0, so a dead machine parks on frame 0 AND stands
    // still. One value, one gate, no way for the two to disagree.
    // `armMask` and `glowFace` serve the CONNECTED shapes (BlockShape.h's
    // kConnectParts): a quad belonging to a part that hangs off face f is
    // emitted only when bit f is set, which is how one baked hub draws a
    // straight run, a corner and a four-way junction. Anything without
    // connection parts passes kAllFaces and never notices.
    //
    // `glowFace` is the conduit's flow direction. It replaced the top-face
    // arrow the cube used to wear, and it costs nothing: emissive is already a
    // per-vertex float, so lighting one arm is a value change, not a pass.
    template <typename NeighborFn>
    void appendShaped(std::vector<float>& out, const BlockShape& shape,
                      const glm::vec3& base, float emissive, ShapeId animShape,
                      NeighborFn neighbor,
                      std::uint8_t armMask = kAllFaces, int glowFace = -1) {
        const float bank = static_cast<float>(animShape);
        for (const ShapeQuad& q : shape.quads) {
            if (q.cull && isFullCube(neighbor(q.face))) continue;

            // Which face this quad's part hangs off, if any. Read from the
            // REAL shape, never animShape -- that is FullCube when the block is
            // unpowered, and an unpowered conduit must still show its arms.
            const int face = partFace(shape.id, q.part);
            if (face >= 0 && !(armMask & (1u << face))) continue;
            const float quadEmissive = (face >= 0 && face == glowFace)
                ? std::max(emissive, kConduitFlowGlow)
                : emissive;

            const float slot = static_cast<float>(partSlot(animShape, q.part));

            // Corners arrive baked and correctly wound; nothing to reconstruct.
            const auto push = [&](int k) {
                pushVertex(out, base + q.pos[k], q.normal, q.uv[k], quadEmissive);
                out.push_back(bank);
                out.insert(out.end(), {q.partOff[k].x, q.partOff[k].y,
                                       q.partOff[k].z});
                out.push_back(slot);
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
                     const PowerState& power, const BeltMap& belts,
                     const CellSet& cranking) {
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
                        // A dead machine sits still -- its texture parked on
                        // frame 0 and its parts stopped. Gating on power costs
                        // nothing extra because power is ALREADY a mesh input:
                        // solvePowerAndMarkDirty dirties exactly the chunks
                        // whose glow flipped, so a machine losing power
                        // re-meshes for the glow regardless. FullCube is the
                        // inert shape: bank offset zero, every part slot 0.
                        // A hand-cranked machine is never energized, so the
                        // one whose handle someone is turning animates instead
                        // -- a set that changes only when a crank panel opens
                        // or closes, so it too costs a remesh per edge, never
                        // per frame.
                        const ShapeId animShape = energized || cranking.count(w) > 0
                            ? blockInfo(id).shape
                            : ShapeId::FullCube;

                        // A connected shape (Conduit, Wire) grows an arm per
                        // face, so its geometry depends on its neighbours and
                        // not on its BlockId alone. Everything else takes
                        // kAllFaces and pays nothing: shapeConnects is a
                        // constexpr scan of a 12-row table, and the whole
                        // branch is skipped for the four machine models.
                        std::uint8_t armMask = kAllFaces;
                        int glowFace = -1;
                        if (shapeConnects(shape.id)) {
                            TubeShape::Neighbours nb;
                            for (int fi = 0; fi < 6; ++fi) {
                                nb[static_cast<std::size_t>(fi)] =
                                    neighborAt(lx, ly, lz, fi);
                            }
                            if (id == BlockId::Wire) {
                                armMask = TubeShape::wireArms(nb);
                            } else {
                                const auto bit = belts.find(w);
                                const Belt self = bit != belts.end() ? bit->second
                                                                     : Belt{};
                                armMask = TubeShape::conduitArms(w, self, belts, nb);
                                glowFace = TubeShape::faceIndex(self.facing);
                            }
                        }

                        appendShaped(shapedOut, shape, base, emissive, animShape,
                                     [&](int fi) { return neighborAt(lx, ly, lz, fi); },
                                     armMask, glowFace);
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
