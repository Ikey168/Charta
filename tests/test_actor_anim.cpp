// Drive animated actor states with crossfade - issue #351.
//
// The renderer-agnostic decision layer: player/enemy gameplay state maps to animation clips
// (idle/run/attack/flee/win/lose), and a deterministic Crossfade blends from the current
// clip to a newly chosen one over a fixed duration. Verifies the state->clip mapping, the
// crossfade blend progression and retargeting, and that identical state/dt sequences produce
// identical animation state. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_actor_anim.cpp -o test_actor_anim

#include "game/ActorAnim.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace IKore;
using namespace IKore::game;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

int main() {
    // 1. Player clip mapping: end states override movement.
    CHECK(playerClip(false, GameStatus::Playing) == ActorClip::Idle);
    CHECK(playerClip(true, GameStatus::Playing) == ActorClip::Run);
    CHECK(playerClip(true, GameStatus::Won) == ActorClip::Win);
    CHECK(playerClip(false, GameStatus::Won) == ActorClip::Win);
    CHECK(playerClip(true, GameStatus::Lost) == ActorClip::Lose);

    // 2. Enemy clip mapping by behavior.
    CHECK(enemyClip(EnemyBehavior::Ranged, false, GameStatus::Playing) == ActorClip::Attack);
    CHECK(enemyClip(EnemyBehavior::Ranged, true, GameStatus::Playing) == ActorClip::Attack);
    CHECK(enemyClip(EnemyBehavior::Flee, true, GameStatus::Playing) == ActorClip::Flee);
    CHECK(enemyClip(EnemyBehavior::Flee, false, GameStatus::Playing) == ActorClip::Idle);
    CHECK(enemyClip(EnemyBehavior::Chase, true, GameStatus::Playing) == ActorClip::Run);
    CHECK(enemyClip(EnemyBehavior::Chase, false, GameStatus::Playing) == ActorClip::Idle);
    CHECK(enemyClip(EnemyBehavior::Patrol, true, GameStatus::Playing) == ActorClip::Run);
    CHECK(enemyClip(EnemyBehavior::Chase, true, GameStatus::Won) == ActorClip::Idle);   // round over
    CHECK(enemyClip(EnemyBehavior::Ranged, true, GameStatus::Lost) == ActorClip::Idle); // round over

    // 3. Clip names.
    CHECK(std::string(clipName(ActorClip::Run)) == "run");
    CHECK(std::string(clipName(ActorClip::Win)) == "win");

    // 4. Crossfade progression and retargeting.
    {
        Crossfade cf;
        cf.duration = 0.25f;
        CHECK(cf.blend == 1.0f && cf.current == ActorClip::Idle);
        cf.retarget(ActorClip::Run);
        CHECK(cf.target == ActorClip::Run && cf.current == ActorClip::Idle && cf.blend == 0.0f);
        CHECK(cf.blending());
        cf.update(0.1f);
        CHECK(std::fabs(cf.blend - 0.4f) < 1e-5f);
        cf.update(0.1f);
        CHECK(std::fabs(cf.blend - 0.8f) < 1e-5f);
        cf.update(0.1f); // overshoots -> clamps, current becomes target
        CHECK(cf.blend == 1.0f);
        CHECK(cf.current == ActorClip::Run && !cf.blending());
        cf.retarget(ActorClip::Run); // same target: no-op
        CHECK(cf.blend == 1.0f);
    }

    // 5. ActorAnimator driven by a state sequence: idle -> run -> win.
    {
        ActorAnimator a;
        a.setDuration(0.2f);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 30; ++i) a.drivePlayer(false, GameStatus::Playing, dt);
        CHECK(a.currentClip() == ActorClip::Idle);
        CHECK(a.blend() == 1.0f);

        a.drivePlayer(true, GameStatus::Playing, dt); // start moving
        CHECK(a.targetClip() == ActorClip::Run);
        CHECK(a.blending());
        for (int i = 0; i < 30; ++i) a.drivePlayer(true, GameStatus::Playing, dt);
        CHECK(a.currentClip() == ActorClip::Run);
        CHECK(!a.blending());

        a.drivePlayer(true, GameStatus::Won, dt); // win overrides
        CHECK(a.targetClip() == ActorClip::Win);
    }

    // 6. Determinism: identical (state, dt) sequences yield identical animation state.
    {
        struct Step { bool moving; GameStatus status; };
        const std::vector<Step> seq = {{false, GameStatus::Playing}, {true, GameStatus::Playing},
                                       {true, GameStatus::Playing},  {false, GameStatus::Playing},
                                       {true, GameStatus::Playing},  {true, GameStatus::Won}};
        ActorAnimator a, b;
        a.setDuration(0.15f);
        b.setDuration(0.15f);
        const float dt = 1.0f / 60.0f;
        for (int rep = 0; rep < 5; ++rep)
            for (const Step& s : seq) {
                a.drivePlayer(s.moving, s.status, dt);
                b.drivePlayer(s.moving, s.status, dt);
                CHECK(a.currentClip() == b.currentClip());
                CHECK(a.targetClip() == b.targetClip());
                CHECK(a.blend() == b.blend());
            }
    }

    if (g_failures == 0) {
        std::printf("test_actor_anim: all checks passed\n");
        return 0;
    }
    std::printf("test_actor_anim: %d check(s) failed\n", g_failures);
    return 1;
}
