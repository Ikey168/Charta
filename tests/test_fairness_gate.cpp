// Test for the solver-backed fairness gate - issue #334.
//
// Verifies checkLevelFairness classifies clearable vs unfair levels (unreachable exit or
// coin) with par; that a fairness-enforcing Leaderboard refuses an unfair level, records
// par for a fair one, and still accepts a legit run, while a default Leaderboard is
// unchanged; and that WeeklyChallenge exposes the featured level's fairness. Pure std +
// the header-only game:
//   g++ -std=c++17 -I src tests/test_fairness_gate.cpp -o test_fairness_gate

#include "game/Fairness.h"
#include "game/Leaderboard.h"
#include "game/LevelCatalog.h"
#include "game/LevelFormat.h" // toLevelJson
#include "game/WeeklyChallenge.h"

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

static const float kDt = 1.0f / 60.0f;

// Open level, clearable by driving east: player -> coin -> exit.
static std::string solvableJson() {
    LevelSpec s;
    s.symbols.push_back(Symbol{"player", ecs::Vec3{0.5f, 0.0f, 0.5f}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{2.0f, 0.0f, 0.5f}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{4.0f, 0.0f, 0.5f}, 0.0f});
    return toLevelJson(s);
}
// A coin sealed inside a thick-walled square: reachable exit, unreachable coin.
static std::string trappedCoinJson() {
    LevelSpec s;
    s.wallThickness = 0.6f;
    s.walls.push_back(Wall{{{8, 0, 8}, {12, 0, 8}, {12, 0, 12}, {8, 0, 12}, {8, 0, 8}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{0.5f, 0.0f, 0.5f}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{10.0f, 0.0f, 10.0f}, 0.0f}); // inside the square
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{3.0f, 0.0f, 0.5f}, 0.0f});
    return toLevelJson(s);
}

static std::vector<GameInput> winTrace(const std::string& json) {
    LevelSpec spec;
    fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    std::vector<GameInput> ins;
    for (int i = 0; i < 600 && g.status == GameStatus::Playing; ++i) {
        const GameInput in{1.0f, 0.0f};
        g.update(in, kDt);
        ins.push_back(in);
    }
    return ins;
}

int main() {
    const std::string good = solvableJson();
    const std::string trapped = trappedCoinJson();

    // --- checkLevelFairness classification ------------------------------------
    const FairnessResult fg = checkLevelFairness(good);
    CHECK(fg.fair);
    CHECK(fg.par > 0);
    CHECK(fg.unreachableObjectives == 0);

    const FairnessResult ft = checkLevelFairness(trapped);
    CHECK(!ft.fair);
    CHECK(ft.exitReachable);            // the exit itself is fine
    CHECK(ft.reachableCoins == 0);      // the sealed coin is collectible from nowhere
    CHECK(ft.unreachableObjectives >= 1);
    CHECK(!ft.reason.empty());

    // --- Leaderboard with fairness enforced -----------------------------------
    Leaderboard fair(kDt, /*enforceFairness=*/true);
    CHECK(fair.fairnessEnforced());
    CHECK(fair.registerLevel(trapped).empty());   // unfair level refused
    CHECK(!fair.lastRegisterReason().empty());
    const std::string code = fair.registerLevel(good); // fair level accepted
    CHECK(!code.empty());
    CHECK(fair.parFor(code) > 0);                  // par recorded

    // A legit run still verifies and ranks on the enforced board.
    RunTrace t;
    t.dt = kDt;
    t.inputs = winTrace(good);
    const SubmitResult sr = fair.submit(code, "alice", t, /*submittedAt=*/1);
    CHECK(sr.accepted);

    // --- Default Leaderboard is unchanged (no gate) ---------------------------
    Leaderboard lax(kDt);
    CHECK(!lax.fairnessEnforced());
    CHECK(!lax.registerLevel(trapped).empty());    // registers anything, as before

    // --- WeeklyChallenge featured fairness query ------------------------------
    {
        LevelCatalog catalog;
        catalog.publish("bob", "Fair Map", good, /*publishedAt=*/100);
        WeeklyChallenge wc(kSecondsPerWeek, /*epoch=*/0, kDt);
        const FairnessResult wf = wc.featuredFairness(catalog, /*now=*/200);
        CHECK(wf.fair);
        CHECK(wf.par > 0);
    }
    {
        LevelCatalog catalog;
        catalog.publish("bob", "Unfair Map", trapped, /*publishedAt=*/100);
        WeeklyChallenge wc(kSecondsPerWeek, /*epoch=*/0, kDt);
        const FairnessResult wf = wc.featuredFairness(catalog, /*now=*/200);
        CHECK(!wf.fair);
    }

    if (g_failures == 0) {
        std::printf("test_fairness_gate: all checks passed\n");
        return 0;
    }
    std::printf("test_fairness_gate: %d check(s) failed\n", g_failures);
    return 1;
}
