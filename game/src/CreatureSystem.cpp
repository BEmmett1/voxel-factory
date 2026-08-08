// The entity layer: ambient wanderers and the boss. Loads Blockbench models
// per species, steps everyone in the ACTIVE dimension at 20 Hz (gravity + the
// shared box collision; bosses aggro, chase, and strike), and draws skinned
// through the entity shader. Deliberately NOT persisted -- creatures are
// transient; save records arrive with the full entity layer.

#include "game/CreatureSystem.h"

#include "VoxelGameInternal.h"
#include "game/Collision.h"
#include "game/Raycast.h"
#include "game/World.h"
#include "engine/Audio.h"
#include "engine/Camera.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>

using namespace vg;

namespace {

    // One row per SpeciesId, in enum order (the kBlocks/kItems discipline).
    constexpr CreatureSpecies kSpecies[] = {
        {.id = SpeciesId::TestCreature, .model = kCreatureModel,
         .kind = CreatureKind::Wanderer, .name = "",
         .scale = kCreatureScale, .halfW = kCreatureHalfW, .height = kCreatureHeight,
         .hp = kCreatureHealth, .walkSpeed = kCreatureWalkSpeed},
        {.id = SpeciesId::VoidWarden, .model = kWardenModel,
         .kind = CreatureKind::Boss, .name = "VOID WARDEN",
         .scale = kBossScale, .halfW = kBossHalfW, .height = kBossHeight,
         .hp = kBossHealth, .walkSpeed = kBossWalkSpeed,
         .aggroRadius = kBossAggroRadius, .strikeRange = kBossStrikeRange,
         .damage = kBossDamage, .strikeCooldown = kBossStrikeCooldown,
         .swingImpact = kBossSwingImpact,
         .drop = ItemId::VoidCatalyst,
         .lungeCooldown = kBossLungeCooldown, .lungeWindup = kBossLungeWindup,
         .lungeSpeed = kBossLungeSpeed, .lungeDuration = kBossLungeDuration,
         .lungeMinRange = kBossLungeMinRange, .lungeMaxRange = kBossLungeMaxRange,
         .lungeDamageMult = kBossLungeDamageMult,
         .enrageAt = kBossEnrageAt, .enrageSpeedMult = kBossEnrageSpeedMult,
         .enrageRateMult = kBossEnrageRateMult},
        {.id = SpeciesId::Tempest, .model = kTempestModel,
         .kind = CreatureKind::Boss, .name = "THE TEMPEST",
         .scale = kTempestScale, .halfW = kTempestHalfW, .height = kTempestHeight,
         .hp = kTempestHealth, .walkSpeed = kTempestWalkSpeed,
         .aggroRadius = kTempestAggroRadius, .strikeRange = kTempestStrikeRange,
         .damage = kTempestDamage, .strikeCooldown = kTempestStrikeCooldown,
         .drop = ItemId::StormCore},
    };

    static_assert(std::size(kSpecies) == static_cast<std::size_t>(SpeciesId::Count),
                  "kSpecies needs exactly one row per SpeciesId");

    constexpr bool speciesInEnumOrder() {
        for (std::size_t i = 0; i < std::size(kSpecies); ++i) {
            if (kSpecies[i].id != static_cast<SpeciesId>(i)) return false;
        }
        return true;
    }
    static_assert(speciesInEnumOrder(), "kSpecies rows must be in SpeciesId order");

    const CreatureSpecies& speciesOf(SpeciesId id) {
        return kSpecies[static_cast<std::size_t>(id)];
    }

    // Hash roll in [0, 1) from a creature's decision counter.
    float roll01(std::uint32_t& counter, std::uint32_t salt) {
        return static_cast<float>(hash2(static_cast<int>(counter++), 7, salt) % 1024u) /
               1024.0f;
    }

    // Shortest-arc wrap of an angle difference into [-180, 180).
    float wrapDeg(float a) {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f) a += 360.0f;
        return a - 180.0f;
    }

    // (The ray/AABB slab test moved to Collision.h — the world raycast needs
    // the same maths now that a cell holds boxes rather than one cube.)

} // namespace

