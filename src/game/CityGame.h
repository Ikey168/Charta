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
 * Richer OSM semantics (#365) layer on top and are inert when absent, so a plain
 * building+road import is unchanged: barrier ways (walls/fences) become thin wall
 * obstacles, water regions are tiled with hazards so stepping into water is a loss, and
 * map POIs (amenities/shops/...) become the preferred coin placements (real destinations).
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

    // Richer OSM semantics (#365). All default on, but inert on a City with no water /
    // barriers / POIs, so a plain building+road import plays exactly as before.
    bool coinsAtPois{true};      ///< when the map has POIs, seed coins there (preferred source).
    bool waterAsHazard{true};    ///< tile hazards across water regions so water is deadly.
    float hazardSpacing{3.0f};   ///< grid step (world units) for tiling water hazards.
    bool barriersAsWalls{true};  ///< emit thin wall boxes along barrier (wall/fence) ways.
};

namespace detail {

/// Squared XZ distance between two points.
inline float distSqXZ(const ecs::Vec3& a, const ecs::Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

/// Ray-cast point-in-polygon test on the XZ footprint (ring need not be closed).
inline bool pointInPolygonXZ(const std::vector<ecs::Vec3>& ring, float x, float z) {
    bool inside = false;
    const std::size_t n = ring.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = ring[i].x, zi = ring[i].z;
        const float xj = ring[j].x, zj = ring[j].z;
        if (((zi > z) != (zj > z)) &&
            (x < (xj - xi) * (z - zi) / ((zj - zi) != 0.0f ? (zj - zi) : 1e-6f) + xi))
            inside = !inside;
    }
    return inside;
}

/// Append a thin wall box for each segment of a barrier polyline (#365).
inline void emitBarrierWalls(const std::vector<ecs::Vec3>& line, float width, float wallHeight,
                             std::vector<world::Box>& out) {
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const ecs::Vec3& a = line[i];
        const ecs::Vec3& b = line[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z;
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length <= 0.0f) continue;
        world::Box box;
        box.center = ecs::Vec3{(a.x + b.x) * 0.5f, wallHeight * 0.5f, (a.z + b.z) * 0.5f};
        box.size = ecs::Vec3{length, wallHeight, width};
        box.yaw = std::atan2(dz, dx);
        out.push_back(box);
    }
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

    // Barrier ways (walls/fences) become thin wall obstacles (#365).
    if (opt.barriersAsWalls) {
        for (const world::Barrier& bar : city.barriers)
            detail::emitBarrierWalls(bar.line, bar.width, wallHeight, scene.wallBoxes);
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

    // Coins: prefer map POIs (real places) when present, else the road graph
    // (intersections if any, else road midpoints).
    if (opt.coinsAtPois && !city.pois.empty()) {
        for (const world::Poi& poi : city.pois) {
            scene.spawns.push_back(EntitySpawn{"coin", ecs::Vec3{poi.position.x, 0.0f, poi.position.z}, 0.0f});
        }
    } else if (opt.coinsAtIntersections && !city.intersections.empty()) {
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

    // Water regions become deadly: tile hazards across each water footprint on a grid,
    // keeping only points actually inside the polygon (#365).
    if (opt.waterAsHazard && opt.hazardSpacing > 0.0f) {
        for (const world::Region& r : city.regions) {
            if (r.kind != world::RegionKind::Water) continue;
            const float minX = r.center.x - r.size.x * 0.5f, maxX = r.center.x + r.size.x * 0.5f;
            const float minZ = r.center.z - r.size.z * 0.5f, maxZ = r.center.z + r.size.z * 0.5f;
            for (float z = minZ; z <= maxZ; z += opt.hazardSpacing) {
                for (float x = minX; x <= maxX; x += opt.hazardSpacing) {
                    if (detail::pointInPolygonXZ(r.footprint, x, z))
                        scene.spawns.push_back(EntitySpawn{"hazard", ecs::Vec3{x, 0.0f, z}, 0.0f});
                }
            }
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
