// Test for the real-time versus mode - issue #293.
//
// Versus layers two players' inputs onto the rollback session (net/Rollback.h #160 via
// the doodle adapter #291) over one shared level, resolving the winner by first clear.
// This verifies:
//   - two peers on a delayed, lossy channel converge to a no-network reference and agree
//     on the winner;
//   - the winner is decided by first clear with a deterministic (lower-index) tie-break;
//   - the match result re-simulates to the identical outcome on any instance, so a
//     reported win cannot be faked (verifyMatch anti-cheat);
//   - live per-tick standings track each player's progress.
// Headless, deterministic, std + header-only:
//   g++ -std=c++17 -I src tests/test_versus.cpp -o test_versus
#include "game/Versus.h"
#include "game/LevelFormat.h" // toLevelJson

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

// An open room with a player, one coin on the diagonal, and an exit further along it.
// No enemy: a player driving straight for the exit clears deterministically, so a race
// has a well-defined winner.
static std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {40, 0, 0}, {40, 0, 40}, {0, 0, 40}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{9, 0, 9}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{14, 0, 14}, 0.0f});
    return toLevelJson(s);
}

// Drive straight at the exit (collect the on-path coin en route): a guaranteed clear.
static std::vector<GameInput> rushInputs(int n) {
    return std::vector<GameInput>(static_cast<std::size_t>(n), GameInput{1.0f, 1.0f});
}
// Stand still: never clears.
static std::vector<GameInput> idleInputs(int n) {
    return std::vector<GameInput>(static_cast<std::size_t>(n), GameInput{});
}

// No-network ground truth for the converged state (all inputs authoritative).
static DoodleNetState referenceState(const std::string& json, const std::vector<GameInput>& p0,
                                     const std::vector<GameInput>& p1, int n) {
    DoodleNetState st = makeDoodleNetState(json, 2);
    for (int f = 0; f < n; ++f) {
        std::vector<GameInput> in = {p0[static_cast<std::size_t>(f)], p1[static_cast<std::size_t>(f)]};
        doodleStep(st, in, f, kDt);
    }
    return st;
}

// The rushing player clears and the idle player never does, so the winner is player 0,
// and replayVersus reports the clear step.
static void testFirstClearWins() {
    const std::string json = makeLevelJson();
    const int n = 400;
    const std::vector<GameInput> p0 = rushInputs(n);
    const std::vector<GameInput> p1 = idleInputs(n);

    const VersusResult r = replayVersus(json, kDt, p0, p1);
    CHECK(r.decided);
    CHECK(r.winner == 0);
    CHECK(r.clearStep[0] > 0);
    CHECK(r.clearStep[1] == -1);
    CHECK(r.winnerClearStep == r.clearStep[0]);
}

// Two identical rushing runs clear on the same step; the tie breaks to the lower index.
static void testDeterministicTieBreak() {
    const std::string json = makeLevelJson();
    const int n = 400;
    const std::vector<GameInput> p = rushInputs(n);

    const VersusResult r = replayVersus(json, kDt, p, p);
    CHECK(r.decided);
    CHECK(r.winner == 0); // same clear step -> lower index wins
    CHECK(r.clearStep[0] == r.clearStep[1]);
    CHECK(r.clearStep[0] > 0);
}

// Two peers exchanging inputs `delay` frames late (some dropped then flushed) converge to
// the no-network reference and agree on the winner.
static void runConvergence(int delay, bool lossy) {
    const std::string json = makeLevelJson();
    const int n = 400;
    const std::vector<GameInput> p0 = rushInputs(n); // player 0 clears
    const std::vector<GameInput> p1 = idleInputs(n); // player 1 idles

    const DoodleNetState ref = referenceState(json, p0, p1, n);
    const VersusResult refResult = replayVersus(json, kDt, p0, p1);

    Versus A(json, /*local=*/0, kDt);
    Versus B(json, /*local=*/1, kDt);
    CHECK(A.valid());
    CHECK(B.valid());

    for (int f = 0; f < n; ++f) {
        A.advance(p0[static_cast<std::size_t>(f)]);
        B.advance(p1[static_cast<std::size_t>(f)]);
        const int df = f - delay;
        if (df >= 0) {
            const bool drop = lossy && ((df % 5) == 0);
            if (!drop) {
                A.addRemoteInput(1, df, p1[static_cast<std::size_t>(df)]);
                B.addRemoteInput(0, df, p0[static_cast<std::size_t>(df)]);
            }
        }
    }
    // Flush the late tail and any dropped frames so both peers are fully authoritative.
    for (int f = 0; f < n; ++f) {
        A.addRemoteInput(1, f, p1[static_cast<std::size_t>(f)]);
        B.addRemoteInput(0, f, p0[static_cast<std::size_t>(f)]);
    }

    // Converged simulation state.
    CHECK(A.state() == ref);
    CHECK(B.state() == ref);
    // Both peers agree with each other and with the no-network verdict.
    CHECK(A.result().winner == refResult.winner);
    CHECK(B.result().winner == refResult.winner);
    CHECK(A.result().winner == 0);
    CHECK(A.decided());
    CHECK(B.decided());
    // Both peers produced the identical confirmed traces (the anti-cheat record matches).
    CHECK(A.confirmedInputs(0) == B.confirmedInputs(0));
    CHECK(A.confirmedInputs(1) == B.confirmedInputs(1));
}

