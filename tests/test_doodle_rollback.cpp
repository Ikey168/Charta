// Test for the doodle rollback adapter - issue #291.
//
// Verifies the doodle game plugs into the GGPO-style rollback core (net/Rollback.h,
// #160): two RollbackSessions driving a DoodleNetState exchange inputs over a delayed,
// lossy channel and, once all inputs are delivered, reach state identical to a
// no-network reference; mispredictions roll back and resimulate to exactly the
// reference; and a correctly-predicted stream never rolls back. Mirrors test_rollback
// with the doodle sim substituted, so it is headless and deterministic.
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_doodle_rollback.cpp -o test_doodle_rollback

#include "game/DoodleRollback.h"
#include "game/LevelFormat.h" // toLevelJson

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace IKore;
using namespace IKore::game;
using IKore::net::RollbackSession;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static const float kDt = 1.0f / 60.0f;

// A room with a player, a coin, an exit, and a chasing enemy, so the per-frame state
// (position, coins, enemy) is rich enough for a misprediction to visibly diverge.
static std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {40, 0, 0}, {40, 0, 40}, {0, 0, 40}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{6, 0, 6}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{20, 0, 20}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{34, 0, 34}, 0.0f});
    s.symbols.push_back(Symbol{"enemy", ecs::Vec3{30, 0, 8}, 0.0f});
    return toLevelJson(s);
}

// A tiny deterministic PRNG so both peers and the reference script identical inputs.
static std::uint64_t rng(std::uint64_t& s) {
    s += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static std::vector<GameInput> scriptInputs(std::uint64_t seed, int n) {
    std::vector<GameInput> v(static_cast<std::size_t>(n));
    std::uint64_t s = seed;
    for (int f = 0; f < n; ++f) {
        v[static_cast<std::size_t>(f)].moveX = static_cast<float>(static_cast<int>(rng(s) % 3) - 1); // -1,0,1
        v[static_cast<std::size_t>(f)].moveZ = static_cast<float>(static_cast<int>(rng(s) % 3) - 1);
    }
    return v;
}

// Ground truth: run the shared step with all inputs authoritative (no prediction).
static DoodleNetState referenceRun(const std::string& json, const std::vector<GameInput>& p0,
                                   const std::vector<GameInput>& p1, int n) {
    DoodleNetState st = makeDoodleNetState(json, 2);
    for (int f = 0; f < n; ++f) {
        std::vector<GameInput> in = {p0[static_cast<std::size_t>(f)], p1[static_cast<std::size_t>(f)]};
        doodleStep(st, in, f, kDt);
    }
    return st;
}

// Two sessions exchanging inputs `delay` frames late (and, when lossy, some deliveries
// dropped then flushed at the end) converge to the reference.
static void runConvergence(const std::string& json, std::uint64_t seedA, std::uint64_t seedB,
                           int n, int delay, bool lossy) {
    const std::vector<GameInput> p0 = scriptInputs(seedA, n);
    const std::vector<GameInput> p1 = scriptInputs(seedB, n);
    const DoodleNetState ref = referenceRun(json, p0, p1, n);

    RollbackSession<DoodleNetState, GameInput> A(2, 0, makeDoodleNetState(json, 2), makeDoodleStepFn(kDt));
    RollbackSession<DoodleNetState, GameInput> B(2, 1, makeDoodleNetState(json, 2), makeDoodleStepFn(kDt));

    for (int f = 0; f < n; ++f) {
        A.advanceFrame(p0[static_cast<std::size_t>(f)]);
        B.advanceFrame(p1[static_cast<std::size_t>(f)]);
        const int df = f - delay;
        if (df >= 0) {
            const bool drop = lossy && ((df % 5) == 0); // drop 1 in 5, redelivered later
            if (!drop) {
                A.addRemoteInput(1, df, p1[static_cast<std::size_t>(df)]);
                B.addRemoteInput(0, df, p0[static_cast<std::size_t>(df)]);
            }
        }
    }
    // Flush every remaining authoritative input (late tail + dropped frames).
    for (int f = 0; f < n; ++f) {
        A.addRemoteInput(1, f, p1[static_cast<std::size_t>(f)]);
        B.addRemoteInput(0, f, p0[static_cast<std::size_t>(f)]);
    }

    CHECK(A.state() == ref);
    CHECK(B.state() == ref);
    CHECK(stateDigest(A.state()) == stateDigest(ref));
    CHECK(stateDigest(B.state()) == stateDigest(ref));
}

static void testConvergesUnderLatency() {
    runConvergence(makeLevelJson(), 111, 222, 240, /*delay=*/4, /*lossy=*/false);
}

static void testConvergesUnderLatencyAndLoss() {
    runConvergence(makeLevelJson(), 333, 444, 240, /*delay=*/6, /*lossy=*/true);
}

// Mispredictions actually happen (the varying stream cannot be predicted) yet the
// final state is exactly the reference: rollback corrected the divergence.
static void testMispredictionRollsBackToReference() {
    const std::string json = makeLevelJson();
    const int n = 180, delay = 5;
    const std::vector<GameInput> p0 = scriptInputs(9, n);
    const std::vector<GameInput> p1 = scriptInputs(10, n);
    const DoodleNetState ref = referenceRun(json, p0, p1, n);

    RollbackSession<DoodleNetState, GameInput> A(2, 0, makeDoodleNetState(json, 2), makeDoodleStepFn(kDt));
    for (int f = 0; f < n; ++f) {
        A.advanceFrame(p0[static_cast<std::size_t>(f)]);
        const int df = f - delay;
        if (df >= 0) A.addRemoteInput(1, df, p1[static_cast<std::size_t>(df)]);
    }
    for (int f = 0; f < n; ++f) A.addRemoteInput(1, f, p1[static_cast<std::size_t>(f)]);

    CHECK(A.rollbackCount() > 0);   // the delayed, varying remote stream forced rollbacks
    CHECK(A.state() == ref);        // and it still landed exactly on the reference
}

// A remote stream equal to the default input is predicted correctly from the start
// (before any input, prediction is the default), so no rollback ever occurs.
static void testCorrectPredictionNeverRollsBack() {
    const std::string json = makeLevelJson();
    const int n = 120, delay = 4;
    const std::vector<GameInput> p0 = scriptInputs(7, n);
    std::vector<GameInput> p1(static_cast<std::size_t>(n), GameInput{}); // matches the default prediction

    RollbackSession<DoodleNetState, GameInput> A(2, 0, makeDoodleNetState(json, 2), makeDoodleStepFn(kDt));
    for (int f = 0; f < n; ++f) {
        A.advanceFrame(p0[static_cast<std::size_t>(f)]);
        const int df = f - delay;
        if (df >= 0) A.addRemoteInput(1, df, p1[static_cast<std::size_t>(df)]);
    }
    CHECK(A.rollbackCount() == 0); // repeat-last-input predicted the default stream exactly
}

int main() {
    testConvergesUnderLatency();
    testConvergesUnderLatencyAndLoss();
    testMispredictionRollsBackToReference();
    testCorrectPredictionNeverRollsBack();

    if (g_failures == 0) {
        std::printf("All doodle rollback tests passed.\n");
        return 0;
    }
    std::printf("%d doodle rollback test(s) failed.\n", g_failures);
    return 1;
}
