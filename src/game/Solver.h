#pragma once

#include "game/DungeonGame.h"  // DungeonGame, GameInput, loadGame, SceneDescription
#include "game/LevelFormat.h"  // fromLevelJson (validateLevelJson)

#include <algorithm>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

/**
 * @file Solver.h
 * @brief Deterministic solvability validator + auto-solver for levels (issue #316).
 *
 * Weekly challenges (#273) and seed-shared levels can be unfair (an unreachable exit
 * or coin). Because DungeonGame (#164) is fully deterministic, a search can prove a
 * level is clearable and report the optimal clear length (par), gating challenges and
 * leaderboards.
 *
 * The solver discretizes the walkable XZ space into a grid and runs a breadth-first
 * search over (cell, collected-coin bitmask), so it finds the shortest route that
 * collects every coin and reaches the exit. Crucially it reuses DungeonGame's own
 * rules: a cell is walkable iff game.hitsWall(center, playerRadius) is false (the same
 * collision), and a coin/exit is "reached" from a cell iff the center is within the
 * same pickup radius the game uses. So the solver and the game agree - the returned
 * path, driven as inputs, wins the actual game (exercised by the test).
 *
 * The chase enemy is ignored by default (static solvability); optionally it can be
 * treated as a fixed hazard at its start (enemiesAsHazard). Header-only, std-only,
 * deterministic.
 */
namespace IKore {
namespace game {

struct SolveOptions {
    float cellSize{0.5f};       ///< grid resolution in world units.
    int padCells{2};            ///< walkable margin added around the content AABB.
    bool enemiesAsHazard{false}; ///< block cells near an enemy's start position.
    int maxCoinsExact{16};      ///< above this, skip the exact bitmask search (report reachability only).
};

struct SolveResult {
    bool solvable{false};         ///< every coin collectible and the exit reachable after collecting them.
    int par{-1};                  ///< minimal number of grid steps to clear; -1 if unsolvable/unknown.
    int totalCoins{0};
    int reachableCoins{0};        ///< coins collectible from some cell reachable from the start.
    bool exitReachable{false};    ///< exit reachable from the start (ignoring coin gating).
    int unreachableObjectives{0}; ///< unreachable coins + (exit unreachable ? 1 : 0).
    std::vector<ecs::Vec3> path;  ///< cell centers from start to the winning cell (auto-solver output).
};

namespace detail {

struct SolverGrid {
    float minX{0.0f}, minZ{0.0f}, cellSize{0.5f};
    int nx{0}, nz{0};

