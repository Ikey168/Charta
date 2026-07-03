// Test for the async ghost race mode - issue #292.
//
// Verifies the acceptance criteria headlessly: a live run and a decoded ghost step
// tick for tick with a deterministic lead and finish order; the race is bound to the
// level so a wrong-level or tampered ghost is rejected before it starts.
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_ghost_race.cpp -o test_ghost_race

#include "game/GhostRace.h"
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

static const float kDt = 1.0f / 60.0f;

static std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {40, 0, 0}, {40, 0, 40}, {0, 0, 40}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{6, 0, 6}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{20, 0, 20}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{34, 0, 34}, 0.0f});
    return toLevelJson(s);
}

static std::string makeOtherLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {50, 0, 0}, {50, 0, 50}, {0, 0, 50}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{25, 0, 30}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{44, 0, 44}, 0.0f});
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

// Racing the ghost's own inputs ties it exactly: same finish tick, ~zero lead throughout.
static void testIdenticalRunTies() {
    const std::string json = makeLevelJson();
    const RunTrace ghost = playGreedy(json, kDt, 100000);

    GhostRace race(json, ghost, kDt);
    CHECK(race.valid());

    std::size_t i = 0;
    float maxAbsLead = 0.0f;
    while (!race.finished() && i < ghost.inputs.size()) {
        race.step(ghost.inputs[i]); // live replays the ghost's own inputs
        maxAbsLead = std::max(maxAbsLead, std::fabs(race.lead()));
        ++i;
    }
    CHECK(race.winner() == 0);            // a dead heat
    CHECK(maxAbsLead < 1e-3f);            // never ahead or behind at any tick
    CHECK(race.liveStanding().won && race.ghostStanding().won);
    CHECK(race.liveStanding().finishTick == race.ghostStanding().finishTick);
}

// An idle live run loses: the ghost finishes, the live run never leads.
static void testIdleLiveLoses() {
    const std::string json = makeLevelJson();
    const RunTrace ghost = playGreedy(json, kDt, 100000);

    GhostRace race(json, ghost, kDt);
    CHECK(race.valid());
    int sawLiveLeadTicks = 0;
    while (!race.finished()) {
        race.step(GameInput{0.0f, 0.0f}); // live stands still
        if (race.leader() == 1) ++sawLiveLeadTicks;
    }
    CHECK(race.winner() == -1);          // the ghost wins
    CHECK(race.ghostStanding().won);
    CHECK(!race.liveStanding().won);
    CHECK(sawLiveLeadTicks == 0);        // an idle player is never ahead
}

// The race is bound to the level via the share string: correct level accepted,
// wrong-level and tampered strings rejected.
static void testShareBinding() {
    const std::string json = makeLevelJson();
    const std::string other = makeOtherLevelJson();
    const RunTrace ghost = playGreedy(json, kDt, 100000);
    const std::string share = encodeGhost(json, ghost);

    // Correct level: valid race that ties when replaying the same inputs.
    GhostRace ok = GhostRace::fromShare(json, share, kDt);
    CHECK(ok.valid());

    // Wrong level: rejected.
    GhostRace wrong = GhostRace::fromShare(other, share, kDt);
    CHECK(!wrong.valid());

    // Tampered payload: rejected.
    std::string tampered = share;
    tampered[tampered.size() - 1] = (tampered.back() == 'A') ? 'B' : 'A';
    GhostRace bad = GhostRace::fromShare(json, tampered, kDt);
    CHECK(!bad.valid());

    // A finished/invalid race is a safe no-op.
    CHECK(wrong.finished());
    CHECK(!wrong.step(GameInput{1.0f, 0.0f}));
}

int main() {
    testIdenticalRunTies();
    testIdleLiveLoses();
    testShareBinding();

    if (g_failures == 0) {
        std::printf("All ghost race tests passed.\n");
        return 0;
    }
    std::printf("%d ghost race test(s) failed.\n", g_failures);
    return 1;
}
