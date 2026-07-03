#pragma once

#include "game/DungeonGame.h"      // SceneDescription, EntitySpawn, DungeonGame, loadGame
#include "world/GeoJsonImporter.h" // world::City / Building / Road

#include <cmath>
#include <cstddef>

/**
 * @file CityGame.h
 * @brief Turn an imported GeoJSON/OSM City into a playable world (issue #313).
 *
 * The GeoJSON importer (#150) parses a map extract into a renderer-agnostic City
 * (building footprints, road centerlines, intersections). This bridges that data
 * into the deterministic DungeonGame loop (#164) so an imported map is immediately
 * walkable and clearable without a renderer:
 *   - each building footprint becomes a solid wall collision box (an obstacle the
 *     player slides against), reusing the same world::Box / DungeonGame collision,
 *   - the road network provides the walkable placements: the player spawns on the
 *     first road, coins are dropped on the road graph (intersections, else road
 *     midpoints), and the exit is the road endpoint farthest from the spawn.
 *
 * So the output is a SceneDescription (wallBoxes + EntitySpawns) fed straight into
 * loadGame(), i.e. the same playable contract as a hand-drawn Doodlebound level.
 * Header-only, dependency-free (std + the header-only importer / game types), so it
 * is unit-testable headless and deterministic.
 */
namespace IKore {
namespace game {

struct CityGameOptions {
    float wallHeight{3.0f};        ///< building obstacle height (collision is XZ-only, but kept sensible).
    bool coinsAtIntersections{true}; ///< drop coins at road intersections; else at road midpoints.
    bool coinsAtRoadMidpoints{true}; ///< fallback when there are no intersections.
};

namespace detail {

/// Squared XZ distance between two points.
inline float distSqXZ(const ecs::Vec3& a, const ecs::Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

} // namespace detail

/**
 * @brief Convert an imported City into a playable SceneDescription.
 *
 * Buildings become wall boxes; the road network seeds the player spawn, coins, and
 * exit. Determinism: buildings/roads/intersections are visited in City order, so the
 * same City always yields byte-identical spawns.
 */
inline SceneDescription cityToScene(const world::City& city, const CityGameOptions& opt = {}) {
    SceneDescription scene;

    const float wallHeight = opt.wallHeight > 0.0f ? opt.wallHeight : 1.0f;
    for (const world::Building& b : city.buildings) {
        world::Box box;
        box.center = ecs::Vec3{b.center.x, wallHeight * 0.5f, b.center.z};
        box.size = ecs::Vec3{b.size.x, wallHeight, b.size.z};
        box.yaw = 0.0f;
        scene.wallBoxes.push_back(box);
    }

    // Gather every road endpoint (walkable). The first is the player spawn.
    bool haveSpawn = false;
    ecs::Vec3 spawn{};
    for (const world::Road& road : city.roads) {
        if (road.centerline.size() < 2) continue;
        if (!haveSpawn) {
            spawn = road.centerline.front();
            haveSpawn = true;
            break;
        }
    }
    if (haveSpawn) {
        scene.spawns.push_back(EntitySpawn{"player", spawn, 0.0f});
    }

    // Exit: the road endpoint farthest (XZ) from the spawn, so the level spans the map.
    if (haveSpawn) {
        ecs::Vec3 exit = spawn;
        float best = -1.0f;
        for (const world::Road& road : city.roads) {
            if (road.centerline.empty()) continue;
            for (const ecs::Vec3& end : {road.centerline.front(), road.centerline.back()}) {
                const float d = detail::distSqXZ(spawn, end);
                if (d > best) {
                    best = d;
                    exit = end;
                }
            }
        }
        if (best > 0.0f) scene.spawns.push_back(EntitySpawn{"exit", exit, 0.0f});
    }

    // Coins on the road graph: intersections if present, else road midpoints.
    if (opt.coinsAtIntersections && !city.intersections.empty()) {
        for (const ecs::Vec3& node : city.intersections) {
            scene.spawns.push_back(EntitySpawn{"coin", node, 0.0f});
        }
    } else if (opt.coinsAtRoadMidpoints) {
        for (const world::Road& road : city.roads) {
            if (road.centerline.size() < 2) continue;
            const ecs::Vec3& a = road.centerline.front();
            const ecs::Vec3& b = road.centerline.back();
            scene.spawns.push_back(
                EntitySpawn{"coin", ecs::Vec3{(a.x + b.x) * 0.5f, 0.0f, (a.z + b.z) * 0.5f}, 0.0f});
        }
    }

    return scene;
}

/// Build a playable DungeonGame directly from an imported City.
inline DungeonGame loadCityGame(const world::City& city, const CityGameOptions& opt = {}) {
    return loadGame(cityToScene(city, opt));
}

} // namespace game
} // namespace IKore
