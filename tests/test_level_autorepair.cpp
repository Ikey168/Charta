// Auto-repair unsolvable captured levels - issue #362.
//
// The Solver's diagnostics drive minimal, reported fixes: an unreachable exit or coin is
// relocated onto the nearest reachable cell, an already-solvable level is untouched, the
// repair is deterministic and idempotent, and a suggest-only mode reports edits without
// applying them. Pure std + the header-only game / solver:
//   g++ -std=c++17 -I src tests/test_level_autorepair.cpp -o test_level_autorepair

#include "game/LevelRepair.h"

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

static world::Box wall(float cx, float cz, float sx, float sz) {
    world::Box b;
    b.center = ecs::Vec3{cx, 0.0f, cz};
    b.size = ecs::Vec3{sx, 3.0f, sz};
    b.yaw = 0.0f;
    return b;
}
// Four walls fully enclosing (cx,cz) in a 2*half square, so anything inside is stranded.
static std::vector<world::Box> boxAround(float cx, float cz, float half = 1.5f) {
    const float t = 0.4f, span = 2.0f * half + t;
    return {wall(cx, cz + half, span, t), wall(cx, cz - half, span, t),
            wall(cx - half, cz, t, span), wall(cx + half, cz, t, span)};
}
static EntitySpawn sp(const char* t, float x, float z) { return EntitySpawn{t, ecs::Vec3{x, 0.0f, z}, 0.0f}; }
static DungeonGame makeGame(std::vector<world::Box> walls, std::vector<EntitySpawn> spawns) {
    SceneDescription s;
    s.wallBoxes = std::move(walls);
    s.spawns = std::move(spawns);
    return loadGame(s);
}
static int countEdit(const std::vector<RepairEdit>& e, const char* what) {
    int n = 0;
    for (const RepairEdit& x : e) if (x.what == what) ++n;
    return n;
}

int main() {
    // 1. An already-solvable level is returned untouched.
    {
        const DungeonGame g = makeGame({}, {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 4, 0)});
        const RepairResult r = repairLevel(g);
        CHECK(r.wasSolvable && r.nowSolvable);
        CHECK(r.edits.empty());
        CHECK(r.level.exitPosition.x == g.exitPosition.x); // unchanged
    }

    // 2. An unreachable exit is relocated onto a reachable cell.
    {
        const DungeonGame g = makeGame(boxAround(10, 0), {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 10, 0)});
        CHECK(!solve(g).solvable); // stranded exit
        const RepairResult r = repairLevel(g);
        CHECK(!r.wasSolvable);
        CHECK(r.nowSolvable);
        CHECK(countEdit(r.edits, "exit") == 1);
        CHECK(r.level.exitPosition.x != g.exitPosition.x); // moved
        CHECK(solve(r.level).solvable);
    }

    // 3. An unreachable coin is relocated so all coins become collectible.
    {
        const DungeonGame g = makeGame(boxAround(10, 0), {sp("player", 0, 0), sp("coin", 10, 0), sp("exit", 2, 0)});
        CHECK(!solve(g).solvable);
        const RepairResult r = repairLevel(g);
        CHECK(r.nowSolvable);
        CHECK(countEdit(r.edits, "coin") == 1);
        const SolveResult sr = solve(r.level);
        CHECK(sr.solvable);
        CHECK(sr.reachableCoins == sr.totalCoins);
    }

    // 4. Idempotent: repairing a repaired level is a no-op.
    {
        const DungeonGame g = makeGame(boxAround(10, 0), {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 10, 0)});
        const RepairResult r1 = repairLevel(g);
        const RepairResult r2 = repairLevel(r1.level);
        CHECK(r2.wasSolvable);
        CHECK(r2.edits.empty());
    }

    // 5. Suggest-only reports the edits but leaves the level unchanged.
    {
        const DungeonGame g = makeGame(boxAround(10, 0), {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 10, 0)});
        RepairOptions opt;
        opt.apply = false;
        const RepairResult r = repairLevel(g, opt);
        CHECK(!r.edits.empty());               // a fix is proposed
        CHECK(r.nowSolvable);                  // ...and it would work
        CHECK(r.level.exitPosition.x == g.exitPosition.x); // but nothing was applied
        CHECK(!solve(r.level).solvable);       // still unsolvable as-is
    }

    // 6. Deterministic: same input -> same edits.
    {
        const DungeonGame g = makeGame(boxAround(10, 0), {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 10, 0)});
        const RepairResult a = repairLevel(g);
        const RepairResult b = repairLevel(g);
        CHECK(a.edits.size() == b.edits.size());
        CHECK(!a.edits.empty());
        CHECK(a.edits[0].to.x == b.edits[0].to.x && a.edits[0].to.z == b.edits[0].to.z);
    }

    if (g_failures == 0) {
        std::printf("test_level_autorepair: all checks passed\n");
        return 0;
    }
    std::printf("test_level_autorepair: %d check(s) failed\n", g_failures);
    return 1;
}
