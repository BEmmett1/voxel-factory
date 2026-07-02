#include "game/ChunkMesher.h"

#include "game/World.h"
#include "game/Chunk.h"
#include "game/Block.h"
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

} // namespace

namespace ChunkMesher {

    void appendChunk(std::vector<float>& out, const World& world,
                     const glm::ivec3& chunkCoord, const PowerState& power,
                     const BeltMap& belts) {
        constexpr float kEnergizedEmissive = 0.7f;
        const glm::ivec3 originBlock = chunkCoord * CHUNK_SIZE;

        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                    const glm::ivec3 w = originBlock + glm::ivec3(lx, ly, lz);
                    const BlockId id = world.getBlock(w.x, w.y, w.z);
                    if (!isSolid(id)) continue;

                    // Powered network glow or the block's own glow (sources).
                    const float emissive = power.energized(w.x, w.y, w.z)
                        ? kEnergizedEmissive
                        : blockInfo(id).emissive;

                    glm::vec2 uvMin, uvMax;
                    Atlas::uvForBlock(id, uvMin, uvMax);
                    const glm::vec2 uv[4] = {
                        {uvMin.x, uvMin.y}, {uvMax.x, uvMin.y}, {uvMax.x, uvMax.y}, {uvMin.x, uvMax.y}};

                    const glm::vec3 base(w);

                    for (const Face& f : kFaces) {
                        const glm::ivec3 n = w + f.offset;
                        if (isSolid(world.getBlock(n.x, n.y, n.z))) continue;

                        // Belt direction arrows. Horizontal belts show the
                        // arrow on the top face, rotated to the facing (each
                        // corner's UV is its offset from the block center in
                        // right/forward axes). Vertical belts show it on all
                        // four side faces, pointing up or down the block.
                        glm::vec2 faceUv[4] = {uv[0], uv[1], uv[2], uv[3]};
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
