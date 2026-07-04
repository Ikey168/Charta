// Test for the DungeonRuntime render-data builder - issue #333.
//
// Verifies buildRenderList turns a live runtime into a draw list: static level features
// from the game (walls, locked door, toggle wall, key, switch, hazard) plus the live
// actors from their ECS SpawnTag entities (player/enemy/coin/exit), each with the right
// per-type color; and that collected/opened/toggled things drop out of the list. Pure std
// + the header-only ECS / game:
//   g++ -std=c++17 -I src tests/test_dungeon_render_data.cpp -o test_dungeon_render_data

#include "game/DungeonRenderData.h"
#include "game/DungeonRuntime.h"

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
static bool sameColor(const RenderColor& a, const RenderColor& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
static const RenderInstance* find(const std::vector<RenderInstance>& v, const char* type) {
    for (const auto& i : v) if (i.type == type) return &i;
    return nullptr;
}

int main() {
    SceneDescription scene;
    world::Box w1, w2;
    w1.center = ecs::Vec3{0.0f, 0.0f, -3.0f};
    w1.size = ecs::Vec3{2.0f, 3.0f, 0.4f};
    w2.center = ecs::Vec3{8.0f, 0.0f, -3.0f};
    w2.size = ecs::Vec3{2.0f, 3.0f, 0.4f};
    scene.wallBoxes = {w1, w2};
    // Pickups on a straight +X path; hazard/enemy off it so a short drive is safe.
    scene.spawns = {sp("player", 0, 0),   sp("enemy", 20, 20), sp("coin", 1, 0),
                    sp("exit", 8, 0),      sp("key@1", 2, 0),   sp("lock@1", 4, 0),
                    sp("switch@1", 3, 0),  sp("toggle@1", 5, 0), sp("hazard", 0, 5)};

    DungeonRuntime rt(loadGame(scene));
    ecs::Registry reg;
    rt.spawnInto(reg);

    // Initial draw list: statics from the game + actors from the ECS.
    std::vector<RenderInstance> list = buildRenderList(reg, rt.game());
    CHECK(countRenderType(list, "wall") == 2);
    CHECK(countRenderType(list, "lockeddoor") == 1);
    CHECK(countRenderType(list, "togglewall") == 1);
    CHECK(countRenderType(list, "key") == 1);
    CHECK(countRenderType(list, "switch") == 1);
    CHECK(countRenderType(list, "hazard") == 1);
    CHECK(countRenderType(list, "player") == 1);
    CHECK(countRenderType(list, "enemy") == 1);
    CHECK(countRenderType(list, "coin") == 1);
    CHECK(countRenderType(list, "exit") == 1);
    CHECK(list.size() == 11);

    // Per-type colors and placement.
    const RenderInstance* player = find(list, "player");
    const RenderInstance* coin = find(list, "coin");
    const RenderInstance* wall = find(list, "wall");
    CHECK(player && sameColor(player->color, renderColorForType("player")));
    CHECK(player && player->position.x == 0.0f);
    CHECK(coin && sameColor(coin->color, renderColorForType("coin")));
    CHECK(wall && sameColor(wall->color, RenderStyle{}.wall));

    // Drive east: collect the coin and key, press the switch. That collects the coin
    // (entity destroyed), collects the key (opens lock@1), and toggles toggle@1 open.
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 400 && rt.game().playerPosition.x < 3.4f &&
                    rt.game().status == GameStatus::Playing;
         ++i) {
        rt.update(reg, GameInput{1.0f, 0.0f}, dt);
    }
    CHECK(rt.game().status == GameStatus::Playing);

    list = buildRenderList(reg, rt.game());
    CHECK(countRenderType(list, "coin") == 0);       // collected -> entity destroyed
    CHECK(countRenderType(list, "key") == 0);        // collected
    CHECK(countRenderType(list, "lockeddoor") == 0); // opened by the matching key
    CHECK(countRenderType(list, "togglewall") == 0); // switched off
    CHECK(countRenderType(list, "player") == 1);
    const RenderInstance* movedPlayer = find(list, "player");
    CHECK(movedPlayer && movedPlayer->position.x > 1.0f); // its transform advanced
    CHECK(countRenderType(list, "wall") == 2);           // statics unaffected
    CHECK(countRenderType(list, "exit") == 1);
    CHECK(countRenderType(list, "hazard") == 1);

    if (g_failures == 0) {
        std::printf("test_dungeon_render_data: all checks passed\n");
        return 0;
    }
    std::printf("test_dungeon_render_data: %d check(s) failed\n", g_failures);
    return 1;
}
