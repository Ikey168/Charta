#pragma once

/**
 * @file StarRating.h
 * @brief Deterministic 1-3 star ratings from a run's clear time versus solver par (#346).
 *
 * The Solver (#316) reports the optimal clear length "par" as a count of grid steps. A run
 * earns stars by how close its clear time comes to that optimum: a near-par run gets 3
 * stars, progressively slower runs get 2 then 1. To compare a wall-time clear against a
 * grid-step par, par is first turned into an ideal traversal time - the time to walk the
 * optimal path at full speed: parTime = par * cellSize / playerSpeed - using the solver's
 * cell size (#316 default 0.5) and the game's player speed (DungeonGame default 4.0). The
 * ratio clearTime / parTime then drives the star thresholds.
 *
 * Header-only, std-free, deterministic: same inputs always yield the same stars.
 */
namespace IKore {
namespace game {

/// Tunable star thresholds and the par-time conversion constants.
struct StarConfig {
    double three{1.20};      ///< clearTime <= parTime * three  -> 3 stars (near-optimal).
    double two{2.00};        ///< clearTime <= parTime * two    -> 2 stars; slower -> 1.
    double cellSize{0.5};    ///< solver grid cell size used to compute par (SolveOptions default).
    double playerSpeed{4.0}; ///< player speed used to convert path length to time (DungeonGame default).
};

/// Ideal traversal time for an integer solver @p par (grid steps); 0 if par is unknown.
inline double parTime(int par, const StarConfig& cfg = {}) {
    if (par <= 0 || cfg.playerSpeed <= 0.0) return 0.0;
    return (static_cast<double>(par) * cfg.cellSize) / cfg.playerSpeed;
}

/**
 * @brief Stars (1-3) earned by a cleared run.
 * @param clearTime the run's clear time (seconds), e.g. RunResult::clearTime.
 * @param par       the solver par (grid steps) for the level.
 * @return 3 for a near-par run, 2 then 1 as it slows; 0 when par or clearTime is undefined.
 */
inline int starsForClear(double clearTime, int par, const StarConfig& cfg = {}) {
    const double pt = parTime(par, cfg);
    if (pt <= 0.0 || clearTime <= 0.0) return 0;
    const double ratio = clearTime / pt;
    if (ratio <= cfg.three) return 3;
    if (ratio <= cfg.two) return 2;
    return 1;
}

} // namespace game
} // namespace IKore
