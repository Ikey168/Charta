#pragma once

#include "game/Solver.h" // solve, SolveResult (and transitively DungeonGame / LevelFormat / DoodleScene)

#include <string>

/**
 * @file Fairness.h
 * @brief Solver-backed fairness check for published/competitive levels (issue #334).
 *
 * The deterministic solver (#316) can prove a level clearable and report its optimal par.
 * This wraps it as a one-call fairness verdict so the UGC / competitive paths - the
 * leaderboard (#242) and the weekly challenge (#273) - can refuse a level that cannot be
 * cleared (an unreachable exit or coin) and record its par as the reference time.
 *
 * Header-only, deterministic, std only.
 */
namespace IKore {
namespace game {

struct FairnessResult {
    bool fair{false};             ///< the level is clearable (every coin collectible, exit reachable).
    int par{-1};                  ///< optimal clear length in grid steps (-1 if not fair).
    int totalCoins{0};
    int reachableCoins{0};
    bool exitReachable{false};
    int unreachableObjectives{0}; ///< unreachable coins + (exit unreachable ? 1 : 0).
    std::string reason;           ///< why an unfair level was rejected ("" when fair).
};

/// Decide whether @p levelJson is fair (clearable), reporting par and what is unreachable.
inline FairnessResult checkLevelFairness(const std::string& levelJson, const SolveOptions& opt = {}) {
    FairnessResult r;
    LevelSpec spec;
    if (!fromLevelJson(levelJson, spec)) {
        r.reason = "unparseable level";
        return r;
    }
    const SolveResult s = solve(loadGame(convert(spec)), opt);
    r.par = s.par;
    r.totalCoins = s.totalCoins;
    r.reachableCoins = s.reachableCoins;
    r.exitReachable = s.exitReachable;
    r.unreachableObjectives = s.unreachableObjectives;
    if (!s.solvable) {
        r.reason = !s.exitReachable ? "exit unreachable" : "unreachable coin(s)";
        return r;
    }
    r.fair = true;
    return r;
}

} // namespace game
} // namespace IKore