void CreatureSystem::loadAssets(const std::string& dir) {
    // Content, not core: a missing/broken model means that species never
    // spawns, with a log line -- never a crash (the atlas/sounds philosophy).
    for (std::size_t i = 0; i < std::size(kSpecies); ++i) {
        SpeciesAssets& a = m_assets[i];
        if (!engine::loadBbModel(dir + kSpecies[i].model, a.model, kMaxEntityBones)) {
            SDL_Log("Entities: model '%s' unavailable -- species disabled "
                    "(regenerate with the tools/ scripts)", kSpecies[i].model);
            continue;
        }
        if (!a.model.texture.rgba.empty()) {
            a.texture.createFromPixels(a.model.texture.width, a.model.texture.height,
                                       a.model.texture.rgba.data());
        } else {
            // Loud-but-alive fallback: a magenta/black checker.
            const unsigned char checker[16] = {255, 0, 255, 255, 25, 25, 25, 255,
                                               25, 25, 25, 255, 255, 0, 255, 255};
            a.texture.createFromPixels(2, 2, checker);
        }
        a.mesh.upload(a.model.vertexData, {3, 3, 2, 1});
        a.ready = true;
    }

    if (!m_shader.loadFromFiles(dir + "shaders/entity.vert",
                                dir + "shaders/entity.frag")) {
        SDL_Log("Entities: entity shader failed to load -- running without "
                "creatures");
        return;
    }
    m_shader.use();
    m_shader.setInt("uTex", 0);
    m_shaderReady = true;
}

void CreatureSystem::spawn(SpeciesId species, DimensionId dim, const World& world,
                           const glm::vec3& feetHint) {
    const std::size_t si = static_cast<std::size_t>(species);
    if (!m_shaderReady || !m_assets[si].ready) return;

    // At the hint's column, snapped down onto solid ground (scan starts a few
    // blocks above the hint so a slightly buried hint still finds the top).
    glm::vec3 feet = feetHint;
    const int x = static_cast<int>(std::floor(feet.x));
    const int z = static_cast<int>(std::floor(feet.z));
    for (int y = static_cast<int>(std::floor(feet.y)) + 8; y >= 0; --y) {
        const float top = Collision::surfaceTopAt(world, x, y, z);
        if (std::isfinite(top)) {
            feet.y = top; // stand on the surface, not the cell above it
            Creature c;
            c.species = species;
            c.dim = dim;
            c.pos = c.prevPos = c.home = c.target = feet;
            c.anim = m_assets[si].model.findAnimation("idle");
            c.hp = speciesOf(species).hp;
            m_creatures.push_back(c);
            return;
        }
    }
    SDL_Log("Entities: no ground at a spawn hint -- skipped");
}

void CreatureSystem::playOnce(Creature& c, const char* clip) {
    const engine::BbModel& model = m_assets[static_cast<std::size_t>(c.species)].model;
    const int index = model.findAnimation(clip);
    if (index < 0) return; // no such clip: this species just keeps walking
    c.anim = index;
    c.animTime = 0.0f;     // from the top, so back-to-back strikes each swing
    c.attackLeft = model.animations[static_cast<std::size_t>(index)].length;
}

void CreatureSystem::clearDimension(DimensionId dim) {
    m_creatures.erase(
        std::remove_if(m_creatures.begin(), m_creatures.end(),
                       [dim](const Creature& c) { return c.dim == dim; }),
        m_creatures.end());
}

int CreatureSystem::rayPickCreature(const World& world, const glm::vec3& origin,
                                    const glm::vec3& dir, DimensionId active,
                                    float reach) const {
    // A wall between us and the creature blocks the shot/swing. The raycast
    // hands back its own hit distance now, so this no longer re-derives one
    // against an assumed unit cube (which was wrong for any shaped block).
    const RaycastHit aim = raycastVoxel(world, origin, dir, reach);
    const float tBlock = aim.hit ? aim.t : reach;

    int   best = -1;
    float bestT = tBlock;
    for (int i = 0; i < static_cast<int>(m_creatures.size()); ++i) {
        const Creature& c = m_creatures[i];
        if (c.dim != active) continue;
        const CreatureSpecies& sp = speciesOf(c.species);
        const glm::vec3 lo = c.pos - glm::vec3(sp.halfW, 0.0f, sp.halfW);
        const glm::vec3 hi = c.pos + glm::vec3(sp.halfW, sp.height, sp.halfW);
        float t = 0.0f;
        if (Collision::rayAabb(origin, dir, lo, hi, t) && t <= reach && t < bestT) {
            bestT = t;
            best = i;
        }
    }
    return best;
}

