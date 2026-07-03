// Test for shareable ghost replays - issue #272.
//
// Verifies the acceptance criteria headlessly: a trace round-trips byte-identical
// through the share string and re-simulates to the same result (reusing replayRun);
// the ghost's position stream matches the original run tick for tick; and tampered or
// wrong-level ghost strings are rejected. Also checks the leaderboard best-trace hook.
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_ghost_replay.cpp -o test_ghost_replay

#include "game/GhostReplay.h"
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

// A clearable room: player, one coin, an exit, no enemy (a greedy path wins).
static std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{10, 0, 10}, {40, 0, 10}, {40, 0, 40}, {10, 0, 40}, {10, 0, 10}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{15, 0, 15}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{25, 0, 25}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{34, 0, 34}, 0.0f});
    return toLevelJson(s);
}

// A different clearable level (for the wrong-level rejection test).
static std::string makeOtherLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {50, 0, 0}, {50, 0, 50}, {0, 0, 50}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{20, 0, 30}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{44, 0, 44}, 0.0f});
    return toLevelJson(s);
}

// Greedy client producing a real winning input trace.
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

static bool tracesEqual(const RunTrace& a, const RunTrace& b) {
    if (a.dt != b.dt) return false; // bit-exact by construction
    if (a.inputs.size() != b.inputs.size()) return false;
    for (std::size_t i = 0; i < a.inputs.size(); ++i) {
        if (a.inputs[i].moveX != b.inputs[i].moveX) return false;
        if (a.inputs[i].moveZ != b.inputs[i].moveZ) return false;
    }
    return true;
}

// The ghost trace round-trips byte-identical and re-simulates to the same result.
static void testRoundTripAndResimulate() {
    const std::string json = makeLevelJson();
    const RunTrace trace = playGreedy(json, 1.0 / 60.0, 100000);
    CHECK(replayRun(json, trace).won); // sanity: the trace actually clears

    const std::string share = encodeGhost(json, trace);
    const GhostImport imp = decodeGhost(share, json);
    CHECK(imp.ok);
    CHECK(tracesEqual(imp.trace, trace)); // byte-identical round trip
    CHECK(imp.levelCode == shareCodeFor(json));

    const RunResult a = replayRun(json, trace);
    const RunResult b = replayRun(json, imp.trace);
    CHECK(a.won && b.won);
    CHECK(a.steps == b.steps);
    CHECK(std::fabs(a.clearTime - b.clearTime) < 1e-9);
}

// The ghost's position stream matches the original run tick for tick.
static void testGhostMatchesOriginalTickForTick() {
    const std::string json = makeLevelJson();
    const RunTrace trace = playGreedy(json, 1.0 / 60.0, 100000);

    // Original run position after each applied input.
    std::vector<ecs::Vec3> original;
    {
        LevelSpec spec;
        fromLevelJson(json, spec);
        DungeonGame g = loadGame(convert(spec));
        for (const GameInput& in : trace.inputs) {
            g.update(in, static_cast<float>(trace.dt));
            original.push_back(g.playerPosition);
            if (g.status != GameStatus::Playing) break;
        }
    }

    // Ghost via the share string, stepped one tick at a time.
    const GhostImport imp = decodeGhost(encodeGhost(json, trace), json);
    CHECK(imp.ok);
    GhostPlayback ghost(json, imp.trace);
    CHECK(ghost.valid());

    std::size_t t = 0;
    while (ghost.step()) {
        CHECK(t < original.size());
        if (t < original.size()) {
            CHECK(ghost.position().x == original[t].x); // deterministic: exact match
            CHECK(ghost.position().z == original[t].z);
        }
        ++t;
    }
    CHECK(t == original.size()); // same number of played ticks
}

// Tampered payload, wrong level, and bad prefix are all rejected.
static void testRejections() {
    const std::string json = makeLevelJson();
    const std::string other = makeOtherLevelJson();
    const RunTrace trace = playGreedy(json, 1.0 / 60.0, 100000);
    const std::string share = encodeGhost(json, trace);

    // Correct decode works.
    CHECK(decodeGhost(share, json).ok);

    // Wrong level: same share, different level JSON -> rejected.
    CHECK(!decodeGhost(share, other).ok);

    // Tamper: flip a character in the base64 payload (last field) -> integrity fails.
    std::string tampered = share;
    tampered[tampered.size() - 1] = (tampered.back() == 'A') ? 'B' : 'A';
    CHECK(!decodeGhost(tampered, json).ok);

    // Bad prefix -> rejected.
    CHECK(!decodeGhost("NOPE:" + share, json).ok);
    CHECK(!decodeGhost("", json).ok);

    // Peek exposes the bound level code without verifying.
    CHECK(peekGhostLevelCode(share) == shareCodeFor(json));
    CHECK(peekGhostLevelCode("garbage").empty());
}

// The leaderboard stores the winning trace so a rank's ghost can be fetched and shared.
static void testLeaderboardGhostHook() {
    const std::string json = makeLevelJson();
    const RunTrace trace = playGreedy(json, 1.0 / 60.0, 100000);

    Leaderboard lb(1.0 / 60.0);
    const std::string code = lb.registerLevel(json);
    const SubmitResult sr = lb.submit(code, "alice", trace, /*submittedAt=*/1);
    CHECK(sr.accepted && sr.improved);

    RunTrace stored;
    CHECK(lb.bestTrace(code, "alice", stored));
    CHECK(tracesEqual(stored, trace));

    // Fetch the rank-1 ghost and confirm it is alice's trace.
    RunTrace rankTrace;
    std::string who;
    CHECK(lb.ghostForRank(code, 1, rankTrace, who));
    CHECK(who == "alice");
    CHECK(tracesEqual(rankTrace, trace));

    // The leaderboard can hand back the level JSON to build a shareable ghost.
    std::string levelJson;
    CHECK(lb.levelJson(code, levelJson));
    const std::string share = encodeGhost(levelJson, rankTrace);
    CHECK(decodeGhost(share, levelJson).ok);

    // A rank that does not exist yields nothing.
    RunTrace none;
    std::string noname;
    CHECK(!lb.ghostForRank(code, 2, none, noname));
}

int main() {
    testRoundTripAndResimulate();
    testGhostMatchesOriginalTickForTick();
    testRejections();
    testLeaderboardGhostHook();

    if (g_failures == 0) {
        std::printf("All ghost replay tests passed.\n");
        return 0;
    }
    std::printf("%d ghost replay test(s) failed.\n", g_failures);
    return 1;
}
