// Pushable blocks (Sokoban) with solver support - issue #345.
//
// Two halves: the game mechanic (a block ahead of the player slides one grid cell when the
// cell beyond is clear, and does not move when it is walled), and the block-aware Solver
// (BFS over player + block positions) classifying solvable / unsolvable and par correctly.
// A pinch puzzle is clearable only by pushing the block down through the choke twice; the
// same puzzle with no escape below the choke is unsolvable. Classic (block-free) levels are
// unaffected. Pure std + the header-only game / solver:
//   g++ -std=c++17 -I src tests/test_pushable_blocks.cpp -o test_pushable_blocks

#include "game/DungeonGame.h"
#include "game/Solver.h"

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

// World units per cell (== blockGrid). Wide enough that the exit pickup radius covers only
// its own cell, so the solver's par is an exact cell count.
static const float S = 2.0f;

// Build a DungeonGame from an ASCII grid: '#' wall, '.' floor, 'P' player, 'B' block,
// 'E' exit, 'C' coin. Cell (x,y) maps to world (x*S, 0, y*S); walls are S-sized boxes.
static DungeonGame buildGame(const std::vector<std::string>& rows) {
    SceneDescription scene;
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
        for (int x = 0; x < static_cast<int>(rows[y].size()); ++x) {
            const char ch = rows[y][x];
            const ecs::Vec3 p{x * S, 0.0f, y * S};
            if (ch == '#') {
                world::Box b;
                b.center = ecs::Vec3{p.x, S * 0.5f, p.z};
                b.size = ecs::Vec3{S, S, S};
                b.yaw = 0.0f;
                scene.wallBoxes.push_back(b);
            } else if (ch == 'P') {
                scene.spawns.push_back(EntitySpawn{"player", p, 0.0f});
            } else if (ch == 'B') {
                scene.spawns.push_back(EntitySpawn{"block", p, 0.0f});
            } else if (ch == 'E') {
                scene.spawns.push_back(EntitySpawn{"exit", p, 0.0f});
            } else if (ch == 'C') {
                scene.spawns.push_back(EntitySpawn{"coin", p, 0.0f});
            }
        }
    }
    DungeonGame g = loadGame(scene);
    g.blockGrid = S;
    return g;
}

static void hold(DungeonGame& g, float mx, float mz, int frames) {
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < frames && g.status == GameStatus::Playing; ++i)
        g.update(GameInput{mx, mz}, dt);
}

static bool drivePath(DungeonGame& g, const std::vector<ecs::Vec3>& path) {
    const float dt = 1.0f / 60.0f;
    for (const ecs::Vec3& wp : path) {
        for (int i = 0; i < 4000 && g.status == GameStatus::Playing; ++i) {
            const float dx = wp.x - g.playerPosition.x, dz = wp.z - g.playerPosition.z;
            if (dx * dx + dz * dz < 0.0025f) break;
            g.update(GameInput{dx, dz}, dt);
        }
    }
    return g.won();
}

int main() {
    // 1. Mechanic: the player pushes a block along a clear corridor and stays behind it.
    {
        DungeonGame g = buildGame({"########",
                                   "#P.B...#",
                                   "########"});
        CHECK(g.blocks.size() == 1);
        const float x0 = g.blocks[0].position.x;
        hold(g, 1.0f, 0.0f, 400);
        CHECK(g.blocks[0].position.x > x0 + S - 0.01f);   // pushed at least one cell
        CHECK(g.playerPosition.x < g.blocks[0].position.x); // player stays behind the block
    }

    // 2. Mechanic: a block against a wall cannot be pushed, and blocks the player.
    {
        DungeonGame g = buildGame({"######",
                                   "#PB#.#",
                                   "######"});
        const float bx0 = g.blocks[0].position.x;
        hold(g, 1.0f, 0.0f, 400);
        CHECK(g.blocks[0].position.x == bx0);              // wall behind it: no move
        CHECK(g.playerPosition.x < g.blocks[0].position.x); // player cannot pass through it
    }

    // 3. Solver: a pinch puzzle solvable only by pushing the block down through the choke.
    //    Push (down, down), then walk around to the exit: par = 4 player steps.
    {
        DungeonGame g = buildGame({"#####",
                                   "#.P.#",
                                   "##B##",
                                   "#...#",
                                   "#E..#",
                                   "#####"});
        const SolveResult r = solve(g);
        CHECK(r.solvable);
        CHECK(r.exitReachable);
        CHECK(r.par == 4);
        CHECK(r.path.size() == static_cast<std::size_t>(r.par) + 1);
        // Deterministic: same result on a second run.
        const SolveResult r2 = solve(g);
        CHECK(r2.solvable && r2.par == r.par);
    }

    // 4. Solver: the same choke with no room below is unsolvable - the only push seals the
    //    single passage into the exit room.
    {
        DungeonGame g = buildGame({"#####",
                                   "#.P.#",
                                   "##B##",
                                   "#E..#",
                                   "#####"});
        const SolveResult r = solve(g);
        CHECK(!r.solvable);
        CHECK(r.par == -1);
        CHECK(!r.exitReachable);
    }

    // 5. Classic (no blocks) is unaffected: the block-free solver path still classifies and
    //    the game still drives to a win along the returned route.
    {
        DungeonGame g = buildGame({"######",
                                   "#P.CE#",
                                   "######"});
        CHECK(g.blocks.empty());
        const SolveResult r = solve(g);
        CHECK(r.solvable);
        CHECK(r.totalCoins == 1);
        CHECK(r.reachableCoins == 1);
        CHECK(r.exitReachable);
        CHECK(r.par > 0);
        DungeonGame play = g;
        CHECK(drivePath(play, r.path));
    }

    if (g_failures == 0) {
        std::printf("test_pushable_blocks: all checks passed\n");
        return 0;
    }
    std::printf("test_pushable_blocks: %d check(s) failed\n", g_failures);
    return 1;
}
