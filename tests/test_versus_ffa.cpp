// Test for N-player (3-4) free-for-all versus - issue #318.
//
// Verifies the 2-player versus model generalizes to N in [2,4]: replayFFA resolves the
// winner by first clear with the deterministic lower-index tie-break and ranks all N into
// a standings order; N peers on a delayed, lossy channel converge to the no-network
// reference and agree on winner and standings; the N-way match record re-simulates
// (anti-cheat); and 2-player behavior equals replayVersus. Headless, std + header-only:
//   g++ -std=c++17 -I src tests/test_versus_ffa.cpp -o test_versus_ffa

#include "game/Versus.h"     // replayVersus (2-player equivalence check)
#include "game/VersusFFA.h"
#include "game/LevelFormat.h" // toLevelJson

#include <cstdio>
#include <memory>
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
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{9, 0, 9}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{14, 0, 14}, 0.0f});
    return toLevelJson(s);
}
static std::vector<GameInput> rush(int n) { return std::vector<GameInput>(n, GameInput{1.0f, 1.0f}); }
static std::vector<GameInput> idle(int n) { return std::vector<GameInput>(n, GameInput{}); }

static DoodleNetState referenceState(const std::string& json,
                                     const std::vector<std::vector<GameInput>>& in, int n) {
    DoodleNetState st = makeDoodleNetState(json, static_cast<int>(in.size()));
    for (int f = 0; f < n; ++f) {
        std::vector<GameInput> frame;
        for (const auto& tr : in) frame.push_back(tr[static_cast<std::size_t>(f)]);
        doodleStep(st, frame, f, kDt);
    }
    return st;
}

// N peers exchanging inputs `delay` frames late (some dropped then flushed) must converge
// to the no-network reference and agree on the winner and the full standings order.
static void runConvergence(int players, int delay, bool lossy) {
    const std::string json = makeLevelJson();
    const int n = 400;

    std::vector<std::vector<GameInput>> in;
    in.push_back(rush(n)); // player 0 clears
    for (int p = 1; p < players; ++p) in.push_back(idle(n)); // the rest idle

    const DoodleNetState ref = referenceState(json, in, n);
    const FFAResult refResult = replayFFA(json, kDt, in);
    CHECK(refResult.decided && refResult.winner == 0);

    std::vector<std::unique_ptr<VersusFFA>> peers;
    for (int p = 0; p < players; ++p)
        peers.push_back(std::unique_ptr<VersusFFA>(new VersusFFA(json, p, players, kDt)));
    for (const auto& pr : peers) CHECK(pr->valid());

    for (int f = 0; f < n; ++f) {
        for (int p = 0; p < players; ++p) peers[p]->advance(in[p][static_cast<std::size_t>(f)]);
        const int df = f - delay;
        if (df >= 0 && !(lossy && (df % 5) == 0)) {
            for (int q = 0; q < players; ++q)
                for (int r = 0; r < players; ++r)
                    if (r != q) peers[q]->addRemoteInput(r, df, in[r][static_cast<std::size_t>(df)]);
        }
    }
    // Flush the late tail and any drops so every peer is fully authoritative.
    for (int f = 0; f < n; ++f)
        for (int q = 0; q < players; ++q)
            for (int r = 0; r < players; ++r)
                if (r != q) peers[q]->addRemoteInput(r, f, in[r][static_cast<std::size_t>(f)]);

    for (int q = 0; q < players; ++q) {
        CHECK(peers[q]->state() == ref);
        const FFAResult res = peers[q]->result();
        CHECK(res.winner == refResult.winner);
        CHECK(res.decided);
        CHECK(res.standings == refResult.standings); // full ranking agrees across peers
    }
    // Idle players trail the winner; the winner leads the standings.
    CHECK(refResult.standings.front() == 0);
    CHECK(static_cast<int>(refResult.standings.size()) == players);
}

int main() {
    const std::string json = makeLevelJson();
    const int n = 400;

    // 2-player behavior is the special case: FFA agrees with replayVersus.
    {
        const std::vector<GameInput> p0 = rush(n), p1 = idle(n);
        const FFAResult ffa = replayFFA(json, kDt, {p0, p1});
        const VersusResult v = replayVersus(json, kDt, p0, p1);
        CHECK(ffa.winner == v.winner);
        CHECK(ffa.clearStep[0] == v.clearStep[0]);
        CHECK(ffa.clearStep[1] == v.clearStep[1]);
        CHECK(ffa.standings.size() == 2 && ffa.standings[0] == 0);
    }

    // Deterministic tie-break across 4 players: players 0 and 2 rush identically, 1 and 3
    // idle. Same clear step -> lower index (0) wins; standings put the two clearers first.
    {
        const FFAResult r = replayFFA(json, kDt, {rush(n), idle(n), rush(n), idle(n)});
        CHECK(r.decided);
        CHECK(r.winner == 0);
        CHECK(r.clearStep[0] == r.clearStep[2] && r.clearStep[0] > 0);
        CHECK(r.clearStep[1] == -1 && r.clearStep[3] == -1);
        CHECK(r.standings.size() == 4);
        CHECK(r.standings[0] == 0 && r.standings[1] == 2); // clearers first, by index tie
    }

    // Anti-cheat: an honest N-way record verifies, a flipped winner does not.
    {
        FFAMatchRecord rec;
        rec.levelJson = json;
        rec.dt = kDt;
        rec.inputs = {rush(n), idle(n), idle(n)};
        rec.claimedWinner = 0;
        FFAResult out;
        CHECK(verifyFFA(rec, out));
        CHECK(out.winner == 0);
        FFAMatchRecord faked = rec;
        faked.claimedWinner = 1;
        FFAResult out2;
        CHECK(!verifyFFA(faked, out2));
    }

    // 3- and 4-peer convergence under latency (and loss).
    runConvergence(/*players=*/3, /*delay=*/4, /*lossy=*/false);
    runConvergence(/*players=*/4, /*delay=*/6, /*lossy=*/true);

    if (g_failures == 0) {
        std::printf("test_versus_ffa: all checks passed\n");
        return 0;
    }
    std::printf("test_versus_ffa: %d check(s) failed\n", g_failures);
    return 1;
}
