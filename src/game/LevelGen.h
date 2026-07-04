#pragma once

#include "game/DoodleScene.h" // LevelSpec, Wall, Symbol, convert
#include "game/Solver.h"      // solve, SolveResult, loadGame

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

/**
 * @file LevelGen.h
 * @brief Solver-as-oracle procedural level generator (issue #348).
 *
 * Generates guaranteed-clearable levels for endless / daily modes by using the Solver
 * (#316) as an oracle: it lays out a bordered grid with random interior wall cells and
 * random player / exit / coin / enemy placements, then keeps a candidate only if solve()
 * proves it clearable (and, optionally, its par falls in a requested window). It retries
 * with a fresh deterministic layout until one passes or the attempt budget runs out.
 *
 * Determinism: a std::mt19937 seeded per (seed, attempt) drives every choice, and only its
 * raw output reduced with %% is used (never std::*_distribution, whose results vary across
 * standard libraries), so the same seed yields the same level on any platform. Difficulty
 * knobs (grid size, coins, enemies, and an explicit par window) shape the result. Walls are
 * emitted as one-cell segments (LevelSpec wallThickness == cell size), so a wall cell is a
 * solid box - the same grid model the Solver walks. Header-only.
 */
namespace IKore {
namespace game {

struct GenParams {
    int gridW{12};            ///< interior width in cells.
    int gridH{9};             ///< interior height in cells.
    int coins{3};             ///< coin objectives to place.
    int enemies{0};           ///< enemies to place (dynamic; do not affect static par).
    float wallDensity{0.15f}; ///< fraction of interior cells turned into wall obstacles.
    int minPar{-1};           ///< reject solvable levels with par below this (-1 = no bound).
    int maxPar{-1};           ///< reject solvable levels with par above this (-1 = no bound).
    float cellWorld{2.0f};    ///< world units per cell.
    int maxAttempts{300};     ///< layouts to try before giving up.
};

struct GenResult {
    bool ok{false};       ///< a solvable level within the constraints was found.
    LevelSpec spec;       ///< the generated level (walls + objective symbols).
    SolveResult solve;    ///< the solver proof (par, reachability, route).
    int attempts{0};      ///< layouts tried (1-based) to produce this result.
    unsigned seedUsed{0};
};

namespace detail {

/// Deterministic RNG: only raw mt19937 output reduced with %% (portable across stdlibs).
struct GenRng {
    std::mt19937 g;
    explicit GenRng(unsigned s) : g(s) {}
    int nextInt(int n) { return n <= 0 ? 0 : static_cast<int>(g() % static_cast<unsigned>(n)); }
};

inline unsigned mixSeed(unsigned seed, unsigned attempt) {
    return seed ^ (0x9E3779B9u * (attempt + 1u));
}

/// A LevelSpec whose walls fill whole cells: each wall cell is a one-cell-long segment and
/// wallThickness == cellWorld, so convert() extrudes it into a cellWorld-cubed box.
inline LevelSpec buildSpec(int gw, int gh, float cw, const std::vector<char>& obstacle,
                           int playerCell, int exitCell, const std::vector<int>& coinCells,
                           const std::vector<int>& enemyCells) {
    LevelSpec spec;
    spec.wallHeight = 3.0f;
    spec.wallThickness = cw;
    auto center = [&](int cx, int cz) { return ecs::Vec3{cx * cw, 0.0f, cz * cw}; };
    auto addWallCell = [&](int cx, int cz) {
        const ecs::Vec3 c = center(cx, cz);
        Wall w;
        w.polyline = {ecs::Vec3{c.x - cw * 0.5f, 0.0f, c.z}, ecs::Vec3{c.x + cw * 0.5f, 0.0f, c.z}};
        spec.walls.push_back(w);
    };
    // Border ring.
    for (int x = 0; x <= gw + 1; ++x) { addWallCell(x, 0); addWallCell(x, gh + 1); }
    for (int y = 1; y <= gh; ++y) { addWallCell(0, y); addWallCell(gw + 1, y); }
    // Interior obstacles.
    for (int i = 0; i < static_cast<int>(obstacle.size()); ++i)
        if (obstacle[i]) addWallCell(i % gw + 1, i / gw + 1);
    // Objectives (interior cell index -> world center).
    auto interior = [&](int i) { return center(i % gw + 1, i / gw + 1); };
    spec.symbols.push_back(Symbol{"player", interior(playerCell), 0.0f});
    spec.symbols.push_back(Symbol{"exit", interior(exitCell), 0.0f});
    for (int c : coinCells) spec.symbols.push_back(Symbol{"coin", interior(c), 0.0f});
    for (int c : enemyCells) spec.symbols.push_back(Symbol{"enemy", interior(c), 0.0f});
    return spec;
}

} // namespace detail

/**
 * @brief Generate a guaranteed-clearable level for @p seed under @p p.
 *
 * Returns the first layout the Solver proves solvable (all coins reachable and, if a par
 * window is set, par within it). ok is false if none passes within maxAttempts.
 */
inline GenResult generateLevel(unsigned seed, const GenParams& p = {}) {
    GenResult res;
    res.seedUsed = seed;
    const int gw = std::max(p.gridW, 2), gh = std::max(p.gridH, 2);
    const float cw = p.cellWorld > 1e-4f ? p.cellWorld : 2.0f;
    const int interior = gw * gh;
    const int coins = std::max(0, p.coins);
    const int enemies = std::max(0, p.enemies);
    const int needed = 2 + coins + enemies; // player + exit + coins + enemies
    const int attempts = std::max(1, p.maxAttempts);

    for (int attempt = 0; attempt < attempts; ++attempt) {
        detail::GenRng rng(detail::mixSeed(seed, static_cast<unsigned>(attempt)));

        // Interior obstacles (kept clear of the objectives, which are chosen after).
        int obstacleCount = static_cast<int>(p.wallDensity * static_cast<float>(interior));
        obstacleCount = std::min(obstacleCount, std::max(0, interior - needed - 2));
        std::vector<char> obstacle(static_cast<std::size_t>(interior), 0);
        for (int i = 0; i < obstacleCount; ++i) obstacle[static_cast<std::size_t>(rng.nextInt(interior))] = 1;

        std::vector<int> pool;
        for (int i = 0; i < interior; ++i)
            if (!obstacle[static_cast<std::size_t>(i)]) pool.push_back(i);
        if (static_cast<int>(pool.size()) < needed) continue;

        auto take = [&]() {
            const int idx = rng.nextInt(static_cast<int>(pool.size()));
            const int cell = pool[static_cast<std::size_t>(idx)];
            pool[static_cast<std::size_t>(idx)] = pool.back();
            pool.pop_back();
            return cell;
        };
        const int playerCell = take();
        const int exitCell = take();
        std::vector<int> coinCells;
        for (int i = 0; i < coins; ++i) coinCells.push_back(take());
        std::vector<int> enemyCells;
        for (int i = 0; i < enemies; ++i) enemyCells.push_back(take());

        LevelSpec spec = detail::buildSpec(gw, gh, cw, obstacle, playerCell, exitCell, coinCells, enemyCells);
        const SolveResult sr = solve(loadGame(convert(spec)));
        if (!sr.solvable) continue;
        if (sr.reachableCoins != coins) continue;
        if (p.minPar >= 0 && sr.par < p.minPar) continue;
        if (p.maxPar >= 0 && sr.par > p.maxPar) continue;

        res.ok = true;
        res.spec = std::move(spec);
        res.solve = sr;
        res.attempts = attempt + 1;
        return res;
    }
    res.attempts = attempts;
    return res;
}

} // namespace game
} // namespace IKore
