#pragma once

#include "game/DungeonGame.h"  // DungeonGame, loadGame, SceneDescription
#include "game/LevelEditor.h"  // LevelEditor (editor churn)
#include "game/LevelShare.h"   // encodeShare/decodeShare (capture->play->share loop)

#include <cstdint>
#include <deque>
#include <string>

/**
 * @file Soak.h
 * @brief Headless soak harness for long-session stability under sanitizers (issue #375).
 *
 * Short unit tests miss slow leaks and unbounded caches; this drives sustained, repeated
 * sessions - extended play, editor churn, and the capture->play->share round-trip - in a
 * bounded loop so the ASan/UBSan/LSan job exercises those paths hard enough to surface a
 * leak or corruption. It also keeps a capped "recent" cache so the harness itself asserts
 * bounded memory: the retained size is a function of the cap, not of the iteration count, so
 * an unbounded accumulation would show up as growth. Pure std + the header-only game /
 * editor / share cores; header-only.
 */
namespace IKore {
namespace game {

struct SoakConfig {
    int iterations{200};
    int stepsPerSession{240};
    std::size_t recentCap{16}; ///< cap on the recent-levels cache (bounded memory).
    std::uint64_t seed{1};
};

struct SoakStats {
    int iterations{0};
    int gamesPlayed{0};
    int editsApplied{0};
    int sharesRoundTripped{0};
    std::size_t recentCodes{0};      ///< final cache size; must stay <= recentCap.
    bool allShareRoundTripsOk{true};
};

namespace detail {

/// A small deterministic LCG (portable, independent of <random>).
struct SoakLcg {
    std::uint64_t s;
    explicit SoakLcg(std::uint64_t seed) : s(seed * 2862933555777941757ULL + 3037000493ULL) {}
    std::uint32_t next() {
        s = s * 2862933555777941757ULL + 3037000493ULL;
        return static_cast<std::uint32_t>(s >> 32);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
    float axis() { return static_cast<float>(range(-1, 1)); }
};

/// A small feature-rich arena so a play session exercises walls, coins, and an exit.
inline DungeonGame soakArena() {
    SceneDescription s;
    auto wall = [&](float cx, float cz, float sx, float sz) {
        world::Box b;
        b.center = ecs::Vec3{cx, 0.0f, cz};
        b.size = ecs::Vec3{sx, 3.0f, sz};
        b.yaw = 0.0f;
        s.wallBoxes.push_back(b);
    };
    wall(0, 5, 10, 0.4f);
    wall(0, -5, 10, 0.4f);
    wall(5, 0, 0.4f, 10);
    wall(-5, 0, 0.4f, 10);
    s.spawns.push_back(EntitySpawn{"player", ecs::Vec3{0, 0, 0}, 0.0f});
    s.spawns.push_back(EntitySpawn{"coin", ecs::Vec3{2, 2, 0}, 0.0f});
    s.spawns.push_back(EntitySpawn{"exit", ecs::Vec3{3, -3, 0}, 0.0f});
    return loadGame(s);
}

} // namespace detail

/// Run @p cfg.iterations soak iterations: each plays a seeded session, churns the editor, and
/// round-trips a share code, pushing the code into a capped recent cache. Returns aggregate
/// stats; run under sanitizers to catch leaks/UB and check the reported cache stays bounded.
inline SoakStats runSoak(const SoakConfig& cfg) {
    SoakStats stats;
    const DungeonGame base = detail::soakArena();
    std::deque<std::string> recent; // bounded cache
    detail::SoakLcg rng(cfg.seed);

    for (int it = 0; it < cfg.iterations; ++it) {
        // 1. Extended play: step the sim under a seeded input stream.
        DungeonGame game = base;
        for (int f = 0; f < cfg.stepsPerSession; ++f)
            game.update(GameInput{rng.axis(), rng.axis()}, 1.0f / 60.0f);
        ++stats.gamesPlayed;

        // 2. Editor churn: place, erase, undo/redo, then clear (bounded history).
        LevelEditor editor;
        editor.placeObject(0, 0, "player");
        editor.placeObject(4, 0, "exit");
        const int walls = rng.range(3, 8);
        for (int w = 0; w < walls; ++w) {
            editor.placeWall(rng.range(-3, 3), rng.range(-3, 3));
            ++stats.editsApplied;
        }
        editor.placeObject(rng.range(-3, 3), rng.range(-3, 3), "coin");
        editor.undo();
        editor.redo();
        editor.undo();
        editor.eraseWall(rng.range(-3, 3), rng.range(-3, 3));

        // 3. Capture -> play -> share round-trip through the #317 path.
        const std::string level = editor.serialize();
        const std::string code = encodeShare(level);
        const ShareImport imp = decodeShare(code);
        if (!imp.ok || imp.levelJson != level) stats.allShareRoundTripsOk = false;
        ++stats.sharesRoundTripped;

        // 4. Push into a bounded cache; drop the oldest beyond the cap.
        recent.push_back(code);
        while (recent.size() > cfg.recentCap) recent.pop_front();

        ++stats.iterations;
    }

    stats.recentCodes = recent.size();
    return stats;
}

} // namespace game
} // namespace IKore
