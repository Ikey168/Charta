#pragma once

#include "game/DungeonGame.h"       // DungeonGame, ecs::Vec3
#include "game/DungeonRenderData.h" // RenderColor, renderColorForType

#include <algorithm>
#include <cmath>
#include <string>

/**
 * @file GameCamera.h
 * @brief Top-down / follow game camera and a theme palette for Doodlebound (issue #354).
 *
 * TourCamera (#165) is the first-person walkthrough; this is the normal play view: a camera
 * that frames the player (and, on demand, the whole level) either straight top-down or from
 * a fixed follow angle, plus a theme - a cohesive material/background palette so a level
 * reads clearly. Both are renderer-agnostic and deterministic: update() computes the eye/
 * target the engine's GL camera consumes (with optional smoothing), and themedColor() maps a
 * spawn/feature type to the active theme's color (falling back to the default #333 palette).
 * Header-only, std only.
 */
namespace IKore {
namespace game {

enum class GameCameraMode { TopDown, Follow };

/// A framing camera: an eye looking at a target with an up vector.
struct GameCamera {
    ecs::Vec3 eye{0.0f, 12.0f, 0.0f};
    ecs::Vec3 target{};
    ecs::Vec3 up{0.0f, 1.0f, 0.0f};

    GameCameraMode mode{GameCameraMode::TopDown};
    float height{12.0f};       ///< top-down: eye height above the target.
    float followDist{8.0f};    ///< follow: horizontal distance behind (-Z).
    float followHeight{7.0f};  ///< follow: eye height.
    float smooth{0.0f};        ///< 0 = snap; (0,1] = fraction moved toward the goal per update.

    /// Desired eye for @p player under the current mode. Top-down uses +Z as up (the view
    /// looks straight down -Y); follow looks down at the player from behind and above.
    ecs::Vec3 desiredEye(const ecs::Vec3& player) const {
        if (mode == GameCameraMode::TopDown)
            return ecs::Vec3{player.x, player.y + height, player.z};
        return ecs::Vec3{player.x, player.y + followHeight, player.z - followDist};
    }
    ecs::Vec3 desiredUp() const {
        return mode == GameCameraMode::TopDown ? ecs::Vec3{0.0f, 0.0f, 1.0f}
                                               : ecs::Vec3{0.0f, 1.0f, 0.0f};
    }

    /// Frame the player this update (snapping or smoothing toward the goal).
    void update(const ecs::Vec3& player, float /*dt*/ = 0.0f) {
        const ecs::Vec3 goalEye = desiredEye(player);
        up = desiredUp();
        if (smooth <= 0.0f) {
            eye = goalEye;
            target = player;
        } else {
            const float t = smooth > 1.0f ? 1.0f : smooth;
            eye = lerp(eye, goalEye, t);
            target = lerp(target, player, t);
        }
    }

    /// Frame the whole level top-down: center on the bounds and lift the eye to fit them.
    void frameLevel(const ecs::Vec3& minB, const ecs::Vec3& maxB) {
        const ecs::Vec3 center{(minB.x + maxB.x) * 0.5f, 0.0f, (minB.z + maxB.z) * 0.5f};
        const float span = std::max(maxB.x - minB.x, maxB.z - minB.z);
        height = std::max(span * 0.7f, 4.0f);
        mode = GameCameraMode::TopDown;
        target = center;
        up = desiredUp();
        eye = ecs::Vec3{center.x, center.y + height, center.z};
    }

private:
    static ecs::Vec3 lerp(const ecs::Vec3& a, const ecs::Vec3& b, float t) {
        return ecs::Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }
};

/// Axis-aligned bounds of a game's walls + actors (for frameLevel).
inline void levelBounds(const DungeonGame& game, ecs::Vec3& minB, ecs::Vec3& maxB) {
    minB = maxB = game.playerPosition;
    auto grow = [&](const ecs::Vec3& p) {
        minB.x = std::min(minB.x, p.x); maxB.x = std::max(maxB.x, p.x);
        minB.z = std::min(minB.z, p.z); maxB.z = std::max(maxB.z, p.z);
    };
    for (const world::Box& b : game.walls) grow(b.center);
    for (const Coin& c : game.coins) grow(c.position);
    if (game.hasExit) grow(game.exitPosition);
}

// --- Theme / material palette ------------------------------------------------

/// A cohesive palette: a background plus the core material colors.
struct Theme {
    std::string name{"default"};
    RenderColor background{0.10f, 0.10f, 0.12f};
    RenderColor wall{0.50f, 0.50f, 0.55f};
    RenderColor player{0.20f, 0.80f, 0.35f};
    RenderColor enemy{0.85f, 0.20f, 0.20f};
    RenderColor coin{0.92f, 0.85f, 0.20f};
    RenderColor exit{0.25f, 0.45f, 0.92f};
};

/// A cool, dark dungeon theme.
inline Theme dungeonTheme() {
    Theme t;
    t.name = "dungeon";
    t.background = {0.06f, 0.06f, 0.10f};
    t.wall = {0.38f, 0.40f, 0.52f};
    t.player = {0.30f, 0.85f, 0.55f};
    t.enemy = {0.90f, 0.25f, 0.30f};
    t.coin = {0.95f, 0.82f, 0.25f};
    t.exit = {0.30f, 0.55f, 0.95f};
    return t;
}

/// A warm, bright grassland theme (distinct from dungeon so themes are visibly different).
inline Theme grassTheme() {
    Theme t;
    t.name = "grass";
    t.background = {0.30f, 0.55f, 0.30f};
    t.wall = {0.55f, 0.42f, 0.28f};
    t.player = {0.20f, 0.45f, 0.90f};
    t.enemy = {0.85f, 0.35f, 0.10f};
    t.coin = {0.98f, 0.88f, 0.30f};
    t.exit = {0.90f, 0.30f, 0.75f};
    return t;
}

/// The active theme's color for a spawn/feature type; falls back to the #333 palette for
/// types the theme does not override (keys, switches, hazards, ...).
inline RenderColor themedColor(const std::string& type, const Theme& theme) {
    if (type == "wall") return theme.wall;
    if (type == "player") return theme.player;
    if (type == "enemy" || (type.size() >= 5 && type.compare(0, 5, "enemy") == 0)) return theme.enemy;
    if (type == "coin" || type == "treasure") return theme.coin;
    if (type == "exit" || type == "door") return theme.exit;
    return renderColorForType(type);
}

} // namespace game
} // namespace IKore
