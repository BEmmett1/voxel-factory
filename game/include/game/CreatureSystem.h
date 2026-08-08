#pragma once

#include "game/Dimension.h"
#include "game/Item.h"

#include "engine/BbModel.h"
#include "engine/Mesh.h"
#include "engine/Shader.h"
#include "engine/Texture.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class World;
namespace engine {
    class Audio;
    class Camera;
}

// What a creature is. Behavior code lives in per-kind dispatch inside the
// system (the MachineKind precedent).
enum class CreatureKind : std::uint8_t {
    Wanderer, // ambient: idles and strolls around its home point
    Boss,     // aggros the player, chases, strikes on contact; unique drop
};

// One registry row per species: model, body, and combat numbers. Rows live in
// CreatureSystem.cpp (kSpecies) next to the vg:: knobs they draw from.
enum class SpeciesId : std::uint8_t {
    TestCreature = 0,
    VoidWarden,
    Tempest,
    Count
};

struct CreatureSpecies {
    SpeciesId    id;
    const char*  model;          // .bbmodel path relative to the base dir
    CreatureKind kind = CreatureKind::Wanderer;
    const char*  name = "";      // shown on the boss HP bar
    float scale = 1.0f;
    float halfW = 0.35f;         // collision box
    float height = 0.9f;
    float hp = 6.0f;
    float walkSpeed = 1.6f;
    // Boss-only numbers (zero/None for ambient kinds):
    float  aggroRadius = 0.0f;
    float  strikeRange = 0.0f;   // center-to-center hit distance
    float  damage = 0.0f;        // hearts per strike
    float  strikeCooldown = 0.0f;
    // Seconds from committing to a swing until the blow lands, so a species
    // with a wind-up animation hits when its weapon does. 0 = on contact,
    // which is what a species with no swing clip wants.
    float  swingImpact = 0.0f;
    ItemId drop = ItemId::None;  // awarded on the killing blow
    // Lunge: a telegraphed charge that closes distance a walk can't. Zero
    // cooldown disables it, so a species opts in by filling these five.
    // The windup is the fairness: the boss visibly STOPS before it fires.
    float lungeCooldown = 0.0f;  // 0 = no lunge
    float lungeWindup = 0.0f;    // frozen telegraph before the dash
    float lungeSpeed = 0.0f;     // dash speed (set above player sprint to bite)
    float lungeDuration = 0.0f;  // how long the dash lasts
    float lungeMinRange = 0.0f;  // too close to bother
    float lungeMaxRange = 0.0f;  // too far to commit
    float lungeDamageMult = 1.0f; // strike damage scale while dashing
    // Enrage: below this fraction of max HP the boss speeds up and its
    // cooldowns shrink, so finishing it is the dangerous part. 0 = never.
    float enrageAt = 0.0f;
    float enrageSpeedMult = 1.0f;
    float enrageRateMult = 1.0f; // multiplies cooldowns (<1 = more often)
};

// The entity layer: every live creature across every dimension, plus the
// per-species GPU assets. Operates on World& and engine services passed in
// (never VoxelGame&). Creatures are transient — never saved; the arena's are
// cleared on every entry, the home wanderer respawns each launch.
class CreatureSystem {
public:
    // Load every species' model + texture and the shared entity shader.
    // Missing/broken assets leave that species inert (logged, never fatal).
    void loadAssets(const std::string& dir);

    // Spawn at the hint's column, snapped down onto solid ground near the
    // hint (scans a few blocks up, then down). No ground = no creature.
    void spawn(SpeciesId species, DimensionId dim, const World& world,
               const glm::vec3& feetHint);

    void clearDimension(DimensionId dim); // despawn (arena reset / regen)

    // What update() observed this tick, for the caller to react to.
    struct Events {
        float damageToPlayer = 0.0f;   // boss strikes landed (hearts)
        glm::vec3 playerKnock{0.0f};   // shove impulse from those strikes
    };

    // Fixed 20 Hz step for creatures IN the active dimension (others freeze,
    // like the whole game does under the pause rule). playerFeet drives boss
    // aggro/chase/strikes.
    Events update(const World& world, DimensionId active, const glm::vec3& playerFeet);

    // Per-frame clocks (tick-lerp alpha, animation time, hurt-flash fade).
    void frameAdvance(float dt);

