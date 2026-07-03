// Test for the headless tower-defense mode - issue #271.
//
// Covers the issue's acceptance criteria without a renderer: doorway-graph pathing
// (a drawn two-room map routes enemies entry -> goal through the gap), tower damage,
// lives/leaks, win/lose resolution, and determinism (same seed + inputs -> identical
// outcome). Pathing uses a real LevelSpec through loadTowerDefense; the combat
// mechanics use hand-built boards for exact, layout-independent checks.
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_tower_defense.cpp -o test_tower_defense

#include "game/TowerDefense.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace IKore::game;
using Vec3 = IKore::ecs::Vec3;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

// A two-room level: an outer box split by an inner wall with a one-cell doorway gap.
// gridSize 1 so world coords map straight to topology cells. Entry sits in the left
// room, goal in the right room, one tower in the left room.
static LevelSpec twoRoomLevel() {
    LevelSpec spec;
    Wall outer;
    outer.polyline = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}, {0, 0, 0}};
    spec.walls.push_back(outer);
    // Inner vertical divider at x=5 with a doorway gap at z=5 (split into two segments).
    Wall dividerTop;
    dividerTop.polyline = {{5, 0, 0}, {5, 0, 4}};
    Wall dividerBottom;
    dividerBottom.polyline = {{5, 0, 6}, {5, 0, 10}};
    spec.walls.push_back(dividerTop);
    spec.walls.push_back(dividerBottom);

    Symbol entry;
    entry.type = "entry";
    entry.position = Vec3{2, 0, 5};
    Symbol goal;
    goal.type = "goal";
    goal.position = Vec3{8, 0, 5};
    Symbol tower;
    tower.type = "tower";
    tower.position = Vec3{2, 0, 2};
    spec.symbols = {entry, goal, tower};
    return spec;
}

// Doorway-graph pathing: the board is valid, routes through two rooms, and its
// waypoints lead from the entry to the goal.
static void testDoorwayPathing() {
    TdConfig cfg;
    cfg.gridSize = 1.0f;
    cfg.waves = {TdWave{1, 0.0f, 20.0f, 2.0f, 5.0f}};
    const TowerDefense td = loadTowerDefense(twoRoomLevel(), cfg);

    CHECK(td.valid);
    CHECK(td.roomPath.size() == 2);          // left room -> right room
    CHECK(td.towers.size() == 1);            // the one placed tower
    CHECK(td.path.size() >= 2);              // at least entry + goal
    // First waypoint is the entry symbol, last is the goal symbol.
    CHECK(std::fabs(td.path.front().x - 2.0f) < 1e-3f);
    CHECK(std::fabs(td.path.back().x - 8.0f) < 1e-3f);
    // A doorway waypoint near x=5 sits between them (the gap the enemy walks through).
    bool viaDoor = false;
    for (const Vec3& w : td.path) {
        if (std::fabs(w.x - 5.0f) < 1.5f) viaDoor = true;
    }
    CHECK(viaDoor);
}

// A missing goal (or unreachable rooms) yields an invalid, unplayable board.
static void testInvalidWithoutGoal() {
    LevelSpec spec = twoRoomLevel();
    // Drop the goal symbol.
    std::vector<Symbol> onlyEntryAndTower;
    for (const Symbol& s : spec.symbols) {
        if (s.type != "goal") onlyEntryAndTower.push_back(s);
    }
    spec.symbols = onlyEntryAndTower;
    TdConfig cfg;
    cfg.gridSize = 1.0f;
    cfg.waves = {TdWave{1, 0.0f, 10.0f, 2.0f, 1.0f}};
    const TowerDefense td = loadTowerDefense(spec, cfg);
    CHECK(!td.valid);
}

// Build a straight-line board directly (no rasterization) for exact combat checks.
static TowerDefense straightBoard(float pathLen, const std::vector<TdTower>& towers,
                                  const std::vector<TdWave>& waves, int lives) {
    TowerDefense td;
    td.valid = true;
    td.path = {Vec3{0, 0, 0}, Vec3{pathLen, 0, 0}};
    td.towers = towers;
    td.config.waves = waves;
    td.lives = lives;
    td.rng = TdRng(td.config.seed);
    return td;
}

