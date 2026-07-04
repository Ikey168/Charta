#pragma once

#include "game/DungeonGame.h" // GameStatus, EnemyBehavior

namespace IKore {
namespace game {

/**
 * @file ActorAnim.h
 * @brief Drive actor animation clips + crossfades from gameplay state (issue #351).
 *
 * Skinned animation and crossfade transitions exist in the engine (#287/#321,
 * AnimationComponent::blendToAnimation). This is the renderer-agnostic decision layer that
 * turns deterministic gameplay state into "which clip, blending how far": a player maps to
 * idle / run / win / lose, an enemy to idle / run / flee / attack by its behavior, and a
 * small deterministic Crossfade tracks the blend from the current clip to a newly chosen
 * one over a fixed duration. The live renderer feeds ActorAnimator per frame and calls
 * AnimationComponent::blendToAnimation(clipName(targetClip), duration) when the target
 * changes; here it stays header-only and headlessly testable (no glm / GL).
 */

/// An abstract animation clip an actor can be in.
enum class ActorClip { Idle, Run, Attack, Flee, Win, Lose };

/// The clip's asset name (what AnimationComponent::blendToAnimation is called with).
inline const char* clipName(ActorClip c) {
    switch (c) {
        case ActorClip::Idle: return "idle";
        case ActorClip::Run: return "run";
        case ActorClip::Attack: return "attack";
        case ActorClip::Flee: return "flee";
        case ActorClip::Win: return "win";
        case ActorClip::Lose: return "lose";
    }
    return "idle";
}

/// Player clip: a win/lose end state overrides movement; otherwise moving -> run, still -> idle.
inline ActorClip playerClip(bool moving, GameStatus status) {
    if (status == GameStatus::Won) return ActorClip::Win;
    if (status == GameStatus::Lost) return ActorClip::Lose;
    return moving ? ActorClip::Run : ActorClip::Idle;
}

/// Enemy clip by behavior: ranged attacks, a fleeing enemy flees while moving, chasers and
/// patrollers run while moving; anything idle stands. Once the round is over, enemies idle.
inline ActorClip enemyClip(EnemyBehavior behavior, bool moving, GameStatus status) {
    if (status != GameStatus::Playing) return ActorClip::Idle;
    if (behavior == EnemyBehavior::Ranged) return ActorClip::Attack;
    if (behavior == EnemyBehavior::Flee) return moving ? ActorClip::Flee : ActorClip::Idle;
    return moving ? ActorClip::Run : ActorClip::Idle; // Chase, Patrol
}

/**
 * @brief A deterministic crossfade between the current clip and a target clip.
 *
 * @c blend runs 0 (fully current) to 1 (fully target) over @c duration. retarget() to a new
 * clip folds the previous (near-complete) blend into the base and starts a fresh fade, so
 * the same state/dt sequence always yields the same blend - the property tests rely on.
 */
struct Crossfade {
    ActorClip current{ActorClip::Idle};
    ActorClip target{ActorClip::Idle};
    float blend{1.0f};    ///< 0 = fully current, 1 = fully target.
    float duration{0.25f};

    void retarget(ActorClip clip) {
        if (clip == target) return;
        current = target; // the previous target becomes the base for the new fade
        target = clip;
        blend = 0.0f;
    }
    void update(float dt) {
        if (blend >= 1.0f) return;
        if (duration <= 0.0f) { blend = 1.0f; current = target; return; }
        blend += dt / duration;
        if (blend >= 1.0f) { blend = 1.0f; current = target; }
    }
    bool blending() const { return blend < 1.0f && current != target; }
    float targetWeight() const { return blend; }
};

/// Per-actor animator: choose the clip from gameplay state and advance the crossfade.
struct ActorAnimator {
    Crossfade cf;

    void setDuration(float d) { cf.duration = d; }
    void drivePlayer(bool moving, GameStatus status, float dt) {
        cf.retarget(playerClip(moving, status));
        cf.update(dt);
    }
    void driveEnemy(EnemyBehavior behavior, bool moving, GameStatus status, float dt) {
        cf.retarget(enemyClip(behavior, moving, status));
        cf.update(dt);
    }
    ActorClip currentClip() const { return cf.current; }
    ActorClip targetClip() const { return cf.target; }
    float blend() const { return cf.blend; }
    bool blending() const { return cf.blending(); }
};

} // namespace game
} // namespace IKore
