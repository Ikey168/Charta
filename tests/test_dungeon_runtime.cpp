// Test for the DungeonGame -> live ECS runtime bridge - engine wiring follow-on.
//
// Verifies the deterministic dungeon sim drives real engine ECS entities: spawnInto
// creates one Transform+SpawnTag entity per actor, the decorate hook can attach engine
// components, update() steps the sim and writes actor positions onto their Transforms,
// a collected coin's entity is destroyed, and enemies track their moving positions.
// Pure std + the header-only ECS / game:
//   g++ -std=c++17 -I src tests/test_dungeon_runtime.cpp -o test_dungeon_runtime

#include "core/ecs/View.h"
#include "game/DungeonRuntime.h"

#include <cmath>
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

// Stand-in for a render component the live engine would attach via the decorate hook.
struct RenderStub {
    int marker{0};
};

static game::EntitySpawn sp(const char* t, float x, float z) {
    return game::EntitySpawn{t, ecs::Vec3{x, 0.0f, z}, 0.0f};
}
static int countTagged(ecs::Registry& reg) {
    int n = 0;
    reg.view<game::SpawnTag>().each([&](ecs::Entity, game::SpawnTag&) { ++n; });
    return n;
}

int main() {
    // Player, one coin on the path, exit past it; no enemy so a straight run wins.
    game::SceneDescription scene;
    scene.spawns = {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 4, 0)};

    game::DungeonRuntime rt(game::loadGame(scene));
    ecs::Registry reg;

    int decorated = 0;
    rt.spawnInto(reg, [&](ecs::Registry& r, ecs::Entity e, const std::string&) {
        r.add<RenderStub>(e, RenderStub{1});
        ++decorated;
    });

    // One entity per actor, each tagged and decorated.
    CHECK(countTagged(reg) == 3); // player + coin + exit
    CHECK(decorated == 3);
    CHECK(reg.isValid(rt.playerEntity()) && reg.has<RenderStub>(rt.playerEntity()));
    CHECK(rt.coinEntities().size() == 1);
    CHECK(rt.hasExitEntity());

    // Player entity's Transform starts at the spawn.
    CHECK(reg.get<ecs::Transform>(rt.playerEntity()).position.x == 0.0f);

    // Drive east: the Transform tracks the sim, the coin gets collected and destroyed.
    const float dt = 1.0f / 60.0f;
    bool coinGone = false;
    for (int i = 0; i < 400 && rt.game().status == game::GameStatus::Playing; ++i) {
        rt.update(reg, game::GameInput{1.0f, 0.0f}, dt);
        // Transform mirrors the deterministic sim every step.
        CHECK(reg.get<ecs::Transform>(rt.playerEntity()).position.x == rt.game().playerPosition.x);
        if (rt.game().coinsCollected == 1 && !reg.isValid(rt.coinEntities()[0])) coinGone = true;
    }
    CHECK(rt.game().coinsCollected == 1);
    CHECK(coinGone);                              // collected coin's entity was destroyed
    CHECK(!reg.isValid(rt.coinEntities()[0]));
    CHECK(countTagged(reg) == 2);                // player + exit remain
    CHECK(rt.game().won());

    // Enemy entities track their moving positions.
    {
        game::SceneDescription es;
        es.spawns = {sp("player", 0, 0), sp("enemy", 5, 0), sp("exit", 20, 20)};
        game::DungeonRuntime ert(game::loadGame(es));
        ecs::Registry ereg;
        ert.spawnInto(ereg);
        CHECK(ert.enemyEntities().size() == 1);
        const float ex0 = ereg.get<ecs::Transform>(ert.enemyEntities()[0]).position.x;
        for (int i = 0; i < 30; ++i) ert.update(ereg, game::GameInput{0.0f, 0.0f}, dt);
        const float ex1 = ereg.get<ecs::Transform>(ert.enemyEntities()[0]).position.x;
        CHECK(ex1 < ex0); // the chaser moved toward the player, and its Transform followed
        CHECK(ex1 == ert.game().enemies[0].position.x);
    }

    if (g_failures == 0) {
        std::printf("test_dungeon_runtime: all checks passed\n");
        return 0;
    }
    std::printf("test_dungeon_runtime: %d check(s) failed\n", g_failures);
    return 1;
}
