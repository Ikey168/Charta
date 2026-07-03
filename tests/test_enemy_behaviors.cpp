// Test for selectable enemy behaviors - issue #320.
//
// Verifies each behavior is deterministic and distinct: the default "enemy" chases (and
// is byte-identical to the original chaser); "enemy_patrol" oscillates around its home
// without heading for the player; "enemy_flee" moves away; and "enemy_ranged" fires
// projectiles that are a loss on contact. Pure std + the header-only game:
//   g++ -std=c++17 -I src tests/test_enemy_behaviors.cpp -o test_enemy_behaviors

#include "game/DungeonGame.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace IKore;
using namespace IKore::game;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static EntitySpawn sp(const char* t, float x, float z) {
    return EntitySpawn{t, ecs::Vec3{x, 0.0f, z}, 0.0f};
}
static float dist(const ecs::Vec3& a, const ecs::Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}
static void drive(DungeonGame& g, float mx, float mz, int steps) {
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < steps && g.status == GameStatus::Playing; ++i) g.update(GameInput{mx, mz}, dt);
}

int main() {
    // --- Default enemy chases (behavior is Chase, moves toward the player) --------
    {
        SceneDescription s;
        s.spawns = {sp("player", 0, 0), sp("exit", 20, 20), sp("enemy", 5, 0)};
        DungeonGame g = loadGame(s);
        CHECK(g.enemies.size() == 1 && g.enemies[0].behavior == EnemyBehavior::Chase);
        drive(g, 0.0f, 0.0f, 30); // player stands still; the chaser closes in
        CHECK(g.enemies[0].position.x < 5.0f);
    }

    // --- Patrol oscillates around home and ignores the player --------------------
    {
        SceneDescription s;
        s.spawns = {sp("player", 0, 20), sp("exit", 40, 40), sp("enemy_patrol", 5, 0)};
        DungeonGame g = loadGame(s);
        CHECK(g.enemies[0].behavior == EnemyBehavior::Patrol);
        float minX = 5.0f, maxX = 5.0f, maxAbsZ = 0.0f;
        for (int i = 0; i < 2000 && g.status == GameStatus::Playing; ++i) {
            g.update(GameInput{}, 1.0f / 60.0f);
            const ecs::Vec3& p = g.enemies[0].position;
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            maxAbsZ = std::max(maxAbsZ, std::fabs(p.z));
        }
        CHECK(minX < 4.0f && maxX > 6.0f);              // swings both ways around home
        CHECK(minX >= 2.0f - 0.1f && maxX <= 8.0f + 0.1f); // bounded to +-patrolRange
        CHECK(maxAbsZ < 0.1f);                          // never heads for the player (at z=20)
    }

    // --- Flee moves away from the player -----------------------------------------
    {
        SceneDescription s;
        s.spawns = {sp("player", 4, 0), sp("exit", 40, 40), sp("enemy_flee", 5, 0)};
        DungeonGame g = loadGame(s);
        CHECK(g.enemies[0].behavior == EnemyBehavior::Flee);
        const float before = dist(g.enemies[0].position, ecs::Vec3{4, 0, 0});
        drive(g, 0.0f, 0.0f, 60);
        CHECK(g.enemies[0].position.x > 5.0f);                             // fled +X, away
        CHECK(dist(g.enemies[0].position, ecs::Vec3{4, 0, 0}) > before);   // distance grew
    }

    // --- Ranged fires projectiles that are a loss on contact ---------------------
    auto runRanged = [](int& lostStep) {
        SceneDescription s;
        s.spawns = {sp("player", 0, 0), sp("exit", 40, 40), sp("enemy_ranged", 10, 0)};
        DungeonGame g = loadGame(s);
        g.update(GameInput{}, 1.0f / 60.0f); // first shot fires immediately (cooldown 0)
        const bool fired = !g.projectiles.empty();
        lostStep = -1;
        for (int i = 1; i < 400 && g.status == GameStatus::Playing; ++i) {
            g.update(GameInput{}, 1.0f / 60.0f);
            if (g.lost()) { lostStep = i; break; }
        }
        return fired && g.lost();
    };
    {
        int step1 = 0, step2 = 0;
        CHECK(runRanged(step1));
        CHECK(runRanged(step2));
        CHECK(step1 > 0);
        CHECK(step1 == step2); // deterministic: same step to the hit every run
    }

    if (g_failures == 0) {
        std::printf("test_enemy_behaviors: all checks passed\n");
        return 0;
    }
    std::printf("test_enemy_behaviors: %d check(s) failed\n", g_failures);
    return 1;
}
