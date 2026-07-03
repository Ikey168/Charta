// Test for the weekly challenge rotation + per-week leaderboard - issue #273.
//
// Verifies the acceptance criteria headlessly: the same inputs (catalog + timestamps)
// produce the same weekly selection and standings; submissions are replay-verified
// exactly as in #242 so rejected runs never appear; and week rollover starts a fresh
// board while prior weeks stay queryable.
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_weekly_challenge.cpp -o test_weekly_challenge

#include "game/WeeklyChallenge.h"
#include "game/LevelFormat.h" // toLevelJson

#include <cmath>
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

// A clearable room with the coin/exit at caller-chosen spots (so each level's JSON,
// and thus its content code, differs).
static std::string makeLevelJson(float coinX, float exitX) {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {50, 0, 0}, {50, 0, 50}, {0, 0, 50}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{coinX, 0, 25}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{exitX, 0, 44}, 0.0f});
    return toLevelJson(s);
}

static RunTrace playGreedy(const std::string& json, double dt, int maxSteps) {
    RunTrace tr;
    tr.dt = dt;
    LevelSpec spec;
    fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    for (int step = 0; step < maxSteps && g.status == GameStatus::Playing; ++step) {
        ecs::Vec3 target = g.exitPosition;
        double best = 1e30;
        for (const Coin& c : g.coins) {
            if (c.collected) continue;
            const double d = std::hypot(c.position.x - g.playerPosition.x, c.position.z - g.playerPosition.z);
            if (d < best) { best = d; target = c.position; }
        }
        const GameInput in{target.x - g.playerPosition.x, target.z - g.playerPosition.z};
        tr.inputs.push_back(in);
        g.update(in, static_cast<float>(dt));
    }
    return tr;
}

// Publish three distinct clearable levels with distinct star counts (so the featured
// ranking is total-ordered and rotation is observable).
static LevelCatalog makeCatalog() {
    LevelCatalog cat;
    const std::string a = cat.publish("ann", "Aaa", makeLevelJson(20, 44), /*publishedAt=*/100);
    const std::string b = cat.publish("bob", "Bbb", makeLevelJson(25, 40), /*publishedAt=*/200);
    const std::string c = cat.publish("cid", "Ccc", makeLevelJson(30, 36), /*publishedAt=*/300);
    for (int i = 0; i < 3; ++i) cat.star(a);
    for (int i = 0; i < 2; ++i) cat.star(b);
    cat.star(c);
    return cat;
}

// Week index is a deterministic floor of caller time; bounds and remaining follow.
static void testWeekIndexAndTiming() {
    WeeklyChallenge wc(100, 0, 1.0 / 60.0);
    CHECK(wc.weekIndexAt(0) == 0);
    CHECK(wc.weekIndexAt(99) == 0);
    CHECK(wc.weekIndexAt(100) == 1);
    CHECK(wc.weekIndexAt(250) == 2);
    CHECK(wc.weekIndexAt(-1) == -1);   // floored toward negative
    CHECK(wc.weekIndexAt(-100) == -1);
    CHECK(wc.weekIndexAt(-101) == -2);
    CHECK(wc.weekStart(2) == 200);
    CHECK(wc.weekEnd(2) == 300);
    CHECK(wc.timeRemaining(250) == 50); // weekEnd(2)=300 - 250
}

// The same catalog + week yields the same featured level and prompt on any instance.
static void testDeterministicSelection() {
    const LevelCatalog cat = makeCatalog();
    WeeklyChallenge wc1(100, 0), wc2(100, 0);

    const WeeklyChallengeInfo a = wc1.current(cat, 50);   // week 0
    const WeeklyChallengeInfo b = wc2.current(cat, 50);   // week 0, separate instance
    CHECK(a.valid && b.valid);
    CHECK(a.weekIndex == 0 && b.weekIndex == 0);
    CHECK(a.level.code == b.level.code);
    CHECK(a.prompt == b.prompt);
    CHECK(a.prompt == LevelCatalog::weeklyPrompt(0));
    // Highest-starred level (ann's, 3 stars) is featured in week 0.
    CHECK(a.level.author == "ann");

    // Rotation: a later week features a different level (weekIndex mod count).
    const WeeklyChallengeInfo w1 = wc1.current(cat, 150); // week 1
    CHECK(w1.level.code != a.level.code);
    CHECK(w1.prompt == LevelCatalog::weeklyPrompt(1));

    // An empty catalog has no featured level.
    LevelCatalog empty;
    CHECK(!wc1.current(empty, 50).valid);
}

