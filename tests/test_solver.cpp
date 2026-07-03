// Test for the deterministic solvability validator + auto-solver - issue #316.
//
// Verifies: a solvable level is proven clearable with the hand-verified optimal par;
// the returned auto-solver route, driven as real inputs, actually wins the DungeonGame
// (solver and game agree); an unsolvable level (a coin trapped in a wall) is rejected
// with the right unreachable-objective report; and the JSON gating entry point works.
// Pure std + the header-only game:
//   g++ -std=c++17 -I src tests/test_solver.cpp -o test_solver

#include "game/Solver.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

// Drive a copy of the game along the solver's route as real inputs; return whether
// it wins (proving the abstract route is a genuine, rule-legal clear).
static bool drivesToWin(game::DungeonGame g, const std::vector<ecs::Vec3>& path) {
    const float dt = 1.0f / 60.0f;
    for (const ecs::Vec3& target : path) {
        int steps = 0;
        while (steps < 4000 && g.status == game::GameStatus::Playing) {
            const float dx = target.x - g.playerPosition.x;
            const float dz = target.z - g.playerPosition.z;
            if (dx * dx + dz * dz < 0.0025f) break; // within 0.05
            g.update(game::GameInput{dx, dz}, dt);
            ++steps;
        }
        if (g.status != game::GameStatus::Playing) break;
    }
    return g.won();
}

static game::EntitySpawn spawn(const char* t, float x, float z) {
    return game::EntitySpawn{t, ecs::Vec3{x, 0.0f, z}, 0.0f};
}

int main() {
    // --- Fixture 1: open row, hand-verifiable par ------------------------------
    // cellSize 1.0. Player at x=0.5, exit at x=5.5, same row, no coins/walls. With
    // padCells=2 the start cell is gx=2 and the nearest exit cell is gx=6 -> par 4.
    {
        game::SceneDescription scene;
        scene.spawns.push_back(spawn("player", 0.5f, 0.5f));
        scene.spawns.push_back(spawn("exit", 5.5f, 0.5f));
        game::SolveOptions opt;
        opt.cellSize = 1.0f;
        const game::SolveResult r = game::solve(game::loadGame(scene), opt);
        CHECK(r.solvable);
        CHECK(r.exitReachable);
        CHECK(r.totalCoins == 0);
        CHECK(r.par == 4);
        CHECK(r.path.size() == 5); // par + 1 cells
        CHECK(drivesToWin(game::loadGame(scene), r.path));
    }

    // --- Fixture 2: coins + a wall, solver route must actually win --------------
    {
        game::SceneDescription scene;
        scene.spawns.push_back(spawn("player", 0.0f, 0.0f));
        scene.spawns.push_back(spawn("coin", 3.0f, 0.0f));
        scene.spawns.push_back(spawn("coin", 3.0f, 4.0f));
        scene.spawns.push_back(spawn("exit", 0.0f, 4.0f));
        // A wall segment between the two coins so the route is not a straight line.
        world::Box wall;
        wall.center = ecs::Vec3{1.5f, 1.5f, 2.0f};
        wall.size = ecs::Vec3{3.0f, 3.0f, 0.4f};
        wall.yaw = 0.0f;
        scene.wallBoxes.push_back(wall);

        const game::SolveResult r = game::solve(game::loadGame(scene));
        CHECK(r.solvable);
        CHECK(r.totalCoins == 2);
        CHECK(r.reachableCoins == 2);
        CHECK(r.exitReachable);
        CHECK(r.par > 0);
        CHECK(!r.path.empty());
        CHECK(drivesToWin(game::loadGame(scene), r.path)); // route collects both coins and exits
    }

    // --- Fixture 3: unsolvable, a coin trapped inside a wall -------------------
    {
        game::SceneDescription scene;
        scene.spawns.push_back(spawn("player", 0.0f, 0.0f));
        scene.spawns.push_back(spawn("exit", 2.0f, 0.0f));
        scene.spawns.push_back(spawn("coin", 8.0f, 8.0f)); // sealed inside the wall below
        world::Box block;
        block.center = ecs::Vec3{8.0f, 1.5f, 8.0f};
        block.size = ecs::Vec3{3.0f, 3.0f, 3.0f}; // fully covers the coin's pickup radius
        block.yaw = 0.0f;
        scene.wallBoxes.push_back(block);

        const game::SolveResult r = game::solve(game::loadGame(scene));
        CHECK(!r.solvable);
        CHECK(r.par == -1);
        CHECK(r.totalCoins == 1);
        CHECK(r.reachableCoins == 0);   // the trapped coin is collectible from nowhere
        CHECK(r.exitReachable);         // the exit itself is fine
        CHECK(r.unreachableObjectives == 1);
        CHECK(r.path.empty());
    }

    // --- JSON gating entry point ----------------------------------------------
    {
        game::LevelSpec spec;
        game::Wall w;
        w.polyline = {ecs::Vec3{-1.0f, 0.0f, -1.0f}, ecs::Vec3{6.0f, 0.0f, -1.0f}};
        spec.walls.push_back(w);
        spec.symbols.push_back(game::Symbol{"player", ecs::Vec3{0.5f, 0.0f, 0.5f}, 0.0f});
        spec.symbols.push_back(game::Symbol{"exit", ecs::Vec3{5.5f, 0.0f, 0.5f}, 0.0f});
        const std::string json = game::toLevelJson(spec);
        const game::SolveResult r = game::validateLevelJson(json);
        CHECK(r.solvable);
        CHECK(r.exitReachable);
    }

    if (g_failures == 0) {
        std::printf("test_solver: all checks passed\n");
        return 0;
    }
    std::printf("test_solver: %d check(s) failed\n", g_failures);
    return 1;
}