// A tower kills an enemy in range before it reaches the goal; currency and kill count
// update, no life lost, and the board is won.
static void testTowerDamageAndWin() {
    TdTower tower{Vec3{0, 0, 0}, 50.0f, 1000.0f};
    TowerDefense td = straightBoard(100.0f, {tower}, {TdWave{1, 0.0f, 20.0f, 10.0f, 5.0f}}, 5);
    for (int i = 0; i < 1000 && !td.finished(); ++i) td.update(0.1f);

    CHECK(td.won());
    CHECK(td.enemiesKilled == 1);
    CHECK(td.enemiesLeaked == 0);
    CHECK(td.lives == 5);                 // no leaks
    CHECK(std::fabs(td.currency - 5.0f) < 1e-3f);
}

// With no towers, enemies leak to the goal and drain lives; running out loses.
static void testLeaksAndLose() {
    // Two enemies, one life: the first leak already loses.
    TowerDefense td = straightBoard(10.0f, {}, {TdWave{2, 0.0f, 10.0f, 10.0f, 1.0f}}, 1);
    for (int i = 0; i < 1000 && !td.finished(); ++i) td.update(0.1f);

    CHECK(td.lost());
    CHECK(td.lives == 0);
    CHECK(td.enemiesLeaked >= 1);
    CHECK(td.enemiesKilled == 0);
}

// A single leak costs exactly one life when lives remain.
static void testLifeDecrementsOnLeak() {
    TowerDefense td = straightBoard(10.0f, {}, {TdWave{1, 0.0f, 10.0f, 10.0f, 1.0f}}, 3);
    for (int i = 0; i < 1000 && !td.finished(); ++i) td.update(0.1f);
    // One enemy leaked: lives 3 -> 2, then the wave is cleared and the board is won.
    CHECK(td.enemiesLeaked == 1);
    CHECK(td.lives == 2);
    CHECK(td.won());
}

// Same seed + inputs -> identical outcome; a different seed diverges (jitter on).
static void testDeterminism() {
    auto run = [](std::uint64_t seed) {
        TowerDefense td;
        td.valid = true;
        td.path = {Vec3{0, 0, 0}, Vec3{30, 0, 0}};
        td.towers = {TdTower{Vec3{15, 0, 0}, 8.0f, 15.0f}}; // kills some, others leak
        td.config.waves = {TdWave{8, 0.5f, 20.0f, 3.0f, 2.0f}};
        td.config.hpJitter = 0.4f;
        td.config.seed = seed;
        td.lives = 5;
        td.rng = TdRng(seed);
        for (int i = 0; i < 20000 && !td.finished(); ++i) td.update(0.05f);
        return td;
    };

    const TowerDefense a = run(12345u);
    const TowerDefense b = run(12345u); // identical seed
    CHECK(a.status == b.status);
    CHECK(a.lives == b.lives);
    CHECK(a.enemiesKilled == b.enemiesKilled);
    CHECK(a.enemiesLeaked == b.enemiesLeaked);
    CHECK(std::fabs(a.currency - b.currency) < 1e-4f);

    const TowerDefense c = run(99999u); // different seed
    // The jittered hp stream differs, so at least one aggregate outcome differs.
    const bool differs = (a.enemiesKilled != c.enemiesKilled) || (a.lives != c.lives) ||
                         (std::fabs(a.currency - c.currency) > 1e-4f);
    CHECK(differs);
}

// The RNG itself is a deterministic stream keyed by seed.
static void testRngStream() {
    TdRng r1(7), r2(7), r3(8);
    CHECK(r1.next() == r2.next());
    CHECK(TdRng(7).next() != TdRng(8).next());
    for (int i = 0; i < 100; ++i) {
        const float u = TdRng(static_cast<std::uint64_t>(i)).unit();
        CHECK(u >= 0.0f && u < 1.0f);
    }
    (void)r3;
}

int main() {
    testDoorwayPathing();
    testInvalidWithoutGoal();
    testTowerDamageAndWin();
    testLeaksAndLose();
    testLifeDecrementsOnLeak();
    testDeterminism();
    testRngStream();

    if (g_failures == 0) {
        std::printf("All tower defense tests passed.\n");
        return 0;
    }
    std::printf("%d tower defense test(s) failed.\n", g_failures);
    return 1;
}
