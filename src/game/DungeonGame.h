#pragma once

#include "game/DoodleScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

/// Selectable enemy behavior (#320). Chase is the default and reproduces the
/// original single chaser byte-for-byte.
enum class EnemyBehavior { Chase, Patrol, Flee, Ranged };

struct Enemy {
    ecs::Vec3 position{};
    EnemyBehavior behavior{EnemyBehavior::Chase};
    ecs::Vec3 home{};        ///< anchor for patrol (oscillates around it).
    int patrolAxis{0};       ///< 0 = X, 1 = Z.
    int patrolDir{1};        ///< current patrol direction (+1 / -1).
    float patrolRange{3.0f}; ///< patrol amplitude around home.
    float fireCooldown{0.0f};///< ranged: seconds until the next shot.
};

/// A projectile fired by a Ranged enemy; contact is a loss (#320).
struct Projectile {
    ecs::Vec3 position{};
    ecs::Vec3 velocity{};
    float life{0.0f};
};

// --- Optional richer drawable semantics (issue #319) -------------------------
// All of these are absent from a classic level, so a level with none of them
// plays byte-identically to the original move/collect/avoid/exit loop.

/// A collectible key; collecting it opens every LockedDoor with the same @c id.
struct Key {
    ecs::Vec3 position{};
    int id{0};
    bool collected{false};
};

/// A door that blocks movement until a key of the same @c id is collected.
struct LockedDoor {
    world::Box box;
    int id{0};
    bool open{false};
};

/// A momentary switch; entering it toggles every ToggleWall with the same @c id.
struct Switch {
    ecs::Vec3 position{};
    int id{0};
    bool wasPressed{false};
};

/// A wall that a Switch flips between solid (blocks) and open.
struct ToggleWall {
    world::Box box;
    int id{0};
    bool solid{true};
};

/// A static hazard; touching it is a loss.
struct Hazard {
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

    // Optional richer semantics (#319); empty on a classic level.
    std::vector<Key> keys;
    std::vector<LockedDoor> lockedDoors;
    std::vector<Switch> switches;
    std::vector<ToggleWall> toggleWalls;
    std::vector<Hazard> hazards;

    // Live ranged-enemy projectiles (#320); empty unless a Ranged enemy has fired.
    std::vector<Projectile> projectiles;

    // Tunables.
    float playerSpeed{4.0f};
    float enemySpeed{2.0f};
    float playerRadius{0.4f};
    float enemyRadius{0.4f};
    float coinRadius{0.4f};
    float exitRadius{0.6f};
    float keyRadius{0.4f};
    float switchRadius{0.5f};
    float hazardRadius{0.4f};
    float featureSize{1.0f}; ///< footprint of a spawned locked door / toggle wall.
    float projectileSpeed{6.0f};      ///< ranged projectile speed.
    float projectileRadius{0.25f};    ///< projectile hit radius.
    float rangedFireInterval{1.0f};   ///< seconds between ranged shots.
    float projectileLife{3.0f};       ///< projectile lifetime before it expires.

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

        // Key pickup opens every locked door of the same id (#319).
        for (Key& k : keys) {
            if (!k.collected && within(playerPosition, k.position, playerRadius + keyRadius)) {
                k.collected = true;
                for (LockedDoor& d : lockedDoors) {
                    if (d.id == k.id) d.open = true;
                }
            }
        }

        // A switch toggles its linked walls once per press (rising edge) (#319).
        for (Switch& sw : switches) {
            const bool pressed = within(playerPosition, sw.position, playerRadius + switchRadius);
            if (pressed && !sw.wasPressed) {
                for (ToggleWall& w : toggleWalls) {
                    if (w.id == sw.id) w.solid = !w.solid;
                }
            }
            sw.wasPressed = pressed;
        }

        // Hazard contact is a loss (#319).
        for (const Hazard& h : hazards) {
            if (within(playerPosition, h.position, playerRadius + hazardRadius)) {
                status = GameStatus::Lost;
                return;
            }
        }

        // Enemies act by behavior (#320); direct contact is always a loss. The default
        // Chase branch is the original chaser, so a classic enemy is byte-identical.
        for (Enemy& e : enemies) {
            switch (e.behavior) {
                case EnemyBehavior::Chase: {
                    const float dx = playerPosition.x - e.position.x;
                    const float dz = playerPosition.z - e.position.z;
                    const float d = std::sqrt(dx * dx + dz * dz);
                    if (d > 1e-6f) {
                        const float step = enemySpeed * dt;
                        e.position = slideMove(e.position, (dx / d) * step, (dz / d) * step, enemyRadius);
                    }
                    break;
                }
                case EnemyBehavior::Flee: {
                    const float dx = e.position.x - playerPosition.x;
                    const float dz = e.position.z - playerPosition.z;
                    const float d = std::sqrt(dx * dx + dz * dz);
                    if (d > 1e-6f) {
                        const float step = enemySpeed * dt;
                        e.position = slideMove(e.position, (dx / d) * step, (dz / d) * step, enemyRadius);
                    }
                    break;
                }
                case EnemyBehavior::Patrol: {
                    const float step = enemySpeed * dt * static_cast<float>(e.patrolDir);
                    const float mx = e.patrolAxis == 0 ? step : 0.0f;
                    const float mz = e.patrolAxis == 1 ? step : 0.0f;
                    const ecs::Vec3 moved = slideMove(e.position, mx, mz, enemyRadius);
                    const bool stuck = moved.x == e.position.x && moved.z == e.position.z;
                    e.position = moved;
                    const float along = e.patrolAxis == 0 ? (e.position.x - e.home.x) : (e.position.z - e.home.z);
                    if (std::fabs(along) >= e.patrolRange || stuck) e.patrolDir = -e.patrolDir;
                    break;
                }
                case EnemyBehavior::Ranged: {
                    e.fireCooldown -= dt;
                    if (e.fireCooldown <= 0.0f) {
                        const float dx = playerPosition.x - e.position.x;
                        const float dz = playerPosition.z - e.position.z;
                        const float d = std::sqrt(dx * dx + dz * dz);
                        if (d > 1e-6f) {
                            projectiles.push_back(Projectile{
                                e.position,
                                ecs::Vec3{(dx / d) * projectileSpeed, 0.0f, (dz / d) * projectileSpeed},
                                projectileLife});
                        }
                        e.fireCooldown = rangedFireInterval;
                    }
                    break;
                }
            }
            if (within(playerPosition, e.position, playerRadius + enemyRadius)) {
                status = GameStatus::Lost;
                return;
            }
        }