CreatureSystem::MeleeResult CreatureSystem::tryMeleeAttack(
    const World& world, engine::Audio& audio,
    const glm::vec3& origin, const glm::vec3& dir, DimensionId active,
    float damage) {
    MeleeResult r;
    const int best = rayPickCreature(world, origin, dir, active, kReach);
    if (best < 0) return r;
    r.hit = true;

    Creature& c = m_creatures[best];
    const CreatureSpecies& sp = speciesOf(c.species);
    c.hp -= damage;
    c.hurtFlash = 1.0f;
    const glm::vec3 center = c.pos + glm::vec3(0.0f, sp.height * 0.5f, 0.0f);
    if (c.hp <= 0.0f) {
        // Down: a lower-pitched thud marks the kill. Bosses hand the caller
        // their unique drop; ambient kinds still have none.
        audio.playAt("hit", center, kHurtVolume, 0.7f);
        if (sp.kind == CreatureKind::Boss) {
            r.bossDied = true;
            r.drop = sp.drop;
            r.bossSpecies = c.species;
            r.bossName = sp.name;
        }
        m_creatures.erase(m_creatures.begin() + best);
        return r;
    }
    audio.playAt("hit", center, kHurtVolume);

    // Shove it away from the player and send it moving.
    glm::vec3 away = c.pos - origin;
    away.y = 0.0f;
    away = (glm::dot(away, away) > 1e-6f) ? glm::normalize(away)
                                          : glm::vec3(0.0f, 0.0f, 1.0f);
    c.knock = away * kKnockback;
    c.vel.y += kKnockUp;
    if (sp.kind == CreatureKind::Wanderer) {
        c.target = c.pos + away * kCreatureWanderRadius; // flee
        c.walking = true;
    } // a Boss shrugs off the shove and keeps coming
    return r;
}

CreatureSystem::MeleeResult CreatureSystem::tryRangedAttack(
    const World& world, engine::Audio& audio,
    const glm::vec3& origin, const glm::vec3& dir, DimensionId active,
    float damage, float reach) {
    MeleeResult r;
    const int best = rayPickCreature(world, origin, dir, active, reach);
    if (best < 0) return r;
    r.hit = true;

    Creature& c = m_creatures[best];
    const CreatureSpecies& sp = speciesOf(c.species);
    c.hp -= damage;
    c.hurtFlash = 1.0f;
    const glm::vec3 center = c.pos + glm::vec3(0.0f, sp.height * 0.5f, 0.0f);
    if (c.hp <= 0.0f) {
        audio.playAt("hit", center, kHurtVolume, 0.7f);
        if (sp.kind == CreatureKind::Boss) {
            r.bossDied = true;
            r.drop = sp.drop;
            r.bossSpecies = c.species;
            r.bossName = sp.name;
        }
        m_creatures.erase(m_creatures.begin() + best);
        return r;
    }
    audio.playAt("hit", center, kHurtVolume);
    // A bolt stings but doesn't knock back; a struck wanderer still flees.
    if (sp.kind == CreatureKind::Wanderer) {
        glm::vec3 away = c.pos - origin;
        away.y = 0.0f;
        away = (glm::dot(away, away) > 1e-6f) ? glm::normalize(away)
                                              : glm::vec3(0.0f, 0.0f, 1.0f);
        c.target = c.pos + away * kCreatureWanderRadius;
        c.walking = true;
    }
    return r;
}

