// Multi-room levels from a topology graph - issue #344.
//
// Builds a 3-room floor plan (two doorways in series: left <-> middle <-> right),
// derives the room/doorway graph with cv::buildTopologyFromGrid (#168), turns it into a
// multi-room DungeonGame with buildMultiRoom (#344), and proves it is one playable level
// clearable across rooms: the Solver (#316) validates cross-room clearability, and driving
// the solver's route as inputs wins the actual game (player -> coin in the middle -> exit
// in the far room, through both doorways). Pure std + the header-only CV / game:
//   g++ -std=c++17 -I src tests/test_multi_room.cpp -o test_multi_room

#include "cv/Topology.h"
#include "game/MultiRoom.h"
#include "game/Solver.h"

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

// Drive the game along the solver's cell route; returns true if it wins.
static bool drivePath(DungeonGame& g, const std::vector<ecs::Vec3>& path) {
    const float dt = 1.0f / 60.0f;
    for (const ecs::Vec3& wp : path) {
        for (int guard = 0; guard < 4000 && g.status == GameStatus::Playing; ++guard) {
            const float dx = wp.x - g.playerPosition.x;
            const float dz = wp.z - g.playerPosition.z;
            if (dx * dx + dz * dz < 0.0025f) break;
            g.update(GameInput{dx, dz}, dt);
        }
    }
    return g.won();
}

int main() {
    // A 13x5 grid: three 3-wide rooms separated by vertical walls, each with a
    // one-cell doorway at row 2. '#' wall, '.' floor, 'D' doorway (a 1-wide gap).
    //   #############
    //   #...#...#...#
    //   #...D...D...#
    //   #...#...#...#
    //   #############
    const int W = 13, H = 5;
    cv::Mask walls(W, H);
    for (int x = 0; x < W; ++x) { walls.set(x, 0, 1); walls.set(x, H - 1, 1); }
    for (int y = 0; y < H; ++y) {
        walls.set(0, y, 1);
        walls.set(W - 1, y, 1);
        walls.set(4, y, 1);  // divider between left and middle
        walls.set(8, y, 1);  // divider between middle and right
    }
    walls.set(4, 2, 0);  // doorway left <-> middle
    walls.set(8, 2, 0);  // doorway middle <-> right

    // 1. Topology: three interior rooms, two doorways, all connected in a chain.
    const cv::Topology topo = cv::buildTopologyFromGrid(walls);
    CHECK(topo.interiorRoomCount() == 3);
    CHECK(topo.doorways.size() == 2);
    CHECK(topo.reachable(0, 2)); // far rooms reachable through the middle

    // 2. Multi-room scene: walls rebuilt, doorways left open, spawns across rooms.
    const MultiRoomResult mr = buildMultiRoom(topo);
    CHECK(mr.roomCount == 3);
    CHECK(mr.doorwayCount == 2);
    CHECK(mr.coinCount == 1); // one middle room between player and exit
    CHECK(!mr.scene.wallBoxes.empty());

    int players = 0, exits = 0, coins = 0;
    for (const EntitySpawn& sp : mr.scene.spawns) {
        if (sp.type == "player") ++players;
        else if (sp.type == "exit") ++exits;
        else if (sp.type == "coin") ++coins;
    }
    CHECK(players == 1);
    CHECK(exits == 1);
    CHECK(coins == 1);

    // Player and exit must sit in different rooms (far apart along +X).
    const DungeonGame game = loadGame(mr.scene);
    CHECK(game.hasExit);
    CHECK(game.exitPosition.x - game.playerPosition.x > 4.0f);

    // 3. Solver proves cross-room clearability.
    const SolveResult res = solve(game);
    CHECK(res.solvable);
    CHECK(res.exitReachable);
    CHECK(res.totalCoins == 1);
    CHECK(res.reachableCoins == 1);
    CHECK(res.unreachableObjectives == 0);
    CHECK(res.par > 0);
    CHECK(!res.path.empty());

    // 4. The solver route, driven as inputs, actually clears the level across rooms.
    DungeonGame play = game;
    CHECK(drivePath(play, res.path));
    CHECK(play.won());

    // A disconnected room graph must not be clearable: seal the middle<->right doorway.
    {
        cv::Mask sealed = walls;
        sealed.set(8, 2, 1); // wall the second doorway shut
        const cv::Topology t2 = cv::buildTopologyFromGrid(sealed);
        const MultiRoomResult mr2 = buildMultiRoom(t2);
        const SolveResult r2 = solve(loadGame(mr2.scene));
        CHECK(!r2.solvable);          // exit now in an unreachable room
        CHECK(!r2.exitReachable);
    }

    if (g_failures == 0) {
        std::printf("test_multi_room: all checks passed\n");
        return 0;
    }
    std::printf("test_multi_room: %d check(s) failed\n", g_failures);
    return 1;
}
