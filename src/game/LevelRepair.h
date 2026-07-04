#pragma once

#include "game/DungeonGame.h" // DungeonGame, ecs::Vec3
#include "game/Solver.h"      // solve, SolveResult, SolveOptions, detail::buildGrid

#include <cmath>
#include <queue>
#include <string>
#include <vector>

/**
 * @file LevelRepair.h
 * @brief Auto-repair an unsolvable captured level with minimal, reported edits (issue #362).
 *
 * The Solver (#316) reports exactly why a level fails - an unreachable exit or coin. A
 * captured drawing often just misses playability that way. Rather than reject it, this
 * proposes the smallest fix: relocate each unreachable objective (the exit, or a coin) onto
 * the nearest cell the player can actually reach, guided by the same walkability the Solver
 * uses. Every change is reported so it is reviewable, not silent; an already-solvable level
 * is returned untouched; and the repair is deterministic and idempotent (repairing a
 * repaired level is a no-op). A suggest-only mode returns the edits without applying them.
 * Header-only, std only.
 */
namespace IKore {
namespace game {

struct RepairEdit {
    std::string what;   ///< "exit" or "coin".
    ecs::Vec3 from{};
    ecs::Vec3 to{};
};

struct RepairOptions {
    SolveOptions solve{};
    bool apply{true};   ///< false = suggest only (edits computed, level left unchanged).
};

struct RepairResult {
    bool wasSolvable{false}; ///< the input was already solvable.
    bool nowSolvable{false}; ///< solvable after the (proposed) edits.
    std::vector<RepairEdit> edits;
    DungeonGame level;       ///< repaired level (or the original if suggest-only / already ok).
};

namespace detail {

/// Cell centers reachable from the player start over the game's static-wall walkability.
inline std::vector<ecs::Vec3> reachableCenters(const DungeonGame& game, const SolveOptions& opt) {
    const SolverGrid grid = buildGrid(game, opt);
    const int cells = grid.nx * grid.nz;
    std::vector<ecs::Vec3> out;
    if (cells <= 0) return out;
    std::vector<char> walkable(static_cast<std::size_t>(cells), 0);
    for (int i = 0; i < cells; ++i)
        walkable[static_cast<std::size_t>(i)] = game.hitsWall(grid.center(i), game.playerRadius) ? 0 : 1;
    const int start = grid.cellOf(game.playerPosition.x, game.playerPosition.z);
    if (start < 0 || !walkable[static_cast<std::size_t>(start)]) return out;
    std::vector<char> seen(static_cast<std::size_t>(cells), 0);
    std::queue<int> q;
    q.push(start);
    seen[static_cast<std::size_t>(start)] = 1;
    const int dxs[4] = {1, -1, 0, 0}, dzs[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        const int cur = q.front();
        q.pop();
        out.push_back(grid.center(cur));
        const int gx = cur % grid.nx, gz = cur / grid.nx;
        for (int d = 0; d < 4; ++d) {
            const int nb = grid.index(gx + dxs[d], gz + dzs[d]);
            if (nb < 0 || seen[static_cast<std::size_t>(nb)] || !walkable[static_cast<std::size_t>(nb)]) continue;
            seen[static_cast<std::size_t>(nb)] = 1;
            q.push(nb);
        }
    }
    return out;
}

inline float dist2(const ecs::Vec3& a, const ecs::Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

/// The reachable cell nearest to @p p (assumes @p centers non-empty).
inline ecs::Vec3 nearestReachable(const std::vector<ecs::Vec3>& centers, const ecs::Vec3& p) {
    ecs::Vec3 best = centers.front();
    float bestD = dist2(best, p);
    for (const ecs::Vec3& c : centers) {
        const float d = dist2(c, p);
        if (d < bestD) { bestD = d; best = c; }
    }
    return best;
}

} // namespace detail

/**
 * @brief Repair @p base so every objective is reachable, moving each unreachable objective to
 *        the nearest reachable cell. Returns the edits made and the (optionally applied)
 *        result. An already-solvable level is returned unchanged.
 */
inline RepairResult repairLevel(const DungeonGame& base, const RepairOptions& opt = {}) {
    RepairResult res;
    res.level = base;
    const SolveResult sr0 = solve(base, opt.solve);
    if (sr0.solvable) {
        res.wasSolvable = true;
        res.nowSolvable = true;
        return res; // untouched
    }

    const std::vector<ecs::Vec3> reachable = detail::reachableCenters(base, opt.solve);
    if (reachable.empty()) return res; // player boxed in: nothing to relocate onto

    DungeonGame fixed = base;
    // Relocate an unreachable exit.
    if (fixed.hasExit && !sr0.exitReachable) {
        const ecs::Vec3 to = detail::nearestReachable(reachable, fixed.exitPosition);
        res.edits.push_back(RepairEdit{"exit", fixed.exitPosition, to});
        fixed.exitPosition = to;
    }
    // Relocate each unreachable coin (no reachable cell within its pickup radius).
    const float coinR = base.playerRadius + base.coinRadius;
    for (Coin& c : fixed.coins) {
        bool reachableCoin = false;
        for (const ecs::Vec3& center : reachable)
            if (detail::dist2(center, c.position) <= coinR * coinR) { reachableCoin = true; break; }
        if (!reachableCoin) {
            const ecs::Vec3 to = detail::nearestReachable(reachable, c.position);
            res.edits.push_back(RepairEdit{"coin", c.position, to});
            c.position = to;
        }
    }

    res.nowSolvable = solve(fixed, opt.solve).solvable;
    if (opt.apply) res.level = fixed; // suggest-only leaves res.level == base
    return res;
}

} // namespace game
} // namespace IKore