CreatureSystem::Events CreatureSystem::update(const World& world, DimensionId active,
                                              const glm::vec3& playerFeet) {
    const float dt = kTickSeconds;
    Events ev;

    for (Creature& c : m_creatures) {
        if (c.dim != active) continue; // frozen while its dimension sleeps
        const CreatureSpecies& sp = speciesOf(c.species);
        c.prevPos = c.pos;

        // --- Intent: wander or hunt. ---
        glm::vec3 wishVel(0.0f);
        // A dash overrides the walk entirely; faceDir carries the turn target
        // out of the branches, since a winding-up boss turns without walking.
        glm::vec3 lungeVel(0.0f), faceDir(0.0f);
        float chaseSpeedMult = 1.0f;
        if (sp.kind == CreatureKind::Boss) {
            c.strikeTimer = std::max(0.0f, c.strikeTimer - dt);
            c.lungeTimer = std::max(0.0f, c.lungeTimer - dt);
            // Runs down in the SIM, not the render loop: when the axe lands is
            // gameplay, so it must not drift with the frame rate.
            c.swingLeft = std::max(0.0f, c.swingLeft - dt);
            glm::vec3 toPlayer = playerFeet - c.pos;
            const float distXZ = glm::length(glm::vec2(toPlayer.x, toPlayer.z));
            const float dist = glm::length(toPlayer);

            // Enrage scales the whole kit at once: below the threshold it
            // moves faster and every cooldown shrinks.
            const bool enraged = sp.enrageAt > 0.0f && sp.hp > 0.0f &&
                                 (c.hp / sp.hp) <= sp.enrageAt;
            const float speedMult = enraged ? sp.enrageSpeedMult : 1.0f;
            const float rateMult  = enraged ? sp.enrageRateMult : 1.0f;

            // Flat XZ direction to the player, reused by the strike shove and
            // the lunge commit.
            glm::vec3 flat = toPlayer;
            flat.y = 0.0f;
            flat = (glm::dot(flat, flat) > 1e-6f) ? glm::normalize(flat)
                                                  : glm::vec3(0.0f, 0.0f, 1.0f);

            // Contact strike, in two halves: the warden COMMITS to a swing
            // here, and the blow lands `swingImpact` seconds later, when the
            // axe reaches the ground in the clip. Committing spends the
            // cooldown either way, so a swing you walk out from under costs
            // the warden its rhythm rather than nothing, and the animation is
            // the tell (the lunge windup's fairness rule, told with art this
            // time).
            if (dist <= sp.strikeRange && c.strikeTimer <= 0.0f && !c.swingPending) {
                c.strikeTimer = sp.strikeCooldown * rateMult;
                c.swingPending = true;
                c.swingLeft = sp.swingImpact;
                c.swingLunged = c.lungeLeft > 0.0f; // a dash that connected
                c.lungeLeft = 0.0f;                 // spends itself on the swing
                playOnce(c, "attack");
            }
            // Ordered after the commit so the swing just started is still in
            // the air, and so a species with no wind-up (swingImpact 0, which
            // is every species without a swing clip) hits on contact exactly
            // as before.
            if (c.swingPending && c.swingLeft <= 0.0f) {
                c.swingPending = false;
                if (dist <= sp.strikeRange) {
                    // Damage plus a shove away from the warden, so the fight
                    // has a hit-and-close rhythm instead of a hug.
                    ev.damageToPlayer +=
                        sp.damage * (c.swingLunged ? sp.lungeDamageMult : 1.0f);
                    // The vertical pop is rolled per strike (same hash-counter
                    // scheme the wander decisions use, so it stays
                    // deterministic and needs no RNG state of its own). Rolled
                    // as a HEIGHT and converted here, so the knobs stay in
                    // blocks.
                    const float popH = kBossKnockUpMinH +
                                       roll01(c.wanderRolls, 53u) *
                                           (kBossKnockUpMaxH - kBossKnockUpMinH);
                    const float up = std::sqrt(2.0f * kGravity * popH);
                    ev.playerKnock += flat * kBossKnockback + glm::vec3(0.0f, up, 0.0f);
                }
            }

            if (c.lungeLeft > 0.0f) {
                // Dashing: committed direction, no steering. Overrides the
                // walk below, so a lunge can overshoot -- that is the counter.
                c.lungeLeft -= dt;
                c.walking = false;
                lungeVel = c.lungeDir * sp.lungeSpeed;
                faceDir = c.lungeDir;
            } else if (c.windupLeft > 0.0f) {
                // Telegraph: planted and turning to face you. Standing still
                // IS the tell, so it reads without any new art.
                c.windupLeft -= dt;
                c.walking = false;
                c.target = playerFeet;
                faceDir = flat; // tracks you right up to the commit
                if (c.windupLeft <= 0.0f) {
                    c.lungeLeft = sp.lungeDuration;
                    c.lungeDir = flat; // committed here, not tracked
                }
            } else if (dist <= sp.aggroRadius) {
                if (sp.lungeCooldown > 0.0f && c.lungeTimer <= 0.0f &&
                    dist >= sp.lungeMinRange && dist <= sp.lungeMaxRange) {
                    c.windupLeft = sp.lungeWindup;
                    c.lungeTimer = sp.lungeCooldown * rateMult;
                    c.walking = false;
                } else {
                    c.walking = distXZ > sp.strikeRange * 0.6f; // don't jitter inside the hit box
                }
                c.target = playerFeet;
            } else {
                c.walking = false; // out of sight: the warden waits
            }
            chaseSpeedMult = speedMult;
        } else if (!c.walking) {
            c.idleTimer -= dt;
            if (c.idleTimer <= 0.0f) {
                const float ang = roll01(c.wanderRolls, 11u) * glm::two_pi<float>();
                const float rad = roll01(c.wanderRolls, 23u) * kCreatureWanderRadius;
                c.target = c.home + glm::vec3(std::cos(ang) * rad, 0.0f,
                                              std::sin(ang) * rad);
                c.walking = true;
            }
        }

        if (c.walking) {
            glm::vec3 d = c.target - c.pos;
            d.y = 0.0f;
            if (glm::dot(d, d) < 0.1f) {
                if (sp.kind == CreatureKind::Wanderer) {
                    c.walking = false;
                    c.idleTimer = kCreatureIdleMin +
                        roll01(c.wanderRolls, 37u) * (kCreatureIdleMax - kCreatureIdleMin);
                }
            } else {
                d = glm::normalize(d);
                wishVel = d * sp.walkSpeed * chaseSpeedMult;
                faceDir = d;
            }
        }

        // A committed dash replaces the walk velocity outright -- no steering
        // mid-lunge, which is what makes sidestepping it work.
        if (glm::dot(lungeVel, lungeVel) > 0.0f) wishVel = lungeVel;

        // Face the way we move (model faces -Z at yaw 0). Driven by faceDir
        // rather than the walk, so a planted boss still turns to track you.
        if (glm::dot(faceDir, faceDir) > 1e-6f) {
            const float desired = glm::degrees(std::atan2(-faceDir.x, -faceDir.z));
            const float delta = wrapDeg(desired - c.yaw);
            const float maxStep = kCreatureTurnRate * dt;
            c.yaw += glm::clamp(delta, -maxStep, maxStep);
        }

        // --- Physics: gravity + the player's axis-separated move-and-slide.
        // Being-hit knockback rides on top of the walk velocity and decays.
        c.knock *= kKnockDecay;
        c.vel.x = wishVel.x + c.knock.x;
        c.vel.z = wishVel.z + c.knock.z;
        c.vel.y = std::max(c.vel.y - kGravity * dt, -kTerminalVel);

        bool blockedX = false, blockedZ = false;
        glm::vec3 next = c.pos;
        next.x += c.vel.x * dt;
        if (!boxCollides(world, next, sp.halfW, sp.height)) c.pos.x = next.x;
        else blockedX = true;
        next = c.pos;
        next.z += c.vel.z * dt;
        if (!boxCollides(world, next, sp.halfW, sp.height)) c.pos.z = next.z;
        else blockedZ = true;

        c.grounded = false;
        next = c.pos;
        next.y += c.vel.y * dt;
        if (!boxCollides(world, next, sp.halfW, sp.height)) {
            c.pos.y = next.y;
        } else if (c.vel.y <= 0.0f) {
            // Land on the surface hit, not the cell boundary (see the
            // player's identical snap in PlayerController).
            const float surface = Collision::landingSurface(
                world, c.pos.x - sp.halfW, c.pos.x + sp.halfW,
                c.pos.z - sp.halfW, c.pos.z + sp.halfW, next.y, c.pos.y);
            c.pos.y = std::isfinite(surface) ? surface : std::floor(next.y) + 1.0f;
            c.vel.y = 0.0f;
            c.grounded = true;
        } else {
            c.vel.y = 0.0f;
        }

        // A wall on both axes means the leg is unreachable: give up, idle.
        if (c.walking && blockedX && blockedZ && sp.kind == CreatureKind::Wanderer) {
            c.walking = false;
            c.idleTimer = kCreatureIdleMin;
        }

        // Fell out of the world: quietly return home (ambient pets forgive;
        // a boss that somehow leaves the arena resets to its lair).
        if (c.pos.y < kVoidY) {
            c.pos = c.prevPos = c.home;
            c.vel = glm::vec3(0.0f);
            c.walking = false;
        }

        // --- Animation state. A swing owns the model until it plays out, so
        // a strike reads as one motion instead of being cut off by the next
        // step the boss takes (frameAdvance runs the clock down).
        if (c.attackLeft <= 0.0f) {
            const bool moving = c.walking && (!blockedX || !blockedZ);
            const int want = m_assets[static_cast<std::size_t>(c.species)]
                                 .model.findAnimation(moving ? "walk" : "idle");
            if (want != c.anim) {
                c.anim = want;
                c.animTime = 0.0f;
            }
        }
    }

    m_sinceTick = 0.0f;
    return ev;
}

