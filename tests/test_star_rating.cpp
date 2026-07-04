// Par-based star ratings - issue #346.
//
// The Solver reports optimal par (#316); this awards 1-3 stars from a run's clear time
// versus par and surfaces them on the leaderboard (#242) and the weekly challenge (#273).
// Checks: the pure star function maps time/par to 3 -> 2 -> 1 with tunable thresholds and
// is deterministic; a verified leaderboard submission carries stars (when par is known via
// enforced fairness) and a faster run never scores fewer stars than a slower one; and a
// weekly submission is star-rated when the challenge opts into fairness. Pure std +
// header-only:
//   g++ -std=c++17 -I src tests/test_star_rating.cpp -o test_star_rating

#include "doodle/Doodle.h"
#include "game/StarRating.h"
#include "game/WeeklyChallenge.h"

#include <cmath>
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

static std::string makeLevelJson() {
    doodle::LevelSpec s;
    s.walls.push_back(doodle::Wall{{{10, 0, 10}, {40, 0, 10}, {40, 0, 40}, {10, 0, 40}, {10, 0, 10}}});
    s.symbols.push_back(doodle::Symbol{"player", doodle::Vec3{15, 0, 15}, 0.0f});
    s.symbols.push_back(doodle::Symbol{"coin", doodle::Vec3{25, 0, 25}, 0.0f});
    s.symbols.push_back(doodle::Symbol{"exit", doodle::Vec3{34, 0, 34}, 0.0f});
    return doodle::saveLevel(s);
}

// A greedy client toward the nearest uncollected coin then the exit (a real clearing run).
static RunTrace playGreedy(const std::string& json, double dt, int maxSteps) {
    RunTrace tr;
    tr.dt = dt;
    LevelSpec spec;
    doodle::loadLevel(json, spec);
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

static RunTrace padded(const RunTrace& base, int idleFrames) {
    RunTrace tr;
    tr.dt = base.dt;
    for (int i = 0; i < idleFrames; ++i) tr.inputs.push_back(GameInput{0.0f, 0.0f});
    for (const GameInput& in : base.inputs) tr.inputs.push_back(in);
    return tr;
}

int main() {
    // 1. Pure star function: par-time -> 3 stars, progressively slower -> 2 then 1.
    {
        const int par = 100;
        const double pt = parTime(par);          // 100 * 0.5 / 4.0
        CHECK(std::fabs(pt - 12.5) < 1e-9);
        CHECK(starsForClear(pt, par) == 3);       // exactly par -> 3
        CHECK(starsForClear(pt * 1.20, par) == 3); // at the 3-star threshold
        CHECK(starsForClear(pt * 1.30, par) == 2);
        CHECK(starsForClear(pt * 2.00, par) == 2); // at the 2-star threshold
        CHECK(starsForClear(pt * 2.50, par) == 1);
        CHECK(starsForClear(pt * 8.00, par) == 1);
        // Monotonic: never more stars for a slower time.
        CHECK(starsForClear(pt, par) >= starsForClear(pt * 1.5, par));
        CHECK(starsForClear(pt * 1.5, par) >= starsForClear(pt * 3.0, par));
        // Undefined inputs -> 0 stars.
        CHECK(starsForClear(pt, 0) == 0);
        CHECK(starsForClear(0.0, par) == 0);
        CHECK(parTime(0) == 0.0);
        // Tunable thresholds.
        StarConfig strict;
        strict.three = 1.05;
        CHECK(starsForClear(pt * 1.10, par, strict) == 2); // now outside 3-star band
    }

    // 2. Leaderboard: a verified run is star-rated against the recorded par, and a slower
    //    run never earns more stars than a faster one.
    {
        Leaderboard lb(1.0 / 60.0, /*enforceFairness=*/true);
        const std::string json = makeLevelJson();
        const std::string code = lb.registerLevel(json);
        CHECK(!code.empty());
        CHECK(lb.parFor(code) > 0);

        const RunTrace fast = playGreedy(json, lb.fixedDt(), 5000);
        const RunTrace slow = padded(fast, 120);

        const SubmitResult a = lb.submit(code, "alice", fast, 1);
        const SubmitResult b = lb.submit(code, "bob", slow, 2);
        CHECK(a.accepted && b.accepted);
        CHECK(a.stars >= 1 && a.stars <= 3);
        CHECK(b.stars >= 1 && b.stars <= 3);
        // Stars are exactly what the star function gives for the verified time + par.
        CHECK(a.stars == starsForClear(a.clearTime, lb.parFor(code)));
        CHECK(b.stars == starsForClear(b.clearTime, lb.parFor(code)));
        CHECK(b.stars <= a.stars); // slower run: no more stars
        // The stored best entry carries the run's stars.
        const std::vector<ScoreEntry> board = lb.top(code);
        CHECK(!board.empty());
        CHECK(board[0].player == "alice");
        CHECK(board[0].stars == a.stars);
    }

    // 3. Without enforced fairness the par is unknown, so stars stay 0 (rankings unchanged).
    {
        Leaderboard lb(1.0 / 60.0); // fairness off
        const std::string json = makeLevelJson();
        const std::string code = lb.registerLevel(json);
        const RunTrace fast = playGreedy(json, lb.fixedDt(), 5000);
        const SubmitResult r = lb.submit(code, "alice", fast, 1);
        CHECK(r.accepted);
        CHECK(r.stars == 0);
        CHECK(lb.parFor(code) == -1);
    }

    // 4. Weekly challenge: opt-in fairness makes submissions star-rated; default does not.
    {
        LevelCatalog cat;
        cat.publish("ann", "Aaa", makeLevelJson(), /*publishedAt=*/100);

        WeeklyChallenge rated(kSecondsPerWeek, 0, 1.0 / 60.0, /*enforceFairness=*/true);
        const WeeklyChallengeInfo info = rated.current(cat, 10);
        CHECK(info.valid);
        const RunTrace win = playGreedy(info.level.levelJson, rated.fixedDt(), 5000);
        const SubmitResult r = rated.submit(cat, 10, "alice", win, 1);
        CHECK(r.accepted);
        CHECK(r.stars >= 1 && r.stars <= 3);

        WeeklyChallenge plain(kSecondsPerWeek, 0, 1.0 / 60.0); // default: no rating
        const SubmitResult r2 = plain.submit(cat, 10, "bob", win, 2);
        CHECK(r2.accepted);
        CHECK(r2.stars == 0);
    }

    if (g_failures == 0) {
        std::printf("test_star_rating: all checks passed\n");
        return 0;
    }
    std::printf("test_star_rating: %d check(s) failed\n", g_failures);
    return 1;
}
