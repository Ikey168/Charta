#pragma once

#include "game/DoodleScene.h"

#include <cmath>
#include <string>
#include <vector>

/**
 * @file DungeonGame.h
 * @brief Playable top-down dungeon loop over a converted scene (issue #164).
 *
 * Milestone 15 vertical slice. Wires a SceneDescription (#162/#163) into a
 * deterministic, headless gameplay loop so the core risk - "is moving through a
 * generated floor plan a real game loop?" - can be exercised end to end without a
 * renderer: move (with wall collision), collect coins, avoid an enemy, reach the
 * exit to win.
 *
 * It implements the gameplay rules (circle-vs-oriented-box wall collision with
 * axis sliding, coin pickup, a chase enemy, win/lose) on plain data, so it is
 * unit-testable. The engine's Bullet collision, AIComponent/AISystem, and renderer
 * are the integration surface that would drive the same rules in the live build.
 *
 * Header-only and dependency-free (std + the header-only ECS / world / game types).
 */
namespace IKore {
namespace game {

enum class GameStatus { Playing, Won, Lost };

/// Desired move direction for a frame (any magnitude; normalized internally).
struct GameInput {
    float moveX{0.0f};
    float moveZ{0.0f};

    /// Exact equality, so a rollback session can detect a mispredicted input (#291).
    bool operator==(const GameInput& o) const { return moveX == o.moveX && moveZ == o.moveZ; }
    bool operator!=(const GameInput& o) const { return !(*this == o); }
};

struct Coin {
    ecs::Vec3 position{};
    bool collected{false};
};

struct Enemy {
    ecs::Vec3 position{};
};

/// A self-contained, headless dungeon game built from a converted scene.
struct DungeonGame {
    // Geometry + actors (populated by loadGame).
    std::vector<world::Box> walls;
    ecs::Vec3 playerPosition{};
    std::vector<Coin> coins;
    std::vector<Enemy> enemies;
    ecs::Vec3 exitPosition{};
    bool hasExit{false};

    // Tunables.
    float playerSpeed{4.0f};
    float enemySpeed{2.0f};
    float playerRadius{0.4f};
    float enemyRadius{0.4f};
    float coinRadius{0.4f};
    float exitRadius{0.6f};

    // Runtime.
    GameStatus status{GameStatus::Playing};
    int coinsCollected{0};
    int totalCoins{0};

    int coinsRemaining() const { return totalCoins - coinsCollected; }
    bool won() const { return status == GameStatus::Won; }
    bool lost() const { return status == GameStatus::Lost; }

    /**
     * @brief Advance the game by @p dt seconds given @p in.
     *
     * Moves the player (sliding along walls), collects touched coins, advances the
     * chase enemies, and resolves the lose (caught) and win (all coins collected
     * and standing on the exit) conditions. A no-op once the game is over.
     */
    void update(const GameInput& in, float dt) {
        if (status != GameStatus::Playing) return;

        // Player movement with wall collision (axis slide).
        const float len = std::sqrt(in.moveX * in.moveX + in.moveZ * in.moveZ);
        if (len > 1e-6f) {
            const float step = playerSpeed * dt;
            playerPosition =
                slideMove(playerPosition, (in.moveX / len) * step, (in.moveZ / len) * step, playerRadius);
        }

        // Coin pickup.
        for (Coin& c : coins) {
            if (!c.collected && within(playerPosition, c.position, playerRadius + coinRadius)) {
                c.collected = true;
                ++coinsCollected;
            }
        }

        // Enemies chase the player; contact is a loss.
        for (Enemy& e : enemies) {
            const float dx = playerPosition.x - e.position.x;
            const float dz = playerPosition.z - e.position.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d > 1e-6f) {
                const float step = enemySpeed * dt;
                e.position = slideMove(e.position, (dx / d) * step, (dz / d) * step, enemyRadius);
            }
            if (within(playerPosition, e.position, playerRadius + enemyRadius)) {
                status = GameStatus::Lost;
                return;
            }
        }

        // Win: every coin collected and standing on the exit.
        if (coinsCollected >= totalCoins && hasExit &&
            within(playerPosition, exitPosition, playerRadius + exitRadius)) {
            status = GameStatus::Won;
        }
    }

    /// True if a circle of @p radius at @p pos overlaps any wall box.
    bool hitsWall(const ecs::Vec3& pos, float radius) const {
        for (const world::Box& b : walls) {
            if (circleHitsBox(pos, radius, b)) return true;
        }
        return false;
    }

private:
    static bool within(const ecs::Vec3& a, const ecs::Vec3& b, float radius) {
        const float dx = a.x - b.x, dz = a.z - b.z;
        return dx * dx + dz * dz <= radius * radius;
    }

    /// Circle (XZ) vs oriented box footprint overlap.
    static bool circleHitsBox(const ecs::Vec3& center, float radius, const world::Box& box) {
        const float cs = std::cos(box.yaw), sn = std::sin(box.yaw);
        // Transform the circle center into the box's local (un-rotated) frame.
        const float rx = center.x - box.center.x;
        const float rz = center.z - box.center.z;
        const float lx = rx * cs + rz * sn;   // rotate by -yaw
        const float lz = -rx * sn + rz * cs;
        const float hx = std::fabs(box.size.x) * 0.5f;
        const float hz = std::fabs(box.size.z) * 0.5f;
        const float clampedX = lx < -hx ? -hx : (lx > hx ? hx : lx);
        const float clampedZ = lz < -hz ? -hz : (lz > hz ? hz : lz);
        const float dx = lx - clampedX, dz = lz - clampedZ;
        return dx * dx + dz * dz <= radius * radius;
    }

    /// Move from @p from by (dx,dz), sliding along whichever axis stays clear.
    ecs::Vec3 slideMove(const ecs::Vec3& from, float dx, float dz, float radius) const {
        const ecs::Vec3 full{from.x + dx, from.y, from.z + dz};
        if (!hitsWall(full, radius)) return full;
        const ecs::Vec3 xOnly{from.x + dx, from.y, from.z};
        if (!hitsWall(xOnly, radius)) return xOnly;
        const ecs::Vec3 zOnly{from.x, from.y, from.z + dz};
        if (!hitsWall(zOnly, radius)) return zOnly;
        return from; // fully blocked this step
    }
};

/**
 * @brief Build a playable game from a converted scene: walls become collision
 *        geometry; "player"/"enemy"/"treasure"(or "coin")/"door"(or "exit")
 *        spawns become the player, enemies, coins, and exit.
 */
inline DungeonGame loadGame(const SceneDescription& scene) {
    DungeonGame game;
    game.walls = scene.wallBoxes;
    for (const EntitySpawn& s : scene.spawns) {
        if (s.type == "player") {
            game.playerPosition = s.position;
        } else if (s.type == "enemy") {
            game.enemies.push_back(Enemy{s.position});
        } else if (s.type == "treasure" || s.type == "coin") {
            game.coins.push_back(Coin{s.position, false});
        } else if (s.type == "door" || s.type == "exit") {
            game.exitPosition = s.position;
            game.hasExit = true;
        }
    }
    game.totalCoins = static_cast<int>(game.coins.size());
    return game;
}

} // namespace game
} // namespace IKore