static void testConvergesUnderLatency() { runConvergence(/*delay=*/4, /*lossy=*/false); }
static void testConvergesUnderLatencyAndLoss() { runConvergence(/*delay=*/6, /*lossy=*/true); }

// The match record re-simulates to the same outcome anywhere (anti-cheat): an honest
// record verifies, a flipped winner does not, the replay is deterministic, and editing an
// input changes the verified winner (so a fake cannot slip through).
static void testMatchResultIsVerifiable() {
    const std::string json = makeLevelJson();
    const int n = 400;
    const std::vector<GameInput> p0 = rushInputs(n);
    const std::vector<GameInput> p1 = idleInputs(n);

    MatchRecord rec;
    rec.levelJson = json;
    rec.dt = static_cast<double>(kDt);
    rec.inputs0 = p0;
    rec.inputs1 = p1;
    rec.claimedWinner = 0;

    VersusResult out;
    CHECK(verifyMatch(rec, out)); // honest claim verifies
    CHECK(out.winner == 0);

    MatchRecord faked = rec;
    faked.claimedWinner = 1;       // claim the loser won
    VersusResult out2;
    CHECK(!verifyMatch(faked, out2)); // re-simulation refutes it
    CHECK(out2.winner == 0);

    // Re-simulating the same traces on a fresh instance reproduces the identical outcome.
    const VersusResult again = replayVersus(json, kDt, p0, p1);
    CHECK(again.winner == out.winner);
    CHECK(again.winnerClearStep == out.winnerClearStep);

    // Editing player 1's inputs to also rush flips the verified winner to a tie-break,
    // so the outcome tracks the actual inputs and cannot be spoofed by relabeling.
    MatchRecord bothRush = rec;
    bothRush.inputs1 = p0;
    VersusResult out3;
    CHECK(verifyMatch(bothRush, out3)); // claimedWinner 0 still holds (lower-index tie-break)
    CHECK(out3.clearStep[0] == out3.clearStep[1]);
}

// Live standings track progress and reflect the win, for a HUD.
static void testLiveStandings() {
    const std::string json = makeLevelJson();
    const int n = 400;
    const std::vector<GameInput> p0 = rushInputs(n);
    const std::vector<GameInput> p1 = idleInputs(n);

    Versus v(json, /*local=*/0, kDt);
    CHECK(v.valid());
    bool sawLead = false;
    for (int f = 0; f < n; ++f) {
        v.advance(p0[static_cast<std::size_t>(f)]);
        v.addRemoteInput(1, f, p1[static_cast<std::size_t>(f)]);
        if (v.standing(0).progress > v.standing(1).progress) sawLead = true;
    }
    CHECK(sawLead);                       // the rushing player pulls ahead
    CHECK(v.liveLeader() == 1);           // player 0 is ahead at the end
    CHECK(v.standing(0).won);             // and has cleared
    CHECK(!v.standing(1).won);
    CHECK(v.standing(0).coinsCollected >= 1);
}

int main() {
    testFirstClearWins();
    testDeterministicTieBreak();
    testConvergesUnderLatency();
    testConvergesUnderLatencyAndLoss();
    testMatchResultIsVerifiable();
    testLiveStandings();

    if (g_failures == 0) {
        std::printf("All versus tests passed.\n");
        return 0;
    }
    std::printf("%d versus test(s) failed.\n", g_failures);
    return 1;
}
