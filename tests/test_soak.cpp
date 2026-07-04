// Bounded soak for long-session stability - issue #375.
//
// Drives sustained repeated sessions (play + editor churn + capture->play->share) so the
// ASan/UBSan/LSan job exercises those paths hard enough to catch leaks/corruption, and
// asserts the harness's retained cache stays bounded (iteration-independent) rather than
// accumulating. Also exercises tricky inputs as crash/corruption regression coverage.
// Pure std + the header-only game / editor / share cores:
//   g++ -std=c++17 -I src tests/test_soak.cpp -o test_soak
// (Runs under sanitizers in CI: g++ -fsanitize=address,undefined ...)

#include "game/Soak.h"

#include <cstdio>

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

// A level exercising every mechanic, for crash/corruption regression coverage.
static DungeonGame everyMechanic() {
    SceneDescription s;
    s.spawns = {
        EntitySpawn{"player", ecs::Vec3{0, 0, 0}, 0.0f},
        EntitySpawn{"coin", ecs::Vec3{1, 1, 0}, 0.0f},
        EntitySpawn{"enemy", ecs::Vec3{4, 4, 0}, 0.0f},
        EntitySpawn{"enemy_ranged", ecs::Vec3{-4, 4, 0}, 0.0f},
        EntitySpawn{"key@1", ecs::Vec3{2, 0, 0}, 0.0f},
        EntitySpawn{"lock@1", ecs::Vec3{3, 0, 0}, 0.0f},
        EntitySpawn{"switch@2", ecs::Vec3{-2, 0, 0}, 0.0f},
        EntitySpawn{"toggle@2", ecs::Vec3{-3, 0, 0}, 0.0f},
        EntitySpawn{"hazard", ecs::Vec3{0, 3, 0}, 0.0f},
        EntitySpawn{"block", ecs::Vec3{1, -1, 0}, 0.0f},
        EntitySpawn{"exit", ecs::Vec3{4, -4, 0}, 0.0f},
    };
    return loadGame(s);
}

int main() {
    // 1. A bounded soak completes cleanly with all share round-trips intact.
    {
        SoakConfig cfg;
        cfg.iterations = 200;
        cfg.stepsPerSession = 240;
        const SoakStats st = runSoak(cfg);
        std::printf("[info] soak: %d iters, %d games, %d shares, recent=%zu\n", st.iterations,
                    st.gamesPlayed, st.sharesRoundTripped, st.recentCodes);
        CHECK(st.iterations == 200);
        CHECK(st.gamesPlayed == 200);
        CHECK(st.sharesRoundTripped == 200);
        CHECK(st.allShareRoundTripsOk);
    }

    // 2. Bounded memory: the retained cache is a function of the cap, not iteration count -
    //    a slow leak / unbounded cache would grow it with the iteration count instead.
    {
        SoakConfig a;
        a.iterations = 100;
        a.recentCap = 16;
        SoakConfig b;
        b.iterations = 400;
        b.recentCap = 16;
        const SoakStats sa = runSoak(a);
        const SoakStats sb = runSoak(b);
        CHECK(sa.recentCodes == 16);              // capped
        CHECK(sb.recentCodes == 16);              // still capped at 4x the iterations
        CHECK(sa.recentCodes == sb.recentCodes);  // iteration-independent -> bounded
    }

    // 3. Determinism: the same seed soaks to the same aggregate stats.
    {
        SoakConfig cfg;
        cfg.iterations = 50;
        cfg.seed = 777;
        const SoakStats x = runSoak(cfg);
        const SoakStats y = runSoak(cfg);
        CHECK(x.editsApplied == y.editsApplied);
        CHECK(x.sharesRoundTripped == y.sharesRoundTripped);
    }

    // 4. Crash/corruption regression coverage: tricky inputs step without UB (checked hard by
    //    the sanitizer job).
    {
        // Every mechanic at once, driven for a long run.
        DungeonGame all = everyMechanic();
        detail::SoakLcg rng(9);
        for (int f = 0; f < 1000 && all.status == GameStatus::Playing; ++f)
            all.update(GameInput{rng.axis(), rng.axis()}, 1.0f / 60.0f);
        CHECK(true); // reaching here without a sanitizer abort is the assertion

        // An empty level (no spawns) must not crash when stepped.
        DungeonGame empty = loadGame(SceneDescription{});
        for (int f = 0; f < 100; ++f) empty.update(GameInput{1, 1}, 1.0f / 60.0f);
        CHECK(empty.status == GameStatus::Playing); // nothing to win/lose, just no crash

        // Editor round-trip on a churned level stays consistent (no dangling state).
        LevelEditor ed;
        for (int i = 0; i < 50; ++i) ed.placeWall(i % 7 - 3, i % 5 - 2);
        for (int i = 0; i < 30; ++i) ed.undo();
        const std::string text = ed.serialize();
        CHECK(LevelEditor::deserialize(text).serialize() == text);
    }

    if (g_failures == 0) {
        std::printf("test_soak: all checks passed\n");
        return 0;
    }
    std::printf("test_soak: %d check(s) failed\n", g_failures);
    return 1;
}
