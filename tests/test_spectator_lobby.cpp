// Spectator lobby: list live/finished sessions and route in - issue #359.
//
// Verifies the lobby lists a mix of live sessions and finished-match replays in a
// deterministic order (live first, then most recent, then id), reads a live session's
// standings (driving SpectatorUi), opens a finished entry's replay into a scrubbable
// timeline, decodes a pasted share code into a scrubber, and reports an invalid code as an
// invalid scrubber. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_spectator_lobby.cpp -o test_spectator_lobby

#include "game/LevelFormat.h" // toLevelJson
#include "game/Replay.h"      // makeRunReplay, encodeReplay
#include "game/SpectatorLobby.h"
#include "game/VersusFFA.h"

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

static std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {40, 0, 0}, {40, 0, 40}, {0, 0, 40}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{9, 0, 9}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{14, 0, 14}, 0.0f});
    return toLevelJson(s);
}

static std::string finishedCode(const std::string& json) {
    LevelSpec spec;
    fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    RunTrace t;
    t.dt = kDt;
    for (int i = 0; i < 400 && g.status == GameStatus::Playing; ++i) {
        const GameInput in{1.0f, 1.0f};
        g.update(in, kDt);
        t.inputs.push_back(in);
    }
    return encodeReplay(makeRunReplay(json, t));
}

int main() {
    const std::string json = makeLevelJson();
    const std::string code = finishedCode(json);

    VersusFFA live(json, /*local=*/0, /*players=*/3, kDt);
    for (int f = 0; f < 60; ++f) {
        live.advance(GameInput{1.0f, 1.0f});
        live.addRemoteInput(1, f, GameInput{});
        live.addRemoteInput(2, f, GameInput{});
    }

    SpectatorLobby lobby;
    lobby.addFinished("f1", "Finished A", 2, /*updatedAt=*/100, code);
    lobby.addLive("live1", "Live Match", 3, /*updatedAt=*/50, &live);
    lobby.addFinished("f2", "Finished B", 2, /*updatedAt=*/200, code);
    CHECK(lobby.size() == 3);

    // 1. Listing: live first, then most-recent finished, then id.
    {
        const std::vector<LobbyEntry> l = lobby.list();
        CHECK(l.size() == 3);
        CHECK(l[0].id == "live1" && l[0].live);   // live first
        CHECK(l[1].id == "f2");                    // newer finished (200) before...
        CHECK(l[2].id == "f1");                    // ...older finished (100)
    }

    // 2. A live session's standings drive SpectatorUi.
    {
        std::vector<std::string> standings;
        CHECK(lobby.liveStandings("live1", standings));
        CHECK(standings.size() == 3);
        CHECK(standings == ffaStandingsLines(live)); // bound to the live match
        std::vector<std::string> none;
        CHECK(!lobby.liveStandings("f1", none)); // a finished entry is not live
    }

    // 3. Opening a finished entry yields a scrubbable timeline.
    {
        ReplayScrubber sc = lobby.openReplay("f2");
        CHECK(sc.valid());
        CHECK(sc.length() > 0);
        sc.seek(sc.length());
        CHECK(sc.tick() == sc.length());
        // Unknown id -> invalid scrubber.
        CHECK(!lobby.openReplay("nope").valid());
    }

    // 4. Paste-a-code opens the right replay; an invalid code is reported cleanly.
    {
        CHECK(SpectatorLobby::openCode(code).valid());
        CHECK(!SpectatorLobby::openCode("not-a-real-code").valid());
    }

    if (g_failures == 0) {
        std::printf("test_spectator_lobby: all checks passed\n");
        return 0;
    }
    std::printf("test_spectator_lobby: %d check(s) failed\n", g_failures);
    return 1;
}
