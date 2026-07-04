// Per-type mesh + material decoration for the live renderer - issue #350 (headless core).
//
// Verifies the renderer-agnostic decision: meshKindForType gives a distinct primitive for
// each core actor type, and meshDecorator() attaches a RenderKind (mesh kind + material
// color) to every spawned entity via the DungeonRuntime decorate hook. The GL draw of these
// kinds is exercised by the gl-labelled test_dungeon_mesh_gl. Pure std + header-only ECS:
//   g++ -std=c++17 -I src tests/test_dungeon_decor.cpp -o test_dungeon_decor

#include "game/DungeonDecor.h"
#include "game/DungeonRuntime.h"

#include <cstdio>
#include <set>

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

static bool sameColor(const RenderColor& a, const RenderColor& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

int main() {
    // Core actor types map to distinct primitive meshes (readable at a glance).
    const MeshKind player = meshKindForType("player");
    const MeshKind enemy = meshKindForType("enemy");
    const MeshKind coin = meshKindForType("coin");
    const MeshKind exit = meshKindForType("exit");
    const MeshKind wall = meshKindForType("wall");
    std::set<MeshKind> kinds = {player, enemy, coin, exit, wall};
    CHECK(kinds.size() == 5); // all five distinct

    // Enemy behavior variants share the enemy mesh.
    CHECK(meshKindForType("enemy_patrol") == enemy);
    CHECK(meshKindForType("enemy_ranged") == enemy);
    // A key is its own recognizable shape.
    CHECK(meshKindForType("key") == MeshKind::Diamond);
    CHECK(meshKindForType("player") == MeshKind::Sphere);
    CHECK(meshKindForType("wall") == MeshKind::Cube);

    // meshDecorator attaches a RenderKind (mesh + material) to each spawned entity.
    SceneDescription scene;
    scene.spawns = {EntitySpawn{"player", ecs::Vec3{0, 0, 0}, 0.0f},
                    EntitySpawn{"enemy", ecs::Vec3{5, 0, 5}, 0.0f},
                    EntitySpawn{"coin", ecs::Vec3{2, 0, 0}, 0.0f},
                    EntitySpawn{"exit", ecs::Vec3{8, 0, 8}, 0.0f}};
    DungeonRuntime rt(loadGame(scene));
    ecs::Registry reg;
    rt.spawnInto(reg, meshDecorator());

    CHECK(reg.has<RenderKind>(rt.playerEntity()));
    const RenderKind& pk = reg.get<RenderKind>(rt.playerEntity());
    CHECK(pk.kind == MeshKind::Sphere);
    CHECK(sameColor(pk.color, renderColorForType("player")));

    CHECK(rt.enemyEntities().size() == 1);
    CHECK(reg.get<RenderKind>(rt.enemyEntities()[0]).kind == MeshKind::Pyramid);
    CHECK(rt.coinEntities().size() == 1);
    CHECK(reg.get<RenderKind>(rt.coinEntities()[0]).kind == MeshKind::Disc);
    CHECK(rt.hasExitEntity());
    CHECK(reg.get<RenderKind>(rt.exitEntity()).kind == MeshKind::Prism);
    CHECK(sameColor(reg.get<RenderKind>(rt.exitEntity()).color, renderColorForType("exit")));

    if (g_failures == 0) {
        std::printf("test_dungeon_decor: all checks passed\n");
        return 0;
    }
    std::printf("test_dungeon_decor: %d check(s) failed\n", g_failures);
    return 1;
}
