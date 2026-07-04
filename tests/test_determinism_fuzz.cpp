// Determinism fuzzing at scale - issue #374.
//
// Drives the deterministic sim with large batches of seeded random input streams and asserts
// every run re-simulates to an identical digest; then injects a nondeterminism bug and shows
// the fuzzer catches it and reports a minimal repro (smallest seed + shortest failing input
// prefix). Pure std + the header-only game core:
//   g++ -std=c++17 -I src tests/test_determinism_fuzz.cpp -o test_determinism_fuzz

#include "game/DeterminismFuzz.h"

#include <cstdio>
#include <set>
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

// A representative playable arena: a walled box with coins, so random input produces varied,
// wall-colliding trajectories.
static DungeonGame arena() {
    SceneDescription s;
    auto wall = [&](float cx, float cz, float sx, float sz) {
        world::Box b;
        b.center = ecs::Vec3{cx, 0.0f, cz};
        b.size = ecs::Vec3{sx, 3.0f, sz};
        b.yaw = 0.0f;
        s.wallBoxes.push_back(b);
    };
    wall(0, 6, 12, 0.5f);
    wall(0, -6, 12, 0.5f);
    wall(6, 0, 0.5f, 12);
    wall(-6, 0, 0.5f, 12);
    wall(2, 2, 1, 1); // interior obstacles
    wall(-3, -1, 1, 1);
    s.spawns.push_back(EntitySpawn{"player", ecs::Vec3{0, 0, 0}, 0.0f});
    s.spawns.push_back(EntitySpawn{"coin", ecs::Vec3{3, 3, 0}, 0.0f});
    s.spawns.push_back(EntitySpawn{"coin", ecs::Vec3{-3, 3, 0}, 0.0f});
    s.spawns.push_back(EntitySpawn{"exit", ecs::Vec3{4, -4, 0}, 0.0f});
    return loadGame(s);
}

int main() {
    const DungeonGame base = arena();

    // 1. The digest is stable and responds to the seed (the sim actually varies with input).
    {
        CHECK(simulateDigest(base, 42, 200) == simulateDigest(base, 42, 200));
        std::set<std::uint64_t> digests;
        for (std::uint64_t s = 0; s < 10; ++s) digests.insert(simulateDigest(base, s, 200));
        CHECK(digests.size() >= 5); // distinct seeds -> mostly distinct trajectories
    }

    // 2. A large seeded batch re-simulates to identical digests (determinism at scale).
    {
        const FuzzReport r = fuzzDeterminism(base, /*nSeeds=*/500, /*steps=*/300);
        std::printf("[info] fuzzDeterminism: %d seeds checked, deterministic=%d\n", r.seedsChecked,
                    r.deterministic ? 1 : 0);
        CHECK(r.seedsChecked == 500);
        CHECK(r.deterministic);
    }

    // 3. A deterministic pair never diverges (no false positive).
    {
        const SimDigestFn ref = [&](std::uint64_t seed, int steps) {
            return simulateDigest(base, seed, steps);
        };
        const DivergenceReport r = fuzzDivergence(ref, ref, 500, 300);
        CHECK(!r.found);
    }

    // 4. An injected nondeterminism is caught with a minimal repro (smallest seed + shortest
    //    failing input prefix). The bug perturbs only seeds divisible by 13 (>0), and only
    //    once the trajectory reaches 5 steps.
    {
        const SimDigestFn ref = [&](std::uint64_t seed, int steps) {
            return simulateDigest(base, seed, steps);
        };
        const SimDigestFn buggy = [&](std::uint64_t seed, int steps) {
            std::uint64_t h = simulateDigest(base, seed, steps);
            if (seed % 13 == 0 && seed > 0 && steps >= 5) h ^= 0x9E3779B97F4A7C15ULL;
            return h;
        };
        const DivergenceReport r = fuzzDivergence(ref, buggy, 500, 300);
        std::printf("[info] fuzzDivergence: found=%d minimalSeed=%llu minimalSteps=%d\n",
                    r.found ? 1 : 0, static_cast<unsigned long long>(r.minimalSeed), r.minimalSteps);
        CHECK(r.found);
        CHECK(r.minimalSeed == 13);  // smallest divergent seed (ascending scan)
        CHECK(r.minimalSteps == 5);  // shortest input prefix that reproduces it
    }

    if (g_failures == 0) {
        std::printf("test_determinism_fuzz: all checks passed\n");
        return 0;
    }
    std::printf("test_determinism_fuzz: %d check(s) failed\n", g_failures);
    return 1;
}
