#pragma once

#include "game/DungeonGame.h" // DungeonGame, GameInput, loadGame

#include <cstdint>
#include <functional>
#include <vector>

/**
 * @file DeterminismFuzz.h
 * @brief Seeded determinism fuzzing over the sim, with minimal-repro shrinking (issue #374).
 *
 * The sim/rollback is meant to be bit-reproducible; a fuzzer proves it at scale and catches
 * rare divergences. This drives the deterministic DungeonGame loop with seeded random input
 * streams and digests each trajectory, so a large seeded batch can be re-simulated and
 * checked for identical digests. When two implementations disagree (a real nondeterminism
 * bug, or an injected one in a test), fuzzDivergence scans ascending to report the minimal
 * failing seed, and shrinkSteps reduces it to the shortest failing input prefix - a minimal
 * repro. Pure std + the header-only game core; header-only.
 */
namespace IKore {
namespace game {

namespace detail {

/// A small, portable LCG so seeded streams are identical on every platform.
struct FuzzLcg {
    std::uint64_t state;
    explicit FuzzLcg(std::uint64_t seed) : state(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    std::uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 32);
    }
    /// A move component in {-1, 0, +1}.
    float axis() { return static_cast<float>(static_cast<int>(next() % 3u) - 1); }
};

/// Fold a float into a running FNV-1a digest, quantized so tiny float noise does not alias
/// but a real state difference does.
inline void hashFloat(std::uint64_t& h, float f) {
    const std::int64_t q = static_cast<std::int64_t>(f * 1024.0f + (f >= 0 ? 0.5f : -0.5f));
    for (int b = 0; b < 8; ++b) {
        h ^= static_cast<std::uint64_t>((q >> (b * 8)) & 0xff);
        h *= 1099511628211ULL;
    }
}

} // namespace detail

/// A seeded random input stream of @p steps frames.
inline std::vector<GameInput> randomInputStream(std::uint64_t seed, int steps) {
    detail::FuzzLcg rng(seed);
    std::vector<GameInput> in;
    in.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) in.push_back(GameInput{rng.axis(), rng.axis()});
    return in;
}

/// Digest of running @p base for @p steps under the seeded input stream. Folds the player
/// position, coin count, and status each frame, so any trajectory difference changes it.
inline std::uint64_t simulateDigest(const DungeonGame& base, std::uint64_t seed, int steps,
                                    float dt = 1.0f / 60.0f) {
    DungeonGame game = base;
    const std::vector<GameInput> in = randomInputStream(seed, steps);
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
    for (int i = 0; i < steps; ++i) {
        game.update(in[static_cast<std::size_t>(i)], dt);
        detail::hashFloat(h, game.playerPosition.x);
        detail::hashFloat(h, game.playerPosition.z);
        h ^= static_cast<std::uint64_t>(game.coinsCollected);
        h *= 1099511628211ULL;
        h ^= static_cast<std::uint64_t>(game.status);
        h *= 1099511628211ULL;
    }
    return h;
}

struct FuzzReport {
    int seedsChecked{0};
    bool deterministic{true}; ///< every seed re-simulated to an identical digest.
};

/// Re-simulate @p base across @p nSeeds seeds and confirm each run reproduces its digest.
inline FuzzReport fuzzDeterminism(const DungeonGame& base, std::uint64_t nSeeds, int steps,
                                  float dt = 1.0f / 60.0f) {
    FuzzReport r;
    for (std::uint64_t s = 0; s < nSeeds; ++s) {
        ++r.seedsChecked;
        if (simulateDigest(base, s, steps, dt) != simulateDigest(base, s, steps, dt)) {
            r.deterministic = false;
            break;
        }
    }
    return r;
}

/// A digest of one sim variant for a (seed, steps) pair.
using SimDigestFn = std::function<std::uint64_t(std::uint64_t seed, int steps)>;

struct DivergenceReport {
    bool found{false};
    std::uint64_t minimalSeed{0}; ///< smallest seed at which the two variants disagree.
    int minimalSteps{0};          ///< shortest input prefix that still reproduces it.
    int seedsChecked{0};
};

/// The shortest step prefix at @p seed for which @p a and @p b still disagree (they must
/// disagree at @p maxSteps). A linear scan from 1 - the divergence's first appearance.
inline int shrinkSteps(const SimDigestFn& a, const SimDigestFn& b, std::uint64_t seed, int maxSteps) {
    for (int s = 1; s <= maxSteps; ++s)
        if (a(seed, s) != b(seed, s)) return s;
    return maxSteps;
}

/// Scan seeds ascending for the first (hence minimal) seed where @p a and @p b diverge, then
/// shrink to the shortest failing input prefix. deterministic implementations report found==false.
inline DivergenceReport fuzzDivergence(const SimDigestFn& a, const SimDigestFn& b,
                                       std::uint64_t nSeeds, int steps) {
    DivergenceReport r;
    for (std::uint64_t s = 0; s < nSeeds; ++s) {
        ++r.seedsChecked;
        if (a(s, steps) != b(s, steps)) {
            r.found = true;
            r.minimalSeed = s;
            r.minimalSteps = shrinkSteps(a, b, s, steps);
            return r;
        }
    }
    return r;
}

} // namespace game
} // namespace IKore
