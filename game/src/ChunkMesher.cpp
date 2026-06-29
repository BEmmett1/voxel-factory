#include "game/ChunkMesher.h"

#include "game/Chunk.h"
#include "game/Block.h"

#include <array>

namespace {

    struct Face {
        int dx, dy, dz;                 // neighbor offset to test for occlusion
        glm::vec3 normal;
        std::array<glm::vec3, 4> corners; // CCW around the quad, in [0,1]^3
    };

    // The six cube faces. Corners walk the perimeter of each unit square so the
    // two triangles (0,1,2) and (0,2,3) tile it without gaps.
    const std::array<Face, 6> kFaces = {{
        // +X
        {1, 0, 0, {1, 0, 0},
         {glm::vec3{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
        // -X
        {-1, 0, 0, {-1, 0, 0},
         {glm::vec3{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
        // +Y (top)
        {0, 1, 0, {0, 1, 0},
         {glm::vec3{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
        // -Y (bottom)
        {0, -1, 0, {0, -1, 0},
         {glm::vec3{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
        // +Z
        {0, 0, 1, {0, 0, 1},
         {glm::vec3{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
        // -Z
        {0, 0, -1, {0, 0, -1},
         {glm::vec3{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}},
    }};

    void pushVertex(std::vector<float>& out, const glm::vec3& pos,
                    const glm::vec3& normal, const glm::vec3& color, float emissive) {
        out.push_back(pos.x);
        out.push_back(pos.y);
        out.push_back(pos.z);
        out.push_back(normal.x);
        out.push_back(normal.y);
        out.push_back(normal.z);
        out.push_back(color.x);
        out.push_back(color.y);
        out.push_back(color.z);
        out.push_back(emissive);
    }

} // namespace

namespace ChunkMesher {

    std::vector<float> build(const Chunk& chunk, const glm::vec3& origin,
                             const PowerState& power) {
        std::vector<float> out;

        // How strongly an energized power block self-illuminates.
        constexpr float kEnergizedEmissive = 0.7f;

        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    const BlockId id = chunk.get(x, y, z);
                    if (!isSolid(id)) continue;

                    const glm::vec3 color = blockInfo(id).color;
                    const float emissive = power.energized(x, y, z) ? kEnergizedEmissive : 0.0f;
                    const glm::vec3 base = origin + glm::vec3(x, y, z);

                    for (const Face& f : kFaces) {
                        // Skip faces hidden behind a solid neighbor. Neighbors
                        // outside the chunk read as Air, so boundary faces show.
                        if (isSolid(chunk.get(x + f.dx, y + f.dy, z + f.dz))) {
                            continue;
                        }

                        const glm::vec3 c0 = base + f.corners[0];
                        const glm::vec3 c1 = base + f.corners[1];
                        const glm::vec3 c2 = base + f.corners[2];
                        const glm::vec3 c3 = base + f.corners[3];

                        pushVertex(out, c0, f.normal, color, emissive);
                        pushVertex(out, c1, f.normal, color, emissive);
                        pushVertex(out, c2, f.normal, color, emissive);

                        pushVertex(out, c0, f.normal, color, emissive);
                        pushVertex(out, c2, f.normal, color, emissive);
                        pushVertex(out, c3, f.normal, color, emissive);
                    }
                }
            }
        }

        return out;
    }

} // namespace ChunkMesher