    // Interpolated skinned draw of the active dimension's creatures.
    void render(const engine::Camera& camera, float rainDim, DimensionId active);

    // Swing the sword along the aim ray at the active dimension's creatures.
    struct MeleeResult {
        bool   hit = false;          // connected (caller skips mining)
        bool   bossDied = false;     // the killing blow landed on a Boss
        ItemId drop = ItemId::None;  // that boss's species drop
        SpeciesId bossSpecies = SpeciesId::TestCreature; // which boss fell
        const char* bossName = "";   // for the victory message
    };
    // Melee swing (short reach, knockback). `damage` lets the Elixir of Vigor
    // buff scale the hit; callers pass the base sword damage times the buff.
    MeleeResult tryMeleeAttack(const World& world, engine::Audio& audio,
                               const glm::vec3& origin, const glm::vec3& dir,
                               DimensionId active, float damage);
    // Mana Vial bolt: same ray-vs-creature pick, longer reach, no knockback.
    MeleeResult tryRangedAttack(const World& world, engine::Audio& audio,
                                const glm::vec3& origin, const glm::vec3& dir,
                                DimensionId active, float damage, float reach);

    // Boss HP bar feed: the first living Boss in `dim`, if any.
    bool  bossAlive(DimensionId dim) const;
    float bossHpFrac(DimensionId dim) const;  // 0..1 (0 if none)
    const char* bossName(DimensionId dim) const;

private:
    struct Creature {
        SpeciesId   species = SpeciesId::TestCreature;
        DimensionId dim = DimensionId::Overworld;
        glm::vec3 pos{0.0f}, prevPos{0.0f}; // feet; prevPos = last tick (render lerp)
        glm::vec3 vel{0.0f};
        float yaw = 0.0f;                   // degrees; 0 faces -Z like the model
        glm::vec3 home{0.0f}, target{0.0f};
        float idleTimer = 1.0f;             // counts down while standing
        bool  walking = false, grounded = false;
        int   anim = -1;                    // index into the model's animations
        float animTime = 0.0f;              // frozen while the engine is paused
        float attackLeft = 0.0f;            // >0: a one-shot swing owns the model
        bool  swingPending = false;         // swing in flight; its axe hasn't landed
        float swingLeft = 0.0f;             // seconds until it does
        bool  swingLunged = false;          // that swing started mid-dash (bonus)
        std::uint32_t wanderRolls = 0;      // hash counter for wander decisions
        float     hp = 0.0f;                // set from the species row on spawn
        glm::vec3 knock{0.0f};              // decaying shove from being hit
        float     hurtFlash = 0.0f;         // 0..1 red tint, fades per frame
        float     strikeTimer = 0.0f;       // Boss: seconds until the next hit
        // Lunge state machine: cooling down -> winding up -> dashing.
        float     lungeTimer = 0.0f;        // until the next lunge is allowed
        float     windupLeft = 0.0f;        // >0: frozen, telegraphing
        float     lungeLeft = 0.0f;         // >0: dashing along lungeDir
        glm::vec3 lungeDir{0.0f};           // committed at windup end (XZ, unit)
    };

    struct SpeciesAssets {
        engine::BbModel model;
        engine::Mesh    mesh;
        engine::Texture texture;
        bool ready = false;
    };

    const Creature* firstBoss(DimensionId dim) const;

    // Play a species' one-shot clip (the boss's swing) from the top, holding
    // off walk/idle until it ends. A model without that clip is left alone,
    // so a species opts in purely by having animated one.
    void playOnce(Creature& c, const char* clip);

    // Nearest creature in `active` struck by the ray within `reach`, blocked by
    // a nearer solid block. Returns its index (-1 = miss). Shared by melee/ranged.
    int rayPickCreature(const World& world, const glm::vec3& origin,
                        const glm::vec3& dir, DimensionId active, float reach) const;

    std::array<SpeciesAssets, static_cast<std::size_t>(SpeciesId::Count)> m_assets;
    engine::Shader         m_shader;
    bool                   m_shaderReady = false;
    std::vector<Creature>  m_creatures;
    std::vector<glm::mat4> m_boneScratch;   // reused per draw
    float m_sinceTick = 0.0f;               // seconds since last update() (render lerp)
};
