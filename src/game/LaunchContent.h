#pragma once

#include "game/DungeonGame.h" // SceneDescription, EntitySpawn, DungeonGame, loadGame
#include "game/Solver.h"      // solve, SolveResult, SolveOptions

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file LaunchContent.h
 * @brief The launch campaign, seeded catalog, and weekly rotation (issue #371).
 *
 * The delivery machinery (campaign/progress, LevelCatalog #174, WeeklyChallenge #273) shipped
 * empty; this is the actual content. It authors a difficulty-ramped set of levels that
 * exercise the mechanic vocabulary (coins, walls, enemies, keys, switches, hazards, pushable
 * blocks), exposes them as a manifest (id, order, world, par), validates that every level is
 * solvable and carries a solver-computed par (#316), and selects a weekly rotation
 * deterministically from the catalog for a given week.
 *
 * Levels are authored so the solver-verified path (static walls + coins + exit, with blocks
 * pushed) is always clear; keys/switches are reachable pickups and locked doors / toggle
 * walls sit off that path (the solver treats them as closed), so every shipped level passes
 * the fairness gate. Header-only, std only (plus the header-only game / solver).
 */
namespace IKore {
namespace game {

namespace content {

inline void wall(SceneDescription& s, float cx, float cz, float sx, float sz) {
    world::Box b;
    b.center = ecs::Vec3{cx, 0.0f, cz};
    b.size = ecs::Vec3{sx, 3.0f, sz};
    b.yaw = 0.0f;
    s.wallBoxes.push_back(b);
}
inline void spawn(SceneDescription& s, const char* type, float x, float z) {
    s.spawns.push_back(EntitySpawn{type, ecs::Vec3{x, 0.0f, z}, 0.0f});
}

// Level 1: the basics - reach the coin, then the exit, in open space.
inline SceneDescription level_first_steps() {
    SceneDescription s;
    spawn(s, "player", 0, 0);
    spawn(s, "coin", 3, 0);
    spawn(s, "exit", 6, 0);
    return s;
}

// Level 2: a walled corridor with a bend and two coins.
inline SceneDescription level_the_corridor() {
    SceneDescription s;
    wall(s, 0, 2, 10, 0.4f);
    wall(s, 0, -2, 6, 0.4f);
    wall(s, 4, 0, 0.4f, 4);
    spawn(s, "player", -3, 0);
    spawn(s, "coin", 0, 0);
    spawn(s, "coin", 3, -1);
    spawn(s, "exit", 3, 1);
    return s;
}

// Level 3: a chase enemy patrols the open room (does not block the route).
inline SceneDescription level_the_chase() {
    SceneDescription s;
    wall(s, 0, 4, 12, 0.4f);
    wall(s, 0, -4, 12, 0.4f);
    spawn(s, "player", -4, 0);
    spawn(s, "enemy", 4, 3);
    spawn(s, "coin", 0, 0);
    spawn(s, "coin", 2, -2);
    spawn(s, "exit", 5, 0);
    return s;
}

// Level 4: a key + door mechanic. The exit path is open; the locked door guards a side alcove
// (solver-closed), and the key is a reachable pickup on the way.
inline SceneDescription level_lock_and_key() {
    SceneDescription s;
    wall(s, 0, 3, 12, 0.4f);
    wall(s, 0, -3, 12, 0.4f);
    spawn(s, "player", -4, 0);
    spawn(s, "key@1", -1, 0);
    spawn(s, "coin", 1, 1);
    spawn(s, "lock@1", 2, -2); // off the critical path (a side alcove)
    spawn(s, "exit", 5, 0);
    return s;
}

// Level 5: hazards to weave around; a switch pickup and a toggle wall off the route.
inline SceneDescription level_the_gauntlet() {
    SceneDescription s;
    wall(s, 0, 3, 14, 0.4f);
    wall(s, 0, -3, 14, 0.4f);
    spawn(s, "player", -5, 0);
    spawn(s, "hazard", -1, 1);
    spawn(s, "hazard", 1, -1);
    spawn(s, "switch@2", 0, 0);
    spawn(s, "toggle@2", 3, 2);  // off the direct route
    spawn(s, "coin", 2, 0);
    spawn(s, "exit", 6, 0);
    return s;
}

// Level 6: a pushable block. The exit is reachable and a block sits beside the route (the
// Sokoban solver confirms it is not stranded).
inline SceneDescription level_the_push() {
    SceneDescription s;
    wall(s, 0, 3, 12, 0.4f);
    wall(s, 0, -3, 12, 0.4f);
    wall(s, -6, 0, 0.4f, 6);
    wall(s, 6, 0, 0.4f, 6);
    spawn(s, "player", -4, 0);
    spawn(s, "block", -1, 1);
    spawn(s, "coin", 2, 0);
    spawn(s, "exit", 4, -1);
    return s;
}

} // namespace content

/// One shipped level: its id, ordering, world, and a builder for its scene.
struct ContentLevel {
    std::string id;
    std::string world;
    int order{0};
    SceneDescription (*build)(){nullptr};
};

/// The launch manifest, in campaign order.
inline std::vector<ContentLevel> launchManifest() {
    return {
        {"c1_first_steps", "campaign", 1, &content::level_first_steps},
        {"c2_corridor", "campaign", 2, &content::level_the_corridor},
        {"c3_chase", "campaign", 3, &content::level_the_chase},
        {"c4_lock_and_key", "campaign", 4, &content::level_lock_and_key},
        {"c5_gauntlet", "campaign", 5, &content::level_the_gauntlet},
        {"c6_the_push", "campaign", 6, &content::level_the_push},
    };
}

/// Validation result for one level.
struct ContentCheck {
    std::string id;
    bool solvable{false};
    int par{-1};
};

/// Solve every manifest level and record whether it is solvable and its par.
inline std::vector<ContentCheck> validateContent(const std::vector<ContentLevel>& manifest) {
    std::vector<ContentCheck> out;
    out.reserve(manifest.size());
    for (const ContentLevel& lvl : manifest) {
        const DungeonGame game = loadGame(lvl.build());
        const SolveResult sr = solve(game);
        out.push_back(ContentCheck{lvl.id, sr.solvable, sr.par});
    }
    return out;
}

/// True if every level in @p manifest is solvable with a recorded (non-negative) par.
inline bool allContentFair(const std::vector<ContentLevel>& manifest) {
    for (const ContentCheck& c : validateContent(manifest))
        if (!c.solvable || c.par < 0) return false;
    return true;
}

// --- Weekly rotation ---------------------------------------------------------

/// Deterministically select @p count level ids from @p manifest for @p weekIndex. Same week
/// -> same selection on every device; consecutive weeks rotate through the catalog.
inline std::vector<std::string> weeklyRotation(const std::vector<ContentLevel>& manifest,
                                               std::uint64_t weekIndex, int count) {
    std::vector<std::string> picks;
    if (manifest.empty() || count <= 0) return picks;
    const std::size_t n = manifest.size();
    // A seeded stride keeps the pick deterministic and spreads it across the catalog.
    std::uint64_t idx = (weekIndex * 2654435761ULL) % n;
    const std::uint64_t stride = 1 + (weekIndex * 40503ULL) % (n - 0);
    for (int i = 0; i < count; ++i) {
        picks.push_back(manifest[static_cast<std::size_t>(idx)].id);
        idx = (idx + stride) % n;
    }
    return picks;
}

} // namespace game
} // namespace IKore