// Animation clocks tick at render rate (menus keep animating, just like the
// sim keeps ticking); the caller skips this while truly paused.
void CreatureSystem::frameAdvance(float dt) {
    m_sinceTick += dt;
    for (Creature& c : m_creatures) {
        c.animTime += dt;
        c.attackLeft = std::max(0.0f, c.attackLeft - dt);
        c.hurtFlash = std::max(0.0f, c.hurtFlash - dt * kFlashDecay);
    }
}

void CreatureSystem::render(const engine::Camera& camera, float rainDim,
                            DimensionId active) {
    if (!m_shaderReady || m_creatures.empty()) return;

    m_shader.use();
    m_shader.setMat4("uProj", camera.projection());
    m_shader.setMat4("uView", camera.view());
    m_shader.setVec3("uLightDir", kLightDir);
    m_shader.setFloat("uRainDim", rainDim);

    // Blend between the last two ticks so 20 Hz movement renders smoothly.
    const float alpha = std::min(m_sinceTick / kTickSeconds, 1.0f);

    for (const Creature& c : m_creatures) {
        if (c.dim != active) continue;
        SpeciesAssets& a = m_assets[static_cast<std::size_t>(c.species)];
        if (!a.ready) continue;
        a.texture.bind(0);

        engine::evaluateBbPose(a.model, c.anim, c.animTime, m_boneScratch);
        m_boneScratch.resize(kMaxEntityBones, glm::mat4(1.0f)); // pad to uBones[]

        const glm::vec3 p = glm::mix(c.prevPos, c.pos, alpha);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), p);
        model = glm::rotate(model, glm::radians(c.yaw), {0.0f, 1.0f, 0.0f});
        model = glm::scale(model, glm::vec3(speciesOf(c.species).scale));
        m_shader.setMat4("uModel", model);
        m_shader.setFloat("uFlash", c.hurtFlash * 0.7f);
        m_shader.setMat4Array("uBones", m_boneScratch.data(), kMaxEntityBones);
        a.mesh.draw();
    }
}

const CreatureSystem::Creature* CreatureSystem::firstBoss(DimensionId dim) const {
    for (const Creature& c : m_creatures) {
        if (c.dim == dim && speciesOf(c.species).kind == CreatureKind::Boss) return &c;
    }
    return nullptr;
}

bool CreatureSystem::bossAlive(DimensionId dim) const {
    return firstBoss(dim) != nullptr;
}

float CreatureSystem::bossHpFrac(DimensionId dim) const {
    const Creature* b = firstBoss(dim);
    if (!b) return 0.0f;
    const float full = speciesOf(b->species).hp;
    return full > 0.0f ? glm::clamp(b->hp / full, 0.0f, 1.0f) : 0.0f;
}

const char* CreatureSystem::bossName(DimensionId dim) const {
    const Creature* b = firstBoss(dim);
    return b ? speciesOf(b->species).name : "";
}
