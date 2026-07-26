#include "game/DropSystem.h"

#include "game/Collision.h"

#include "game/World.h"
#include "game/Block.h"

#include <algorithm>
#include <cmath>

namespace {

    // Drop-physics knobs (self-contained; the gameplay-facing pickup radius and
    // render distance live with the rest of the tuning in VoxelGameInternal.h).
    constexpr float kDropGravity     = 24.0f;   // blocks/s^2 while falling
    constexpr float kDropHalf        = 0.15f;   // item half-extent (rest offset)
    constexpr float kDropMergeRadius = 1.25f;   // same-id drops within this fuse
    constexpr float kDropFreezeY     = -40.0f;  // below the world: freeze (stop cost)
    constexpr std::size_t kMaxDrops  = 512;     // hard cap; oldest settled evicted

    float dist2(const glm::vec3& a, const glm::vec3& b) {
        const glm::vec3 d = a - b;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }

    // The surface a drop would rest on in this cell, or -infinity if the cell
    // holds nothing. Shape-aware: an item landing on a slab sits on the slab,
    // not on the cell boundary above it.
    float surfaceAt(const World& world, float x, int y, float z) {
        return Collision::surfaceTopAt(world, static_cast<int>(std::floor(x)), y,
                                       static_cast<int>(std::floor(z)));
    }

    // Coalesce settled same-item, same-dimension drops that came to rest near
    // each other (merge-on-spawn only catches items close at spawn time; two
    // adjacent-block yields settle ~1 apart and would otherwise stay split).
    void mergeSettled(std::vector<DroppedItem>& drops) {
        const float mergeR2 = kDropMergeRadius * kDropMergeRadius;
        for (std::size_t i = 0; i + 1 < drops.size(); ++i) {
            if (!drops[i].settled) continue;
            for (std::size_t j = i + 1; j < drops.size();) {
                if (drops[j].settled && drops[j].id == drops[i].id &&
                    drops[j].dim == drops[i].dim &&
                    dist2(drops[i].pos, drops[j].pos) < mergeR2) {
                    drops[i].count += drops[j].count;
                    drops[i].pickupDelay = std::min(drops[i].pickupDelay, drops[j].pickupDelay);
                    drops[j] = drops.back();
                    drops.pop_back();
                    continue; // re-test the swapped-in element at j
                }
                ++j;
            }
        }
    }

} // namespace

namespace DropSystem {

void spawn(std::vector<DroppedItem>& drops, const glm::vec3& pos, ItemId id,
           int count, DimensionId dim, float pickupDelay) {
    if (id == ItemId::None || count <= 0) return;

    // Merge into a nearby same-id/-dimension drop instead of piling up entities.
    const float mergeR2 = kDropMergeRadius * kDropMergeRadius;
    for (DroppedItem& d : drops) {
        if (d.dim == dim && d.id == id && dist2(d.pos, pos) < mergeR2) {
            d.count += count;
            d.pickupDelay = std::min(d.pickupDelay, pickupDelay);
            return;
        }
    }

    // Cap the population: evict the first settled drop (a resting one is the
    // least missed), else the front. Only runs when genuinely saturated.
    if (drops.size() >= kMaxDrops) {
        auto it = std::find_if(drops.begin(), drops.end(),
                               [](const DroppedItem& d) { return d.settled; });
        drops.erase(it != drops.end() ? it : drops.begin());
    }

    DroppedItem d;
    d.pos = pos;
    d.vel = {0.0f, 2.5f, 0.0f}; // a small pop so it's visible leaving the block
    d.id = id;
    d.count = count;
    d.pickupDelay = pickupDelay;
    d.dim = dim;
    drops.push_back(d);
}

void tick(std::vector<DroppedItem>& drops, const World& world, float dt,
          DimensionId dim) {
    for (DroppedItem& d : drops) {
        if (d.pickupDelay > 0.0f) d.pickupDelay = std::max(0.0f, d.pickupDelay - dt);
        if (d.dim != dim) continue; // off-dimension: no physics

        if (d.settled) {
            // A resting drop costs only this support re-check: if the block
            // beneath it was mined away, wake it up so it falls this tick.
            const int supY = static_cast<int>(std::floor(d.pos.y - kDropHalf - 0.05f));
            if (std::isfinite(surfaceAt(world, d.pos.x, supY, d.pos.z))) continue;
            d.settled = false;
        }

        d.vel.y -= kDropGravity * dt;
        const glm::vec3 np = d.pos + d.vel * dt;

        const int belowY = static_cast<int>(std::floor(np.y - kDropHalf));
        const float surface = surfaceAt(world, np.x, belowY, np.z);
        if (std::isfinite(surface)) {
            // Rest on top of that block's surface.
            d.pos = {np.x, surface + kDropHalf, np.z};
            d.vel = glm::vec3(0.0f);
            d.settled = true;
        } else if (np.y < kDropFreezeY) {
            d.vel = glm::vec3(0.0f); // fell past the world: freeze so it stops costing
            d.settled = true;
        } else {
            d.pos = np;
        }
    }

    mergeSettled(drops);
}

} // namespace DropSystem
