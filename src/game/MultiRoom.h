#pragma once

#include "cv/Topology.h"      // cv::Topology, cv::Room, cv::Doorway
#include "game/DoodleScene.h" // SceneDescription, EntitySpawn, world::Box, detail::appendBox
#include "game/DungeonGame.h" // DungeonGame, loadGame

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

/**
 * @file MultiRoom.h
 * @brief Turn a room-topology graph into a multi-room playable level (issue #344).
 *
 * DungeonGame (#164) plays a single connected space, but cv/Topology (#168) already
 * segments a drawing's walls into rooms, doorways, and a connectivity graph. This
 * bridges the two: it rebuilds the walls from the topology's cell grid (one wall box
 * per wall cell), leaves each doorway cell open as a passage, and places the player,
 * the exit, and one coin objective across distinct rooms - so a level spans several
 * connected chambers instead of one.
 *
 * Because the passages are exactly the topology's doorways, a level is clearable iff
 * the rooms are connected through them, which the Solver (#316) verifies over the
 * same DungeonGame collision rules. Each grid cell maps to a @c cellWorld-sized world
 * square (default 2 units, comfortably wider than the player diameter) so a one-cell
 * doorway is a clean passage.
 *
 * Header-only, deterministic, dependency-free (std + the header-only CV / game types).
 */
namespace IKore {
namespace game {

struct MultiRoomOptions {
    float cellWorld{2.0f};  ///< world units per topology cell (>= player diameter for clean doorways).
    float wallHeight{3.0f}; ///< extruded wall height.
};

struct MultiRoomResult {
    SceneDescription scene; ///< walls (boxes + mesh) + player/exit/coin spawns across rooms.
    int roomCount{0};       ///< interior (non-outside) rooms placed.
    int doorwayCount{0};    ///< doorways left open as passages.
    int coinCount{0};       ///< coin objectives placed (one per middle room).
};

/**
 * @brief Build a multi-room scene from a topology graph.
 *
 * Every wall cell becomes a unit wall box; every doorway cell is left open so the two
 * rooms it separates connect. The player is placed in the lowest-id interior room, the
 * exit in the highest-id interior room, and one coin in each room in between - all at a
 * deterministic walkable anchor cell (the first open cell of the room in scan order),
 * so the player, exit, and objectives span distinct connected rooms.
 */
inline MultiRoomResult buildMultiRoom(const cv::Topology& topo, const MultiRoomOptions& opt = {}) {
    MultiRoomResult out;
    const float s = opt.cellWorld > 1e-4f ? opt.cellWorld : 2.0f;
    const float h = opt.wallHeight > 1e-4f ? opt.wallHeight : 3.0f;
    const int W = topo.width, H = topo.height;
    if (W <= 0 || H <= 0) return out;

    // Doorway cells stay open (passages); mark them so they are not walled over.
    std::vector<char> isDoor(static_cast<std::size_t>(W) * H, 0);
    for (const cv::Doorway& d : topo.doorways) {
        if (d.x >= 0 && d.y >= 0 && d.x < W && d.y < H)
            isDoor[static_cast<std::size_t>(d.y) * W + d.x] = 1;
    }
    out.doorwayCount = static_cast<int>(topo.doorways.size());

    // Walls: every non-room cell that is not a passable doorway becomes a unit wall box.
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (topo.labelAt(x, y) != -1) continue;                     // room floor
            if (isDoor[static_cast<std::size_t>(y) * W + x]) continue;  // passage
            const ecs::Vec3 c{x * s, h * 0.5f, y * s};
            world::Box b;
            b.center = c;
            b.size = ecs::Vec3{s, h, s};
            b.yaw = 0.0f;
            out.scene.wallBoxes.push_back(b);
            detail::appendBox(out.scene.wallMesh, c, s, h, s, 0.0f);
        }
    }

    // Interior rooms in id order + a deterministic walkable anchor cell for each.
    std::vector<int> interior;
    for (const cv::Room& r : topo.rooms)
        if (!r.isOutside) interior.push_back(r.id);
    std::sort(interior.begin(), interior.end());
    out.roomCount = static_cast<int>(interior.size());

    std::vector<ecs::Vec3> anchor(topo.rooms.size(), ecs::Vec3{});
    std::vector<char> haveAnchor(topo.rooms.size(), 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int id = topo.labelAt(x, y);
            if (id < 0 || haveAnchor[static_cast<std::size_t>(id)]) continue;
            anchor[static_cast<std::size_t>(id)] = ecs::Vec3{x * s, 0.0f, y * s};
            haveAnchor[static_cast<std::size_t>(id)] = 1;
        }
    }

    // Spawns across rooms: player first, exit last, one coin per middle room.
    if (!interior.empty()) {
        out.scene.spawns.push_back(EntitySpawn{"player", anchor[static_cast<std::size_t>(interior.front())], 0.0f});
        if (interior.size() >= 2)
            out.scene.spawns.push_back(EntitySpawn{"exit", anchor[static_cast<std::size_t>(interior.back())], 0.0f});
        for (std::size_t i = 1; i + 1 < interior.size(); ++i) {
            out.scene.spawns.push_back(EntitySpawn{"coin", anchor[static_cast<std::size_t>(interior[i])], 0.0f});
            ++out.coinCount;
        }
    }
    return out;
}

/// Convenience: a playable multi-room DungeonGame straight from a topology graph.
inline DungeonGame buildMultiRoomGame(const cv::Topology& topo, const MultiRoomOptions& opt = {}) {
    return loadGame(buildMultiRoom(topo, opt).scene);
}

} // namespace game
} // namespace IKore
