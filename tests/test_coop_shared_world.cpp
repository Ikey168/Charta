// Co-op shared-world mode over rollback - issue #356.
//
// One shared DungeonGame with N avatars: folding one input per avatar into a deterministic
// step, shared coin pickup, the shared win (every living avatar on the exit with all coins)
// and loss (all avatars dead), determinism (identical inputs -> identical digest), and a
// divergent input caught by a digest mismatch (desync). Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_coop_shared_world.cpp -o test_coop_shared_world

#include "game/CoopGame.h"

#include <cstdio>
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

static EntitySpawn sp(const char* t, float x, float z) { return EntitySpawn{t, ecs::Vec3{x, 0.0f, z}, 0.0f}; }
static const float kDt = 1.0f / 60.0f;

int main() {
    SceneDescription scene;
    scene.spawns = {sp("player", 0, 0), sp("coin", 3, 0), sp("exit", 6, 0)};
    const DungeonGame base = loadGame(scene);

    // 1. Co-op clear: both avatars collect the shared coin and reach the exit.
    {
        CoopGame g = makeCoopGame(base, 2);
        CHECK(g.avatars.size() == 2);
        for (int i = 0; i < 600 && g.status == GameStatus::Playing; ++i)
            g.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
        CHECK(g.won());
        CHECK(g.world.coinsCollected == 1); // shared: collected once
        CHECK(g.aliveCount() == 2);
    }

    // 2. The shared win requires EVERY living avatar on the exit: avatar 0 parks on the
    //    exit and the level stays unfinished until avatar 1 arrives too.
    {
        CoopGame g = makeCoopGame(base, 2);
        for (int i = 0; i < 600 && g.status == GameStatus::Playing; ++i) {
            const GameInput a0 = g.avatars[0].position.x < 5.9f ? GameInput{1, 0} : GameInput{0, 0};
            g.step({a0, GameInput{0, 0}}, kDt); // avatar 0 to the exit, avatar 1 waits
        }
        CHECK(!g.won());                        // avatar 1 is not at the exit yet
        CHECK(g.world.coinsCollected == 1);     // avatar 0 already grabbed the coin
        CHECK(g.avatars[0].position.x >= 5.9f);  // parked on the exit
        for (int i = 0; i < 600 && g.status == GameStatus::Playing; ++i) {
            const GameInput a1 = g.avatars[1].position.x < 5.9f ? GameInput{1, 0} : GameInput{0, 0};
            g.step({GameInput{0, 0}, a1}, kDt); // avatar 0 holds, bring avatar 1 in
        }
        CHECK(g.won());
    }

    // 3. Determinism: identical inputs -> identical digest every tick.
    {
        CoopGame a = makeCoopGame(base, 2), b = makeCoopGame(base, 2);
        for (int i = 0; i < 120; ++i) {
            a.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
            b.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
            CHECK(coopDigest(a) == coopDigest(b));
        }
    }

    // 4. A divergent input for one avatar is caught by a digest mismatch.
    {
        CoopGame a = makeCoopGame(base, 2), b = makeCoopGame(base, 2);
        for (int i = 0; i < 60; ++i) {
            a.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
            b.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
        }
        CHECK(coopDigest(a) == coopDigest(b));  // in sync
        a.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
        b.step({GameInput{1, 0}, GameInput{0, 1}}, kDt); // avatar 1 diverges
        CHECK(coopDigest(a) != coopDigest(b));  // desync detected
    }

    // 5. Loss: both avatars die to a hazard.
    {
        SceneDescription hs;
        hs.spawns = {sp("player", 0, 0), sp("hazard", 1, 0), sp("exit", 6, 0)};
        CoopGame g = makeCoopGame(loadGame(hs), 2);
        for (int i = 0; i < 600 && g.status == GameStatus::Playing; ++i)
            g.step({GameInput{1, 0}, GameInput{1, 0}}, kDt);
        CHECK(g.lost());
        CHECK(g.aliveCount() == 0);
    }

    // 6. CoopSession wrapper drives the same shared world to a win.
    {
        CoopSession sess(base, 2);
        CHECK(sess.valid());
        for (int i = 0; i < 600 && sess.status() == GameStatus::Playing; ++i)
            sess.advance({GameInput{1, 0}, GameInput{1, 0}});
        CHECK(sess.status() == GameStatus::Won);
        CHECK(sess.tick() > 0);
    }

    if (g_failures == 0) {
        std::printf("test_coop_shared_world: all checks passed\n");
        return 0;
    }
    std::printf("test_coop_shared_world: %d check(s) failed\n", g_failures);
    return 1;
}
