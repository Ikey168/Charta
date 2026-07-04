// Single-elimination tournament over verified match records - issue #358.
//
// Seeds N players, runs rounds off replay-verified MatchRecords, advances winners, and
// crowns a champion. Checks: an 8-player bracket resolves to one champion in 7 matches; a
// 2-player bracket lets player B win; byes are handled for a 6-player field; and a forged
// record (claimed winner != replay) is rejected and advances no one. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_tournament.cpp -o test_tournament

#include "game/LevelFormat.h" // toLevelJson
#include "game/Tournament.h"

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

static const double kDt = 1.0 / 60.0;

static std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {30, 0, 0}, {30, 0, 30}, {0, 0, 30}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{4, 0, 4}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{12, 0, 12}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{24, 0, 24}, 0.0f});
    return s.walls.empty() ? std::string() : toLevelJson(s);
}

static std::vector<GameInput> winTrace(const std::string& json) {
    LevelSpec spec;
    fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    std::vector<GameInput> ins;
    for (int i = 0; i < 800 && g.status == GameStatus::Playing; ++i) {
        const GameInput in{1.0f, 1.0f};
        g.update(in, static_cast<float>(kDt));
        ins.push_back(in);
    }
    return ins;
}

// A record where `winner` (0 or 1) plays the clearing trace and the other idles.
static MatchRecord record(const std::string& json, const std::vector<GameInput>& win, int winner) {
    MatchRecord r;
    r.levelJson = json;
    r.dt = kDt;
    const std::vector<GameInput> idle(win.size(), GameInput{0.0f, 0.0f});
    r.inputs0 = winner == 0 ? win : idle;
    r.inputs1 = winner == 0 ? idle : win;
    r.claimedWinner = winner;
    return r;
}

int main() {
    const std::string json = makeLevelJson();
    const std::vector<GameInput> win = winTrace(json);

    // 1. An 8-player bracket resolves to one champion in 7 matches (player A always wins,
    //    so the top seed advances all the way).
    {
        Tournament t(8);
        CHECK(!t.complete());
        CHECK(t.nextMatch().playerA == 0 && t.nextMatch().playerB == 1);
        int reports = 0;
        while (!t.complete()) {
            CHECK(t.reportMatch(record(json, win, 0)));
            ++reports;
        }
        CHECK(reports == 7);
        CHECK(t.champion() == 0);
        CHECK(t.history().size() == 7);
        CHECK(t.round() == 3); // 8 -> 4 -> 2 -> 1
    }

    // 2. Player B can win a pairing.
    {
        Tournament t(2);
        CHECK(t.reportMatch(record(json, win, 1)));
        CHECK(t.complete());
        CHECK(t.champion() == 1);
    }

    // 3. Byes are handled for a non-power-of-two (6-player) field.
    {
        Tournament t(6);
        int reports = 0;
        while (!t.complete()) {
            CHECK(t.reportMatch(record(json, win, 0)));
            ++reports;
        }
        CHECK(reports == 5); // round0: 3, round1: 1 (+bye), round2: 1
        CHECK(t.champion() == 0);
    }

    // 4. A forged record (claimed winner disagrees with the replay) advances no one.
    {
        Tournament t(8);
        MatchRecord forged;
        forged.levelJson = json;
        forged.dt = kDt;
        forged.inputs0 = win;                              // player 0 actually clears
        forged.inputs1 = std::vector<GameInput>(win.size(), GameInput{0, 0});
        forged.claimedWinner = 1;                          // ...but claims player 1 won
        const int pendingBefore = t.pendingMatches();
        CHECK(!t.reportMatch(forged));                     // rejected
        CHECK(t.pendingMatches() == pendingBefore);        // nothing advanced
        // A truthful record still advances the bracket.
        CHECK(t.reportMatch(record(json, win, 0)));
        CHECK(t.pendingMatches() == pendingBefore - 1);
    }

    if (g_failures == 0) {
        std::printf("test_tournament: all checks passed\n");
        return 0;
    }
    std::printf("test_tournament: %d check(s) failed\n", g_failures);
    return 1;
}
