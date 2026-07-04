// Test that the rollback desync check covers the #319/#320 state - issue #337.
//
// DoodleNetState::operator== and stateDigest previously ignored keys, locked doors,
// switches, toggle walls, and projectiles, so a divergence in those fields would go
// undetected. This verifies both now catch a divergence in each of them, that identical
// states still match, and that classic-level detection is unchanged. Pure std + the
// header-only game/net:
//   g++ -std=c++17 -I src tests/test_desync_features.cpp -o test_desync_features

#include "game/DoodleRollback.h"

#include <cstdio>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static game::EntitySpawn sp(const char* t, float x, float z) {
    return game::EntitySpawn{t, ecs::Vec3{x, 0.0f, z}, 0.0f};
}

// base != mutated by BOTH operator== and the digest.
static void expectDivergence(const game::DoodleNetState& base, const game::DoodleNetState& mutated,
                             const char* what) {
    if (!(base != mutated)) {
        std::printf("FAIL: operator== missed divergence in %s\n", what);
        ++g_failures;
    }
    if (game::stateDigest(base) == game::stateDigest(mutated)) {
        std::printf("FAIL: stateDigest missed divergence in %s\n", what);
        ++g_failures;
    }
}

int main() {
    // A level exercising every new-type field, plus a ranged enemy so a projectile exists.
    game::SceneDescription scene;
    scene.spawns = {sp("player", 0, 0),   sp("exit", 20, 20),   sp("key@1", 1, 0),
                    sp("lock@1", 3, 0),    sp("switch@1", 1, 2), sp("toggle@1", 5, 0),
                    sp("enemy_ranged", 10, 10)};
    game::DungeonGame g = game::loadGame(scene);
    g.update(game::GameInput{}, 1.0f / 60.0f); // ranged enemy fires -> a projectile exists

    game::DoodleNetState base;
    base.games.push_back(g);
    CHECK(base.games[0].keys.size() == 1);
    CHECK(base.games[0].lockedDoors.size() == 1);
    CHECK(base.games[0].switches.size() == 1);
    CHECK(base.games[0].toggleWalls.size() == 1);
    CHECK(!base.games[0].projectiles.empty());

    // Identical states match, by both checks.
    game::DoodleNetState same = base;
    CHECK(base == same);
    CHECK(game::stateDigest(base) == game::stateDigest(same));

    // A divergence in each new mutable field is detected.
    { auto m = base; m.games[0].keys[0].collected = true;            expectDivergence(base, m, "key.collected"); }
    { auto m = base; m.games[0].lockedDoors[0].open = true;          expectDivergence(base, m, "door.open"); }
    { auto m = base; m.games[0].switches[0].wasPressed = !m.games[0].switches[0].wasPressed;
                                                                     expectDivergence(base, m, "switch.wasPressed"); }
    { auto m = base; m.games[0].toggleWalls[0].solid = !m.games[0].toggleWalls[0].solid;
                                                                     expectDivergence(base, m, "toggleWall.solid"); }
    { auto m = base; m.games[0].projectiles[0].life -= 1.0f;         expectDivergence(base, m, "projectile.life"); }
    { auto m = base; m.games[0].projectiles[0].position.x += 1.0f;   expectDivergence(base, m, "projectile.position"); }

    // Classic levels: still detected, and identical still match (no regression).
    {
        game::SceneDescription cs;
        cs.spawns = {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 4, 0)};
        game::DoodleNetState c;
        c.games.push_back(game::loadGame(cs));
        game::DoodleNetState c2 = c;
        CHECK(c == c2 && game::stateDigest(c) == game::stateDigest(c2));
        auto m = c;
        m.games[0].coins[0].collected = true;
        expectDivergence(c, m, "classic coin.collected");
    }

    if (g_failures == 0) {
        std::printf("test_desync_features: all checks passed\n");
        return 0;
    }
    std::printf("test_desync_features: %d check(s) failed\n", g_failures);
    return 1;
}
