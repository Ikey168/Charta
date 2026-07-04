// Test for the FFA standings HUD and the spectator replay scrubber - issue #335.
//
// Verifies the standings HUD lists all N players ranked by live progress (bound into the
// HUD framework), and that ReplayScrubber seeks to any tick deterministically (re-sim from
// the start), reproduces per-player state, and honors play/pause - for both a single run
// and a versus match. Pure std + the header-only game / ui:
//   g++ -std=c++17 -I src tests/test_spectator_ui.cpp -o test_spectator_ui

#include "game/LevelFormat.h" // toLevelJson
#include "game/SpectatorUi.h"

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
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{9, 0, 9}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{14, 0, 14}, 0.0f});
    return toLevelJson(s);
}

static std::vector<GameInput> winTrace(const std::string& json) {
    LevelSpec spec;
    fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    std::vector<GameInput> ins;
    for (int i = 0; i < 800 && g.status == GameStatus::Playing; ++i) {
        const GameInput in{1.0f, 1.0f};
        g.update(in, kDt);
        ins.push_back(in);
    }
    return ins;
}

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

int main() {
    const std::string json = makeLevelJson();

    // --- FFA standings HUD ------------------------------------------------------
    {
        VersusFFA match(json, /*local=*/0, /*players=*/3, kDt);
        CHECK(match.valid());
        for (int f = 0; f < 120; ++f) {
            match.advance(GameInput{1.0f, 1.0f});         // player 0 rushes
            match.addRemoteInput(1, f, GameInput{});      // players 1 and 2 idle
            match.addRemoteInput(2, f, GameInput{});
        }
        const std::vector<std::string> lines = ffaStandingsLines(match);
        CHECK(lines.size() == 3);
        CHECK(contains(lines[0], "P0"));                  // the rusher leads the standings
        CHECK(match.standings().front().player == 0);

        Hud hud = buildFfaHud(match);
        CHECK(hud.count() == 1);
        const std::vector<std::string> hudLines = hud.elements()[0].list();
        CHECK(hudLines.size() == 3);
        CHECK(contains(hudLines[0], "P0"));
    }

    // --- Replay scrubber: single run -------------------------------------------
    {
        RunTrace t;
        t.dt = kDt;
        t.inputs = winTrace(json);
        const Replay replay = makeRunReplay(json, t);
        ReplayScrubber sc(replay);
        CHECK(sc.valid());
        CHECK(sc.players() == 1);
        CHECK(sc.length() > 0);

        sc.seek(0);
        CHECK(sc.game(0).coinsCollected == 0);
        const float startX = sc.game(0).playerPosition.x;

        const std::size_t mid = sc.length() / 2;
        sc.seek(mid);
        const float midX = sc.game(0).playerPosition.x;
        CHECK(midX > startX);                              // advanced by the mid tick

        sc.seek(sc.length());
        CHECK(sc.game(0).won());                           // the run clears by the end

        // Determinism: seeking back and forth reproduces the exact mid state.
        sc.seek(0);
        sc.seek(mid);
        CHECK(sc.game(0).playerPosition.x == midX);

        // Play/pause.
        sc.seek(0);
        sc.pause();
        CHECK(!sc.advance());                              // paused: no advance
        sc.play();
        CHECK(sc.advance());                               // playing: advances
        CHECK(sc.tick() == 1);
    }

    // --- Replay scrubber: versus match -----------------------------------------
    {
        std::vector<GameInput> winner = winTrace(json);
        std::vector<GameInput> loser(winner.size(), GameInput{}); // idle
        MatchRecord rec;
        rec.levelJson = json;
        rec.dt = kDt;
        rec.inputs0 = winner;
        rec.inputs1 = loser;
        rec.claimedWinner = 0;
        const Replay replay = makeVersusReplay(rec);

        ReplayScrubber sc(replay);
        CHECK(sc.players() == 2);
        sc.seek(sc.length());
        CHECK(sc.game(0).won());
        CHECK(!sc.game(1).won());
    }

    if (g_failures == 0) {
        std::printf("test_spectator_ui: all checks passed\n");
        return 0;
    }
    std::printf("test_spectator_ui: %d check(s) failed\n", g_failures);
    return 1;
}