    int index(int gx, int gz) const {
        if (gx < 0 || gz < 0 || gx >= nx || gz >= nz) return -1;
        return gz * nx + gx;
    }
    int cellOf(float x, float z) const {
        const int gx = static_cast<int>(std::floor((x - minX) / cellSize));
        const int gz = static_cast<int>(std::floor((z - minZ) / cellSize));
        return index(gx, gz);
    }
    ecs::Vec3 center(int idx) const {
        const int gx = idx % nx, gz = idx / nx;
        return ecs::Vec3{minX + (gx + 0.5f) * cellSize, 0.0f, minZ + (gz + 0.5f) * cellSize};
    }
};

inline SolverGrid buildGrid(const DungeonGame& g, const SolveOptions& opt) {
    float minX = g.playerPosition.x, maxX = g.playerPosition.x;
    float minZ = g.playerPosition.z, maxZ = g.playerPosition.z;
    auto grow = [&](const ecs::Vec3& p, float ext) {
        minX = std::min(minX, p.x - ext);
        maxX = std::max(maxX, p.x + ext);
        minZ = std::min(minZ, p.z - ext);
        maxZ = std::max(maxZ, p.z + ext);
    };
    for (const world::Box& b : g.walls) grow(b.center, 0.5f * std::max(std::fabs(b.size.x), std::fabs(b.size.z)));
    for (const Coin& c : g.coins) grow(c.position, 0.0f);
    for (const Enemy& e : g.enemies) grow(e.position, 0.0f);
    if (g.hasExit) grow(g.exitPosition, 0.0f);

    SolverGrid grid;
    grid.cellSize = opt.cellSize > 1e-4f ? opt.cellSize : 0.5f;
    grid.minX = minX - opt.padCells * grid.cellSize;
    grid.minZ = minZ - opt.padCells * grid.cellSize;
    grid.nx = static_cast<int>(std::ceil((maxX - minX) / grid.cellSize)) + 2 * opt.padCells + 1;
    grid.nz = static_cast<int>(std::ceil((maxZ - minZ) / grid.cellSize)) + 2 * opt.padCells + 1;
    grid.nx = std::max(grid.nx, 1);
    grid.nz = std::max(grid.nz, 1);
    return grid;
}

} // namespace detail

/**
 * @brief Prove a level clearable and return the optimal clear route.
 *
 * BFS over (cell, coin bitmask): the start cell seeds whatever coins are collectible
 * there; each move ORs in the neighbor's collectible coins; the goal is "all coins
 * collected and standing on a cell that reaches the exit". par is the number of grid
 * steps of that shortest route.
 */
inline SolveResult solve(const DungeonGame& game, const SolveOptions& opt = {}) {
    SolveResult res;
    res.totalCoins = static_cast<int>(game.coins.size());

    const detail::SolverGrid grid = detail::buildGrid(game, opt);
    const int cellCount = grid.nx * grid.nz;
    if (cellCount <= 0) return res;

    // Walkable mask (same collision as the game), plus optional enemy-hazard blocking.
    std::vector<char> walkable(static_cast<std::size_t>(cellCount), 0);
    for (int i = 0; i < cellCount; ++i) {
        const ecs::Vec3 c = grid.center(i);
        bool ok = !game.hitsWall(c, game.playerRadius);
        if (ok && opt.enemiesAsHazard) {
            for (const Enemy& e : game.enemies) {
                const float dx = c.x - e.position.x, dz = c.z - e.position.z;
                const float r = game.playerRadius + game.enemyRadius;
                if (dx * dx + dz * dz <= r * r) { ok = false; break; }
            }
        }
        walkable[static_cast<std::size_t>(i)] = ok ? 1 : 0;
    }

    // Per-cell collectible-coin bitmask and exit reachability.
    const int nCoins = res.totalCoins;
    std::vector<std::uint32_t> coinsAt(static_cast<std::size_t>(cellCount), 0);
    std::vector<char> exitAt(static_cast<std::size_t>(cellCount), 0);
    for (int i = 0; i < cellCount; ++i) {
        if (!walkable[static_cast<std::size_t>(i)]) continue;
        const ecs::Vec3 c = grid.center(i);
        for (int k = 0; k < nCoins; ++k) {
            const Coin& coin = game.coins[static_cast<std::size_t>(k)];
            const float dx = c.x - coin.position.x, dz = c.z - coin.position.z;
            const float r = game.playerRadius + game.coinRadius;
            if (dx * dx + dz * dz <= r * r) coinsAt[static_cast<std::size_t>(i)] |= (1u << k);
        }
        if (game.hasExit) {
            const float dx = c.x - game.exitPosition.x, dz = c.z - game.exitPosition.z;
            const float r = game.playerRadius + game.exitRadius;
            if (dx * dx + dz * dz <= r * r) exitAt[static_cast<std::size_t>(i)] = 1;
        }
    }

    const int start = grid.cellOf(game.playerPosition.x, game.playerPosition.z);
    if (start < 0 || !walkable[static_cast<std::size_t>(start)]) return res; // player boxed in

    // Plain reachability flood (for reporting reachable coins / exit, ignoring gating).
    {
        std::vector<char> seen(static_cast<std::size_t>(cellCount), 0);
        std::queue<int> q;
        q.push(start);
        seen[static_cast<std::size_t>(start)] = 1;
        std::uint32_t reachMask = 0;
        bool exitSeen = false;
        const int dxs[4] = {1, -1, 0, 0}, dzs[4] = {0, 0, 1, -1};
        while (!q.empty()) {
            const int cur = q.front();
            q.pop();
            reachMask |= coinsAt[static_cast<std::size_t>(cur)];
            if (exitAt[static_cast<std::size_t>(cur)]) exitSeen = true;
            const int gx = cur % grid.nx, gz = cur / grid.nx;
            for (int d = 0; d < 4; ++d) {
                const int nb = grid.index(gx + dxs[d], gz + dzs[d]);
                if (nb < 0 || seen[static_cast<std::size_t>(nb)] || !walkable[static_cast<std::size_t>(nb)]) continue;
                seen[static_cast<std::size_t>(nb)] = 1;
                q.push(nb);
            }
        }
        for (int k = 0; k < nCoins; ++k)
            if (reachMask & (1u << k)) ++res.reachableCoins;
        res.exitReachable = exitSeen;
        res.unreachableObjectives = (nCoins - res.reachableCoins) + (exitSeen ? 0 : 1);
    }

    if (nCoins > opt.maxCoinsExact) return res; // too many coins for the exact bitmask search

    // BFS over (cell, mask). State id = cell * 2^nCoins + mask.
    const std::uint32_t full = nCoins >= 32 ? 0xFFFFFFFFu : ((1u << nCoins) - 1u);
    const std::size_t maskCount = static_cast<std::size_t>(full) + 1;
    const std::size_t stateCount = static_cast<std::size_t>(cellCount) * maskCount;
    std::vector<int> dist(stateCount, -1);
    std::vector<int> parent(stateCount, -1);

    auto stateId = [&](int cell, std::uint32_t mask) {
        return static_cast<std::size_t>(cell) * maskCount + mask;
    };

    const std::uint32_t startMask = coinsAt[static_cast<std::size_t>(start)];
    const std::size_t s0 = stateId(start, startMask);
    dist[s0] = 0;
    std::queue<std::size_t> q;
    q.push(s0);
    std::size_t goalState = static_cast<std::size_t>(-1);
    const int dxs[4] = {1, -1, 0, 0}, dzs[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        const std::size_t cur = q.front();
        q.pop();
        const int cell = static_cast<int>(cur / maskCount);
        const std::uint32_t mask = static_cast<std::uint32_t>(cur % maskCount);
        if (mask == full && exitAt[static_cast<std::size_t>(cell)]) { goalState = cur; break; }
        const int gx = cell % grid.nx, gz = cell / grid.nx;
        for (int d = 0; d < 4; ++d) {
            const int nb = grid.index(gx + dxs[d], gz + dzs[d]);
            if (nb < 0 || !walkable[static_cast<std::size_t>(nb)]) continue;
            const std::uint32_t nmask = mask | coinsAt[static_cast<std::size_t>(nb)];
            const std::size_t ns = stateId(nb, nmask);
            if (dist[ns] >= 0) continue;
            dist[ns] = dist[cur] + 1;
            parent[ns] = static_cast<int>(cur);
            q.push(ns);
        }
    }

    if (goalState == static_cast<std::size_t>(-1)) return res; // unsolvable
    res.solvable = true;
    res.par = dist[goalState];

    // Reconstruct the winning route (cell centers, start -> goal).
    std::vector<int> cells;
    for (int s = static_cast<int>(goalState); s >= 0; s = parent[static_cast<std::size_t>(s)]) {
        cells.push_back(static_cast<int>(static_cast<std::size_t>(s) / maskCount));
        if (parent[static_cast<std::size_t>(s)] < 0) break;
    }
    std::reverse(cells.begin(), cells.end());
    for (int c : cells) res.path.push_back(grid.center(c));
    return res;
}

/// Validate an already-converted scene.
inline SolveResult validateLevel(const SceneDescription& scene, const SolveOptions& opt = {}) {
    return solve(loadGame(scene), opt);
}

/// Validate a level from its "doodle-level" JSON (convenience for gating).
inline SolveResult validateLevelJson(const std::string& json, const SolveOptions& opt = {}) {
    LevelSpec spec;
    if (!fromLevelJson(json, spec)) return SolveResult{};
    return solve(loadGame(convert(spec)), opt);
}

} // namespace game
} // namespace IKore
