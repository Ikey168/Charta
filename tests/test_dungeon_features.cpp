// Test for keys, locked doors, switches, and hazards - issue #319.
//
// Verifies each new rule on fixture corridors: a locked door blocks until its key is
// collected; a solid toggle wall blocks until its switch is pressed; a hazard is a loss on
// contact; and a level with none of the new types plays exactly as the classic game. Pure
// std + the header-only game:
//   g++ -std=c++17 -I src tests/test_dungeon_features.cpp -o test_dungeon_features

#include "game/DungeonGame.h"

#include <cstdio>
#include <string>
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

// Two long walls forming a 1-wide corridor along +X at z=0, x in [-2, 8].
static std::vector<world::Box> corridor() {
    world::Box top, bot;
    top.center = ecs::Vec3{3.0f, 0.0f, 1.0f};
    top.size = ecs::Vec3{10.0f, 1.0f, 0.4f};
    bot.center = ecs::Vec3{3.0f, 0.0f, -1.0f};
    bot.size = ecs::Vec3{10.0f, 1.0f, 0.4f};
    return {top, bot};
}

static void drive(DungeonGame& g, float mx, float mz, int steps) {
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < steps && g.status == GameStatus::Playing; ++i) g.update(GameInput{mx, mz}, dt);
}

int main() {
    // --- Locked door blocks without its key --------------------------------------
    {
        SceneDescription s;
        s.wallBoxes = corridor();
        s.spawns = {sp("player", 0, 0), sp("exit", 6, 0), sp("lock@1", 3, 0)};
        DungeonGame g = loadGame(s);
        CHECK(g.lockedDoors.size() == 1 && !g.lockedDoors[0].open);
        CHECK(g.blocked(ecs::Vec3{3, 0, 0}, 0.4f));   // the door blocks
        CHECK(!g.hitsWall(ecs::Vec3{3, 0, 0}, 0.4f)); // but it is not a static wall
        drive(g, 1.0f, 0.0f, 600);
        CHECK(!g.won());                 // cannot pass the locked door
        CHECK(g.playerPosition.x < 2.5f); // stuck in front of it
    }

    // --- Key opens its door, level clears ----------------------------------------
    {
        SceneDescription s;
        s.wallBoxes = corridor();
        s.spawns = {sp("player", 0, 0), sp("exit", 6, 0), sp("lock@1", 3, 0), sp("key@1", 1, 0)};
        DungeonGame g = loadGame(s);
        drive(g, 1.0f, 0.0f, 600);
        CHECK(g.keys.size() == 1 && g.keys[0].collected); // grabbed en route
        CHECK(g.lockedDoors[0].open);                      // which opened the door
        CHECK(g.won());
    }

    // --- Non-matching key does not open the door ---------------------------------
    {
        SceneDescription s;
        s.wallBoxes = corridor();
        s.spawns = {sp("player", 0, 0), sp("exit", 6, 0), sp("lock@1", 3, 0), sp("key@2", 1, 0)};
        DungeonGame g = loadGame(s);
        drive(g, 1.0f, 0.0f, 600);
        CHECK(g.keys[0].collected);       // key@2 is picked up
        CHECK(!g.lockedDoors[0].open);    // but it does not open door@1
        CHECK(!g.won());
    }

    // --- Solid toggle wall blocks; its switch opens it ---------------------------
    {
        SceneDescription blockOnly;
        blockOnly.wallBoxes = corridor();
        blockOnly.spawns = {sp("player", 0, 0), sp("exit", 6, 0), sp("toggle@1", 3, 0)};
        DungeonGame gb = loadGame(blockOnly);
        CHECK(gb.toggleWalls.size() == 1 && gb.toggleWalls[0].solid);
        drive(gb, 1.0f, 0.0f, 600);
        CHECK(!gb.won());

        SceneDescription withSwitch;
        withSwitch.wallBoxes = corridor();
        withSwitch.spawns = {sp("player", 0, 0), sp("exit", 6, 0), sp("toggle@1", 3, 0), sp("switch@1", 1, 0)};
        DungeonGame gs = loadGame(withSwitch);
        drive(gs, 1.0f, 0.0f, 600);
        CHECK(!gs.toggleWalls[0].solid); // the switch toggled it open (once, on the rising edge)
        CHECK(gs.won());
    }

    // --- Hazard contact is a loss ------------------------------------------------
    {
        SceneDescription s;
        s.spawns = {sp("player", 0, 0), sp("exit", 6, 0), sp("hazard", 3, 0)};
        DungeonGame g = loadGame(s);
        CHECK(g.hazards.size() == 1);
        drive(g, 1.0f, 0.0f, 600);
        CHECK(g.lost());
        CHECK(!g.won());
    }

    // --- Byte-identical: a classic level with no new types plays as before -------
    {
        SceneDescription s;
        s.spawns = {sp("player", 0, 0), sp("coin", 2, 0), sp("exit", 4, 0)};
        DungeonGame g = loadGame(s);
        CHECK(g.keys.empty() && g.lockedDoors.empty() && g.switches.empty());
        CHECK(g.toggleWalls.empty() && g.hazards.empty());
        CHECK(g.blocked(ecs::Vec3{0, 0, 0}, 0.4f) == g.hitsWall(ecs::Vec3{0, 0, 0}, 0.4f));
        drive(g, 1.0f, 0.0f, 600);
        CHECK(g.coinsCollected == 1);
        CHECK(g.won());
    }

    if (g_failures == 0) {
        std::printf("test_dungeon_features: all checks passed\n");
        return 0;
    }
    std::printf("test_dungeon_features: %d check(s) failed\n", g_failures);
    return 1;
}