        // Advance projectiles; contact is a loss, and expired ones are removed (#320).
        for (Projectile& p : projectiles) {
            p.position.x += p.velocity.x * dt;
            p.position.z += p.velocity.z * dt;
            p.life -= dt;
            if (within(playerPosition, p.position, playerRadius + projectileRadius)) {
                status = GameStatus::Lost;
                return;
            }
        }
        projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
                                         [](const Projectile& p) { return p.life <= 0.0f; }),
                          projectiles.end());

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

    /// True if movement is blocked here: a static wall, a still-locked door, or a solid
    /// toggle wall (#319). Equals hitsWall() on a classic level (no doors/toggle walls).
    bool blocked(const ecs::Vec3& pos, float radius) const {
        if (hitsWall(pos, radius)) return true;
        for (const LockedDoor& d : lockedDoors) {
            if (!d.open && circleHitsBox(pos, radius, d.box)) return true;
        }
        for (const ToggleWall& w : toggleWalls) {
            if (w.solid && circleHitsBox(pos, radius, w.box)) return true;
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
        if (!blocked(full, radius)) return full;
        const ecs::Vec3 xOnly{from.x + dx, from.y, from.z};
        if (!blocked(xOnly, radius)) return xOnly;
        const ecs::Vec3 zOnly{from.x, from.y, from.z + dz};
        if (!blocked(zOnly, radius)) return zOnly;
        return from; // fully blocked this step
    }
};

namespace detail {

/// Split a spawn type into its base name and optional "@id" link group (#319). A plain
/// type with no "@" yields id 0, so classic spawns ("player", "coin", ...) are unchanged.
inline std::string featureBase(const std::string& type, int& id) {
    const std::size_t at = type.find('@');
    if (at == std::string::npos) {
        id = 0;
        return type;
    }
    id = std::atoi(type.c_str() + at + 1);
    return type.substr(0, at);
}

/// A square footprint box for a spawned locked door / toggle wall (collision is XZ-only).
inline world::Box featureBox(const ecs::Vec3& pos, float yaw, float size) {
    world::Box b;
    b.center = ecs::Vec3{pos.x, size * 0.5f, pos.z};
    b.size = ecs::Vec3{size, size, size};
    b.yaw = yaw;
    return b;
}

} // namespace detail

/**
 * @brief Build a playable game from a converted scene: walls become collision geometry;
 *        "player"/"enemy"/"treasure"(or "coin")/"door"(or "exit") spawns become the
 *        player, enemies, coins, and exit. Optionally (#319) "key"/"lock"/"switch"/
 *        "toggle"/"hazard" spawns (each with an optional "@id" link) add richer rules; a
 *        scene with none of these produces exactly the classic game.
 */
inline DungeonGame loadGame(const SceneDescription& scene) {
    DungeonGame game;
    game.walls = scene.wallBoxes;
    for (const EntitySpawn& s : scene.spawns) {
        int id = 0;
        const std::string base = detail::featureBase(s.type, id);
        if (base == "player") {
            game.playerPosition = s.position;
        } else if (base == "enemy" || base == "enemy_patrol" || base == "enemy_flee" ||
                   base == "enemy_ranged") {
            Enemy e;
            e.position = s.position;
            e.home = s.position;
            if (base == "enemy_patrol") {
                e.behavior = EnemyBehavior::Patrol;
            } else if (base == "enemy_flee") {
                e.behavior = EnemyBehavior::Flee;
            } else if (base == "enemy_ranged") {
                e.behavior = EnemyBehavior::Ranged;
            }
            game.enemies.push_back(e);
        } else if (base == "treasure" || base == "coin") {
            game.coins.push_back(Coin{s.position, false});
        } else if (base == "door" || base == "exit") {
            game.exitPosition = s.position;
            game.hasExit = true;
        } else if (base == "key") {
            game.keys.push_back(Key{s.position, id, false});
        } else if (base == "lock" || base == "lockeddoor") {
            game.lockedDoors.push_back(
                LockedDoor{detail::featureBox(s.position, s.yaw, game.featureSize), id, false});
        } else if (base == "switch") {
            game.switches.push_back(Switch{s.position, id, false});
        } else if (base == "toggle" || base == "togglewall") {
            game.toggleWalls.push_back(
                ToggleWall{detail::featureBox(s.position, s.yaw, game.featureSize), id, true});
        } else if (base == "hazard" || base == "spike") {
            game.hazards.push_back(Hazard{s.position});
        }
    }
    game.totalCoins = static_cast<int>(game.coins.size());
    return game;
}

} // namespace game
} // namespace IKore
