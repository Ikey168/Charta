// Test for shareable spectator replays - issue #317.
//
// Verifies: a single run and a 2-player versus match each package into a compact,
// content-verified share string that round-trips (encode/decode/re-sim) to the identical
// outcome; a tampered share string is rejected on decode; a replay whose claim does not
// re-simulate is rejected by verifyReplay; and ReplayPlayer reproduces per-tick state for
// both single run and versus. Pure std + the header-only game:
//   g++ -std=c++17 -I src tests/test_replay.cpp -o test_replay

#include "game/Replay.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

// Play +X until the game is won (or capped) to synthesize a real winning trace.
static std::vector<game::GameInput> winTrace(const game::LevelSpec& spec, float dt) {
    game::DungeonGame g = game::loadGame(game::convert(spec));
    std::vector<game::GameInput> ins;
    for (int i = 0; i < 600 && g.status == game::GameStatus::Playing; ++i) {
        const game::GameInput in{1.0f, 0.0f};
        g.update(in, dt);
        ins.push_back(in);
    }
    return ins;
}

int main() {
    const float dt = 1.0f / 60.0f;

    // A trivial level: player at origin, exit to the +X, no coins.
    game::LevelSpec spec;
    spec.symbols.push_back(game::Symbol{"player", ecs::Vec3{0.0f, 0.0f, 0.0f}, 0.0f});
    spec.symbols.push_back(game::Symbol{"exit", ecs::Vec3{3.0f, 0.0f, 0.0f}, 0.0f});
    const std::string level = game::toLevelJson(spec);

    const std::vector<game::GameInput> winner = winTrace(spec, dt);
    CHECK(!winner.empty());

    // --- Single-run replay -----------------------------------------------------
    game::RunTrace rt;
    rt.dt = dt;
    rt.inputs = winner;
    const game::Replay runReplay = game::makeRunReplay(level, rt);
    CHECK(runReplay.traces.size() == 1);
    CHECK(runReplay.claimedWinner == 0); // it is a winning run
    CHECK(game::verifyReplay(runReplay));

    const std::string runShare = game::encodeReplay(runReplay);
    game::Replay runDecoded;
    CHECK(game::decodeReplay(runShare, runDecoded));
    CHECK(runDecoded.traces.size() == 1 && runDecoded.traces[0].size() == winner.size());
    CHECK(runDecoded.dt == runReplay.dt && runDecoded.claimedWinner == 0);
    CHECK(game::verifyReplay(runDecoded));

    // Re-simulate anywhere -> identical outcome.
    game::ReplayPlayer rp(runDecoded);
    CHECK(rp.valid() && rp.players() == 1);
    rp.playToEnd();
    CHECK(rp.game(0).won());

    // Tampered share string is rejected on decode (flip a byte in the base64 payload).
    std::string tampered = runShare;
    tampered[tampered.size() - 3] = (tampered[tampered.size() - 3] == 'A') ? 'B' : 'A';
    game::Replay junk;
    CHECK(!game::decodeReplay(tampered, junk));

    // A replay whose claim does not re-simulate is rejected.
    game::Replay lyingRun = runReplay;
    lyingRun.claimedWinner = -1; // claims "did not clear" but it did
    CHECK(!game::verifyReplay(lyingRun));

    // --- Versus replay ---------------------------------------------------------
    std::vector<game::GameInput> loser(winner.size(), game::GameInput{0.0f, 0.0f}); // stands still
    game::MatchRecord rec;
    rec.levelJson = level;
    rec.dt = dt;
    rec.inputs0 = winner;
    rec.inputs1 = loser;
    rec.claimedWinner = 0;
    game::VersusResult vr;
    CHECK(game::verifyMatch(rec, vr)); // sanity: player 0 wins

    const game::Replay matchReplay = game::makeVersusReplay(rec);
    CHECK(matchReplay.traces.size() == 2);
    CHECK(game::verifyReplay(matchReplay));

    const std::string matchShare = game::encodeReplay(matchReplay);
    game::Replay matchDecoded;
    CHECK(game::decodeReplay(matchShare, matchDecoded));
    CHECK(matchDecoded.traces.size() == 2);
    CHECK(matchDecoded.claimedWinner == 0);
    CHECK(game::verifyReplay(matchDecoded));

    game::ReplayPlayer mp(matchDecoded);
    CHECK(mp.valid() && mp.players() == 2);
    mp.playToEnd();
    CHECK(mp.game(0).won());
    CHECK(!mp.game(1).won());

    // Tampering the claimed winner makes verification fail.
    game::Replay lyingMatch = matchDecoded;
    lyingMatch.claimedWinner = 1;
    CHECK(!game::verifyReplay(lyingMatch));

    // Determinism: two independent players reach the same final coins/status.
    game::ReplayPlayer mp2(matchDecoded);
    mp2.playToEnd();
    CHECK(mp2.game(0).won() == mp.game(0).won());
    CHECK(mp2.game(1).coinsCollected == mp.game(1).coinsCollected);

    if (g_failures == 0) {
        std::printf("test_replay: all checks passed\n");
        return 0;
    }
    std::printf("test_replay: %d check(s) failed\n", g_failures);
    return 1;
}
