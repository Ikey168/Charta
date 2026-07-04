// Desync recovery + connection-quality HUD - issue #360.
//
// A digest mismatch (#337) is recovered by rolling back to the last agreed snapshot and
// re-simulating the confirmed inputs, reconverging peers to an identical digest; the
// connection-quality signals fold into a score/level and a pause policy; and the quality HUD
// exposes the bar, input delay, and a resync indicator. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_desync_recovery.cpp -o test_desync_recovery

#include "game/DesyncRecovery.h"
#include "game/LevelFormat.h" // toLevelJson

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
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{30, 0, 30}, 0.0f});
    return toLevelJson(s);
}

int main() {
    const std::string json = makeLevelJson();

    // 1. Recovery: a diverged peer rolls back to the agreed snapshot and re-sims to reconverge.
    {
        DoodleNetState state = makeDoodleNetState(json, 2);
        CHECK(state.games.size() == 2);
        const std::vector<GameInput> move = {GameInput{1, 0}, GameInput{1, 0}};
        for (int f = 0; f < 20; ++f) doodleStep(state, move, f, kDt);
        const DoodleNetState snapshot = state; // last agreed state

        // The authoritative timeline: three more confirmed frames.
        const std::vector<std::vector<GameInput>> pending = {
            {GameInput{1, 0}, GameInput{1, 0}},
            {GameInput{0, 1}, GameInput{1, 0}},
            {GameInput{1, 1}, GameInput{1, 0}}};
        DoodleNetState reference = snapshot;
        int fr = 0;
        for (const auto& in : pending) doodleStep(reference, in, fr++, kDt);
        const std::uint64_t refDigest = stateDigest(reference);

        // Local mispredicts frame 1 for player 0 -> diverges.
        DoodleNetState local = snapshot;
        std::vector<std::vector<GameInput>> wrong = pending;
        wrong[1][0] = GameInput{-1, -1};
        fr = 0;
        for (const auto& in : wrong) doodleStep(local, in, fr++, kDt);
        CHECK(diverged(local, reference));

        ConnectionQuality q;
        const ResyncResult rr = resyncFrom(local, snapshot, pending, kDt, &q);
        CHECK(rr.recovered);
        CHECK(rr.rollbackDepth == 3);
        CHECK(rr.digest == refDigest);        // reconverged to the authoritative digest
        CHECK(!diverged(local, reference));
        CHECK(q.resyncs == 1 && q.lastRollbackDepth == 3);
    }

    // 2. Connection quality score / level / pause policy.
    {
        ConnectionQuality good;
        CHECK(good.score() == 1.0f);
        CHECK(std::string(good.level()) == "good");
        CHECK(!shouldPause(good));

        ConnectionQuality fair;
        fair.inputDelayFrames = 10; // score ~0.70
        CHECK(std::string(fair.level()) == "fair");

        ConnectionQuality bad;
        bad.inputDelayFrames = 6;
        bad.droppedFrames = 14;
        bad.lateFrames = 5; // score ~0.02
        CHECK(std::string(bad.level()) == "poor");
        CHECK(shouldPause(bad));

        // Quality drops as delay rises.
        ConnectionQuality a, b;
        b.inputDelayFrames = 10;
        CHECK(b.score() < a.score());
    }

    // 3. The quality HUD exposes the bar, delay, and a resync indicator.
    {
        ConnectionQuality q;
        q.inputDelayFrames = 3;
        Hud hud = buildQualityHud(q, /*resyncing=*/false);
        const HudElement *bar = nullptr, *delay = nullptr, *status = nullptr;
        for (const HudElement& e : hud.elements()) {
            if (e.name == "net_quality") bar = &e;
            else if (e.name == "net_delay") delay = &e;
            else if (e.name == "net_status") status = &e;
        }
        CHECK(bar && bar->widget == HudWidget::Bar);
        CHECK(bar && std::fabs(bar->bar() - q.score()) < 1e-6f);
        CHECK(delay && delay->value() == 3);
        CHECK(status && status->text() == "good");

        Hud resync = buildQualityHud(q, /*resyncing=*/true);
        const HudElement* st = nullptr;
        for (const HudElement& e : resync.elements())
            if (e.name == "net_status") st = &e;
        CHECK(st && st->text() == "RESYNC");
    }

    if (g_failures == 0) {
        std::printf("test_desync_recovery: all checks passed\n");
        return 0;
    }
    std::printf("test_desync_recovery: %d check(s) failed\n", g_failures);
    return 1;
}
