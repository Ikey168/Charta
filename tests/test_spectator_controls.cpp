// Wire the replay scrubber to input and resolve the spectator HUD - issue #352 (headless).
//
// Verifies the input -> scrubber command layer (play/pause, step, seek and their timeline
// sync), the scrubber HUD (a progress bar + label bound to the scrubber), and resolveHud()
// turning a bound Hud (scrubber + FFA standings) into positioned draw items read from their
// live sources. The GL draw of these items is exercised by test_spectator_hud_gl. Pure std +
// header-only:
//   g++ -std=c++17 -I src tests/test_spectator_controls.cpp -o test_spectator_controls

#include "game/LevelFormat.h" // toLevelJson
#include "game/SpectatorControls.h"

#include <cmath>
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

static RunTrace winTrace(const std::string& json) {
    LevelSpec spec;
    fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    RunTrace t;
    t.dt = kDt;
    for (int i = 0; i < 200 && g.status == GameStatus::Playing; ++i) {
        const GameInput in{1.0f, 1.0f};
        g.update(in, kDt);
        t.inputs.push_back(in);
    }
    return t;
}

static const HudDrawItem* find(const std::vector<HudDrawItem>& v, const std::string& name) {
    for (const HudDrawItem& i : v) if (i.name == name) return &i;
    return nullptr;
}

int main() {
    const std::string json = makeLevelJson();
    const Replay replay = makeRunReplay(json, winTrace(json));

    // 1. Input -> scrubber commands.
    {
        ReplayScrubber sc(replay);
        CHECK(sc.valid());
        CHECK(!sc.playing() && sc.tick() == 0);

        applyScrub(sc, ScrubCommand::TogglePlay);
        CHECK(sc.playing());
        applyScrub(sc, ScrubCommand::Pause);
        CHECK(!sc.playing());

        applyScrub(sc, ScrubCommand::StepForward);
        CHECK(sc.tick() == 1 && !sc.playing());
        applyScrub(sc, ScrubCommand::StepForward);
        CHECK(sc.tick() == 2);
        applyScrub(sc, ScrubCommand::StepBack);
        CHECK(sc.tick() == 1);

        applyScrub(sc, ScrubCommand::SeekFraction, 0.5f);
        const std::size_t mid = sc.length() / 2;
        CHECK(sc.tick() == mid);
        CHECK(!sc.playing());

        // Seeking is deterministic: an independent scrubber at the same tick agrees.
        ReplayScrubber other(replay);
        other.seek(mid);
        CHECK(sc.game(0).playerPosition.x == other.game(0).playerPosition.x);
        CHECK(sc.game(0).playerPosition.z == other.game(0).playerPosition.z);
    }

    // 2. Scrubber HUD: a progress bar + label bound to the live scrubber, resolved to draw
    //    items that update as the tick advances.
    {
        ReplayScrubber sc(replay);
        const Hud hud = buildScrubberHud(sc);

        sc.seek(0);
        std::vector<HudDrawItem> items = resolveHud(hud);
        const HudDrawItem* bar0 = find(items, "scrub_bar");
        const HudDrawItem* label0 = find(items, "scrub_label");
        CHECK(bar0 && bar0->widget == HudWidget::Bar);
        CHECK(bar0 && std::fabs(bar0->bar - 0.0f) < 1e-6f);
        CHECK(label0 && label0->text.find("PAUSE 0/") != std::string::npos);

        sc.seek(sc.length()); // to the end
        items = resolveHud(hud);
        const HudDrawItem* bar1 = find(items, "scrub_bar");
        CHECK(bar1 && bar1->bar > 0.9f); // bar fills as the timeline advances
        CHECK(std::fabs(scrubProgress(sc) - bar1->bar) < 1e-6f);
    }

    // 3. FFA standings HUD resolves to a live-bound list item.
    {
        VersusFFA match(json, /*local=*/0, /*players=*/3, kDt);
        CHECK(match.valid());
        for (int f = 0; f < 60; ++f) {
            match.advance(GameInput{1.0f, 1.0f});
            match.addRemoteInput(1, f, GameInput{});
            match.addRemoteInput(2, f, GameInput{});
        }
        const Hud hud = buildFfaHud(match);
        const std::vector<HudDrawItem> items = resolveHud(hud);
        const HudDrawItem* list = find(items, "ffa_standings");
        CHECK(list && list->widget == HudWidget::List);
        CHECK(list && list->lines.size() == 3);
        CHECK(list && list->lines == ffaStandingsLines(match)); // bound to the live match
    }

    if (g_failures == 0) {
        std::printf("test_spectator_controls: all checks passed\n");
        return 0;
    }
    std::printf("test_spectator_controls: %d check(s) failed\n", g_failures);
    return 1;
}