// Submissions are replay-verified as in #242: winners rank, rejects never appear.
static void testSubmitReplayVerified() {
    const LevelCatalog cat = makeCatalog();
    WeeklyChallenge wc(100, 0, 1.0 / 60.0);

    const WeeklyChallengeInfo info = wc.current(cat, 10); // week 0
    CHECK(info.valid);
    const RunTrace win = playGreedy(info.level.levelJson, 1.0 / 60.0, 100000);

    const SubmitResult ok = wc.submit(cat, 10, "alice", win, /*submittedAt=*/1);
    CHECK(ok.accepted && ok.improved);
    CHECK(wc.currentStandings(10).size() == 1);
    CHECK(wc.currentStandings(10)[0].player == "alice");

    // An idle trace does not clear -> rejected, standings unchanged.
    RunTrace idle;
    idle.dt = 1.0 / 60.0;
    for (int i = 0; i < 100; ++i) idle.inputs.push_back(GameInput{0.0f, 0.0f});
    const SubmitResult bad = wc.submit(cat, 10, "mallory", idle, 2);
    CHECK(!bad.accepted);
    CHECK(wc.currentStandings(10).size() == 1); // mallory never appears

    // Wrong timestep -> rejected by the reused Leaderboard verification.
    RunTrace wrongDt = win;
    wrongDt.dt = 1.0 / 30.0;
    const SubmitResult bad2 = wc.submit(cat, 10, "carol", wrongDt, 3);
    CHECK(!bad2.accepted);
    CHECK(wc.currentStandings(10).size() == 1);
}

// Week rollover: a new week is a fresh board; prior weeks remain queryable.
static void testWeekRolloverIsolation() {
    const LevelCatalog cat = makeCatalog();
    WeeklyChallenge wc(100, 0, 1.0 / 60.0);

    // Week 0 submission.
    const WeeklyChallengeInfo w0 = wc.current(cat, 10);
    const RunTrace win0 = playGreedy(w0.level.levelJson, 1.0 / 60.0, 100000);
    CHECK(wc.submit(cat, 10, "alice", win0, 1).accepted);

    // Week 1 submission (different featured level).
    const WeeklyChallengeInfo w1 = wc.current(cat, 110);
    CHECK(w1.weekIndex == 1);
    CHECK(w1.level.code != w0.level.code);
    const RunTrace win1 = playGreedy(w1.level.levelJson, 1.0 / 60.0, 100000);
    CHECK(wc.submit(cat, 110, "bob", win1, 2).accepted);

    // Each board is independent, and the earlier week is still queryable after rollover.
    CHECK(wc.standings(0).size() == 1);
    CHECK(wc.standings(0)[0].player == "alice");
    CHECK(wc.standings(1).size() == 1);
    CHECK(wc.standings(1)[0].player == "bob");
    CHECK(wc.hasBoard(0) && wc.hasBoard(1));
    CHECK(!wc.hasBoard(2));                 // untouched week has no board
    CHECK(wc.standings(2).empty());
}

int main() {
    testWeekIndexAndTiming();
    testDeterministicSelection();
    testSubmitReplayVerified();
    testWeekRolloverIsolation();

    if (g_failures == 0) {
        std::printf("All weekly challenge tests passed.\n");
        return 0;
    }
    std::printf("%d weekly challenge test(s) failed.\n", g_failures);
    return 1;
}
