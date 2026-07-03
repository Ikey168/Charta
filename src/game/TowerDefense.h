#pragma once

#include "cv/Topology.h"      // buildTopology, Topology, Doorway (room/doorway graph, #168)
#include "game/DoodleScene.h" // LevelSpec, Symbol, Wall (the doodle-level data)

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file TowerDefense.h
 * @brief Headless, deterministic tower-defense mode on the room topology (issue #271).
 *
 * A launch game mode from PHONE_GAME_CONCEPT.md: enemies path through the rooms you
 * drew. It reuses the existing substrate rather than a bespoke format:
 *   - the room/doorway connectivity graph from the walls (cv::buildTopology, #168),
 *   - the doodle-level JSON with placeable symbols (entry / goal / tower), and
 *   - the deterministic, caller-timestepped sim pattern (DungeonGame.h, #164) so runs
 *     are replay-verifiable the way leaderboards re-simulate them (#242).
 *
 * loadTowerDefense() turns a LevelSpec into a playable board: it builds the topology,
 * maps the entry/goal/tower symbols to rooms, and BFS-routes the enemy path entry ->
 * goal through the doorway graph (waypoints are the doorways between consecutive rooms,
 * so enemies actually walk through the gaps you drew). update(dt) then spawns waves,
 * advances enemies along the path, lets towers damage enemies in range, and resolves
 * win/lose from lives and cleared waves. All randomness comes from a caller-supplied
 * seed via a splitmix64 PRNG, and there is no wall clock, so the same level + config +
 * dt sequence yields the identical outcome on any instance. Header-only, std only.
 */
namespace IKore {
namespace game {

/// splitmix64: a tiny, high-quality deterministic PRNG. Same seed -> same stream, so
/// seeded runs replay identically (matching the rest of the sim's determinism).
struct TdRng {
    std::uint64_t state{0};
    explicit TdRng(std::uint64_t seed = 0) : state(seed) {}
    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    /// Uniform float in [0, 1).
    float unit() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
};

/// One wave: how many enemies, the gap between spawns, and their stats.
struct TdWave {
    int count{5};
    float interval{1.0f};  ///< seconds between spawns
    float enemyHp{20.0f};
    float enemySpeed{2.0f};
    float bounty{5.0f};    ///< currency granted per kill
};

/// Board configuration. `seed` + `hpJitter` are the only source of randomness.
struct TdConfig {
    float gridSize{10.0f};       ///< world units per topology cell (matches the level scale)
    int gridPad{1};              ///< rasterize padding (matches cv::rasterizeWalls default)
    int startingLives{10};
    float startingCurrency{0.0f};
    float towerRange{20.0f};     ///< default tower range if a symbol carries none
    float towerDps{10.0f};       ///< default tower damage per second
    float hpJitter{0.0f};        ///< fractional enemy-hp variance in [0,1); 0 = deterministic baseline
    std::uint64_t seed{0};
    std::vector<TdWave> waves;
};

struct TdEnemy {
    ecs::Vec3 position{};
    float hp{0.0f};
    float maxHp{0.0f};
    float speed{0.0f};
    float bounty{0.0f};
    int waypoint{1};       ///< index of the waypoint being walked toward
    bool alive{true};
    bool reachedGoal{false};
};

struct TdTower {
    ecs::Vec3 position{};
    float range{0.0f};
    float dps{0.0f};
};

enum class TdStatus { Playing, Won, Lost };

/// A self-contained, headless tower-defense game built from a doodle level.
struct TowerDefense {
    // Board (populated by loadTowerDefense).
    bool valid{false};
    std::vector<int> roomPath;       ///< room ids traversed, entry room -> goal room
    std::vector<ecs::Vec3> path;     ///< world-space waypoints (entry, doorways..., goal)
    std::vector<TdTower> towers;

    // Runtime.
    TdConfig config;
    TdRng rng{0};
    std::vector<TdEnemy> enemies;
    int lives{0};
    float currency{0.0f};
    TdStatus status{TdStatus::Playing};
    std::size_t waveIndex{0};
    int spawnedInWave{0};
    float spawnTimer{0.0f};
    float elapsed{0.0f};
    int enemiesKilled{0};
    int enemiesLeaked{0};

    bool won() const { return status == TdStatus::Won; }
    bool lost() const { return status == TdStatus::Lost; }
    bool finished() const { return status != TdStatus::Playing; }
    int aliveEnemies() const {
        int n = 0;
        for (const TdEnemy& e : enemies) {
            if (e.alive) ++n;
        }
        return n;
    }

    /**
     * @brief Advance the game by @p dt seconds. A no-op once finished.
     *
     * Order is fixed for determinism: spawn due enemies, move enemies along the path
     * (a reached-goal enemy costs a life), towers damage the nearest in-range enemy,
     * then resolve win/lose. Waves advance once fully spawned and cleared; the game is
     * won when the last wave is cleared with lives remaining and lost at zero lives.
     */
    void update(float dt) {
        if (status != TdStatus::Playing || !valid) return;
        elapsed += dt;

        // 1. Spawn the current wave at intervals.
        if (waveIndex < config.waves.size()) {
            const TdWave& wave = config.waves[waveIndex];
            spawnTimer -= dt;
            while (spawnedInWave < wave.count && spawnTimer <= 0.0f) {
                spawnEnemy(wave);
                ++spawnedInWave;
                spawnTimer += wave.interval;
            }
            // Advance to the next wave only when this one is fully spawned and cleared.
            if (spawnedInWave >= wave.count && aliveEnemies() == 0) {
                ++waveIndex;
                spawnedInWave = 0;
                spawnTimer = 0.0f;
            }
        }

        // 2. Move enemies along the path; a reached-goal enemy leaks (costs a life).
        for (TdEnemy& e : enemies) {
            if (!e.alive) continue;
            advanceEnemy(e, dt);
            if (e.reachedGoal) {
                e.alive = false;
                ++enemiesLeaked;
                --lives;
                if (lives <= 0) {
                    lives = 0;
                    status = TdStatus::Lost;
                    return;
                }
            }
        }

        // 3. Towers damage the nearest alive enemy in range.
        for (const TdTower& tower : towers) {
            TdEnemy* target = nearestEnemyInRange(tower);
            if (target != nullptr) {
                target->hp -= tower.dps * dt;
                if (target->hp <= 0.0f) {
                    target->alive = false;
                    currency += target->bounty;
                    ++enemiesKilled;
                }
            }
        }

        // 4. Win once every wave is spawned and cleared with lives remaining.
        if (waveIndex >= config.waves.size() && aliveEnemies() == 0 && lives > 0) {
            status = TdStatus::Won;
        }
    }

private:
    void spawnEnemy(const TdWave& wave) {
        TdEnemy e;
        e.position = path.front();
        float hp = wave.enemyHp;
        if (config.hpJitter > 0.0f) {
            // Deterministic per-enemy hp variance in [1 - jitter, 1 + jitter).
            hp *= 1.0f + config.hpJitter * (rng.unit() * 2.0f - 1.0f);
        }
        e.hp = hp;
        e.maxHp = hp;
        e.speed = wave.enemySpeed;
        e.bounty = wave.bounty;
        e.waypoint = 1; // heading toward the second waypoint (index 0 is the spawn)
        e.alive = true;
        enemies.push_back(e);
    }

    void advanceEnemy(TdEnemy& e, float dt) {
        float remaining = e.speed * dt;
        while (remaining > 0.0f && e.waypoint < static_cast<int>(path.size())) {
            const ecs::Vec3& target = path[static_cast<std::size_t>(e.waypoint)];
            const float dx = target.x - e.position.x;
            const float dz = target.z - e.position.z;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist <= remaining) {
                e.position = target;
                remaining -= dist;
                ++e.waypoint;
                if (e.waypoint >= static_cast<int>(path.size())) {
                    e.reachedGoal = true;
                    return;
                }
            } else {
                e.position.x += (dx / dist) * remaining;
                e.position.z += (dz / dist) * remaining;
                remaining = 0.0f;
            }
        }
    }

    TdEnemy* nearestEnemyInRange(const TdTower& tower) {
        TdEnemy* best = nullptr;
        float bestDist = tower.range;
        for (TdEnemy& e : enemies) {
            if (!e.alive) continue;
            const float dx = e.position.x - tower.position.x;
            const float dz = e.position.z - tower.position.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d <= bestDist) {
                bestDist = d;
                best = &e;
            }
        }
        return best;
    }
};

namespace detail {

/// Grid cell (padded topology space) for a world XZ position, matching rasterizeWalls.
inline void worldToCell(float worldX, float worldZ, float gridSize, int pad, int& cx, int& cy) {
    cx = static_cast<int>(std::lround(worldX / gridSize)) + pad;
    cy = static_cast<int>(std::lround(worldZ / gridSize)) + pad;
}

/// World XZ center of a padded grid cell (inverse of worldToCell for cell centers).
inline ecs::Vec3 cellToWorld(float cx, float cy, float gridSize, int pad) {
    return ecs::Vec3{(cx - static_cast<float>(pad)) * gridSize, 0.0f,
                     (cy - static_cast<float>(pad)) * gridSize};
}

/// Room id at a symbol's position, or -1 if it sits on a wall / doorway / outside.
inline int roomAtSymbol(const cv::Topology& topo, const Symbol& s, float gridSize, int pad) {
    int cx = 0, cy = 0;
    worldToCell(s.position.x, s.position.z, gridSize, pad, cx, cy);
    return topo.labelAt(cx, cy);
}

/// BFS shortest room path (by doorway hops) from @p start to @p goal, inclusive.
/// Empty if unreachable. Uses the topology's undirected doorway connections.
inline std::vector<int> bfsRoomPath(const cv::Topology& topo, int start, int goal) {
    if (start < 0 || goal < 0) return {};
    if (start == goal) return {start};
    const std::size_t n = topo.rooms.size();
    std::vector<std::vector<int>> adj(n);
    for (const std::pair<int, int>& c : topo.connections) {
        adj[static_cast<std::size_t>(c.first)].push_back(c.second);
        adj[static_cast<std::size_t>(c.second)].push_back(c.first);
    }
    std::vector<int> parent(n, -2);
    parent[static_cast<std::size_t>(start)] = -1;
    std::vector<int> queue{start};
    std::size_t head = 0;
    while (head < queue.size()) {
        const int cur = queue[head++];
        if (cur == goal) break;
        for (int nb : adj[static_cast<std::size_t>(cur)]) {
            if (parent[static_cast<std::size_t>(nb)] == -2) {
                parent[static_cast<std::size_t>(nb)] = cur;
                queue.push_back(nb);
            }
        }
    }
    if (parent[static_cast<std::size_t>(goal)] == -2) return {};
    std::vector<int> pathRev;
    for (int at = goal; at != -1; at = parent[static_cast<std::size_t>(at)]) {
        pathRev.push_back(at);
    }
    return std::vector<int>(pathRev.rbegin(), pathRev.rend());
}

/// The doorway cell linking rooms @p a and @p b (world center), if one exists.
inline bool doorwayBetween(const cv::Topology& topo, int a, int b, float gridSize, int pad,
                           ecs::Vec3& out) {
    for (const cv::Doorway& d : topo.doorways) {
        const bool match = (d.roomA == a && d.roomB == b) || (d.roomA == b && d.roomB == a);
        if (match) {
            out = cellToWorld(static_cast<float>(d.x), static_cast<float>(d.y), gridSize, pad);
            return true;
        }
    }
    return false;
}

/// Whether a symbol type names a tower / entry / goal (case-sensitive, a few aliases).
inline bool isTowerSymbol(const std::string& t) { return t == "tower" || t == "td_tower"; }
inline bool isEntrySymbol(const std::string& t) {
    return t == "entry" || t == "td_entry" || t == "spawn" || t == "start" || t == "player";
}
inline bool isGoalSymbol(const std::string& t) { return t == "goal" || t == "td_goal" || t == "exit"; }

} // namespace detail

/**
 * @brief Build a tower-defense board from a doodle level.
 *
 * Rasterizes the walls into the room/doorway topology, maps the entry, goal, and tower
 * symbols to rooms, and BFS-routes the enemy path entry -> goal through the doorways.
 * The result is valid() only when an entry symbol, a goal symbol, and a room path
 * between them all exist. Towers keep their per-symbol range/dps when provided (via
 * config defaults here, since the base Symbol carries no stats).
 */
inline TowerDefense loadTowerDefense(const LevelSpec& spec, const TdConfig& config) {
    TowerDefense td;
    td.config = config;
    td.rng = TdRng(config.seed);
    td.lives = config.startingLives;
    td.currency = config.startingCurrency;

    // Walls -> topology.
    std::vector<cv::Polyline> polylines;
    polylines.reserve(spec.walls.size());
    for (const Wall& w : spec.walls) {
        cv::Polyline pl;
        pl.reserve(w.polyline.size());
        for (const ecs::Vec3& v : w.polyline) pl.push_back(cv::Point{v.x, v.z});
        polylines.push_back(std::move(pl));
    }
    const cv::Topology topo = cv::buildTopology(polylines, config.gridSize, config.gridPad);

    // Map symbols to rooms.
    const Symbol* entry = nullptr;
    const Symbol* goal = nullptr;
    for (const Symbol& s : spec.symbols) {
        if (detail::isEntrySymbol(s.type) && entry == nullptr) entry = &s;
        else if (detail::isGoalSymbol(s.type) && goal == nullptr) goal = &s;
        else if (detail::isTowerSymbol(s.type)) {
            td.towers.push_back(TdTower{s.position, config.towerRange, config.towerDps});
        }
    }
    if (entry == nullptr || goal == nullptr) return td; // invalid: missing endpoints

    const int entryRoom = detail::roomAtSymbol(topo, *entry, config.gridSize, config.gridPad);
    const int goalRoom = detail::roomAtSymbol(topo, *goal, config.gridSize, config.gridPad);
    td.roomPath = detail::bfsRoomPath(topo, entryRoom, goalRoom);
    if (td.roomPath.empty()) return td; // invalid: no route through the doorways

    // Waypoints: entry position, then each doorway between consecutive rooms, then goal.
    td.path.push_back(entry->position);
    for (std::size_t i = 0; i + 1 < td.roomPath.size(); ++i) {
        ecs::Vec3 door;
        if (detail::doorwayBetween(topo, td.roomPath[i], td.roomPath[i + 1], config.gridSize,
                                   config.gridPad, door)) {
            td.path.push_back(door);
        }
    }
    td.path.push_back(goal->position);

    td.valid = true;
    return td;
}

/**
 * @brief Run a board to completion at a fixed timestep and return it (replay helper).
 * @param maxSteps Safety cap so a stalemate cannot loop forever.
 *
 * Deterministic: the same spec + config (seed) + dt yields the identical final state,
 * so a client and a server agree, exactly like replayRun for the dungeon mode (#242).
 */
inline TowerDefense simulateTowerDefense(const LevelSpec& spec, const TdConfig& config, float dt,
                                         int maxSteps = 100000) {
    TowerDefense td = loadTowerDefense(spec, config);
    if (!td.valid) return td;
    for (int i = 0; i < maxSteps && !td.finished(); ++i) td.update(dt);
    return td;
}

} // namespace game
} // namespace IKore
