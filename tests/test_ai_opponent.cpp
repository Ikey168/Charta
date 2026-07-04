// Deterministic AI opponent for Versus/FFA - issue #357.
//
// A bot turns the Solver route into a per-tick input stream. Checks: the stream clears the
// level and replay-verifies (replayRun, #242); it is deterministic per (seed, tier, level);
// higher tiers clear faster; a tier-3 clear fits a par-derived budget; and two bot streams
// plug into replayFFA (#318) to produce a reproducible bot-vs-bot result. Pure std +
// header-only:
//   g++ -std=c++17 -I src tests/test_ai_opponent.cpp -o test_ai_opponent

#include "game/AiOpponent.h"
#include "game/Leaderboard.h"  // replayRun, RunTrace
#include "game/LevelFormat.h"  // toLevelJson
#include "game/VersusFFA.h"    // replayFFA, FFAResult

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

static const float kDt = 1.0f / 60.0f;

int main() {
    LevelSpec spec;
    spec.walls.push_back(Wall{{{0, 0, 0}, {40, 0, 0}, {40, 0, 40}, {0, 0, 40}, {0, 0, 0}}});
    spec.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    spec.symbols.push_back(Symbol{"coin", ecs::Vec3{20, 0, 20}, 0.0f});
    spec.symbols.push_back(Symbol{"exit", ecs::Vec3{34, 0, 34}, 0.0f});
    const std::string json = toLevelJson(spec);
    const DungeonGame base = loadGame(convert(spec));

    // 1. The bot clears the level and its stream replay-verifies.
    {
        const BotRun r = runBot(base, BotConfig{3, 1});
        CHECK(r.won);
        RunTrace t;
        t.dt = kDt;
        t.inputs = r.inputs;
        CHECK(replayRun(json, t).won); // the same stream clears on re-simulation
    }

    // 2. Deterministic per (seed, tier, level).
    {
        const std::vector<GameInput> a = botInputStream(base, BotConfig{2, 7});
        const std::vector<GameInput> b = botInputStream(base, BotConfig{2, 7});
        CHECK(a.size() == b.size());
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) same = same && (a[i] == b[i]);
        CHECK(same);
    }

    // 3. Difficulty: a higher tier clears faster than a lower one.
    {
        const BotRun r1 = runBot(base, BotConfig{1, 5});
        const BotRun r3 = runBot(base, BotConfig{3, 5});
        CHECK(r1.won && r3.won);
        CHECK(r3.steps < r1.steps);
    }

    // 4. A tier-3 clear fits a par-derived tick budget.
    {
        const int par = solve(base).par;
        CHECK(par > 0);
        const BotRun r3 = runBot(base, BotConfig{3, 2});
        CHECK(r3.won);
        CHECK(r3.steps <= par * 30); // generous budget derived from the optimal par
    }

    // 5. Two bot streams plug into replayFFA to give a reproducible bot-vs-bot result.
    {
        const std::vector<GameInput> fast = botInputStream(base, BotConfig{3, 5}); // player 0
        const std::vector<GameInput> slow = botInputStream(base, BotConfig{1, 5}); // player 1
        const std::vector<std::vector<GameInput>> streams = {fast, slow};
        const FFAResult a = replayFFA(json, kDt, streams);
        const FFAResult b = replayFFA(json, kDt, streams);
        CHECK(a.decided);
        CHECK(a.winner == 0);                       // the faster (tier-3) bot wins
        CHECK(a.winner == b.winner);                // reproducible
        CHECK(a.winnerClearStep == b.winnerClearStep);
        CHECK(a.standings == b.standings);
    }

    if (g_failures == 0) {
        std::printf("test_ai_opponent: all checks passed\n");
        return 0;
    }
    std::printf("test_ai_opponent: %d check(s) failed\n", g_failures);
    return 1;
}
