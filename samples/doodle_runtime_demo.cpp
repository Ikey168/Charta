// Sample: the doodle cores wired into the engine's live ECS, end to end and headless.
//
// Imports a small neighborhood from GeoJSON, turns it into a playable world (#313),
// spawns its actors as ECS entities via DungeonRuntime, then drives the deterministic
// sim toward the exit - printing the live entity transforms and the outcome. It also
// validates the level up front with the solver (#316). No GL context required.
//
// Build (also wired into CMake as sample_doodle_runtime):
//   g++ -std=c++17 -I src samples/doodle_runtime_demo.cpp -o sample_doodle_runtime
//   ./sample_doodle_runtime

#include "core/ecs/View.h"
#include "game/CityGame.h"
#include "game/DungeonRuntime.h"
#include "game/Solver.h"

#include <cstdio>

using namespace IKore;

// A tiny OSM-style neighborhood: two roads sharing a node (one intersection) plus a
// building set off the streets.
static const char* kNeighborhood = R"JSON({
  "type": "FeatureCollection",
  "features": [
    { "type":"Feature", "properties":{"highway":"residential"},
      "geometry":{"type":"LineString","coordinates":[[0,0],[0.0004,0]]} },
    { "type":"Feature", "properties":{"highway":"residential"},
      "geometry":{"type":"LineString","coordinates":[[0.0004,0],[0.0004,0.0004]]} },
    { "type":"Feature", "properties":{"building":"yes","height":"10"},
      "geometry":{"type":"Polygon","coordinates":[[[0.0001,0.0001],[0.0002,0.0001],[0.0002,0.0002],[0.0001,0.0002],[0.0001,0.0001]]]} }
  ]
})JSON";

static int taggedCount(ecs::Registry& reg) {
    int n = 0;
    reg.view<game::SpawnTag>().each([&](ecs::Entity, game::SpawnTag&) { ++n; });
    return n;
}

int main() {
    const world::City city = world::importString(kNeighborhood, world::GeoImportOptions{});
    std::printf("Imported city: %zu buildings, %zu roads, %zu intersections\n",
                city.buildings.size(), city.roads.size(), city.intersections.size());

    game::DungeonRuntime rt(game::loadCityGame(city));
    std::printf("Playable world: %zu wall boxes, %d coin(s), exit=%s\n", rt.game().walls.size(),
                rt.game().totalCoins, rt.game().hasExit ? "yes" : "no");

    // Prove it is fair before playing it.
    const game::SolveResult solved = game::solve(rt.game());
    std::printf("Solver: solvable=%s par=%d reachableCoins=%d/%d\n",
                solved.solvable ? "true" : "false", solved.par, solved.reachableCoins,
                rt.game().totalCoins);

    // Spawn the actors into the live ECS (the renderer would attach meshes via decorate).
    ecs::Registry reg;
    rt.spawnInto(reg);
    std::printf("Spawned %d ECS entities (Transform + SpawnTag)\n", taggedCount(reg));

    // Drive the deterministic sim along the solver's route; the entity Transforms follow.
    const float dt = 1.0f / 60.0f;
    int steps = 0;
    for (const ecs::Vec3& target : solved.path) {
        int guard = 0;
        while (guard++ < 4000 && rt.game().status == game::GameStatus::Playing) {
            const ecs::Vec3 p = rt.game().playerPosition;
            const float dx = target.x - p.x, dz = target.z - p.z;
            if (dx * dx + dz * dz < 0.0025f) break;
            rt.update(reg, game::GameInput{dx, dz}, dt);
            ++steps;
        }
        if (rt.game().status != game::GameStatus::Playing) break;
    }

    const ecs::Vec3 fp = reg.isValid(rt.playerEntity())
                             ? reg.get<ecs::Transform>(rt.playerEntity()).position
                             : ecs::Vec3{};
    std::printf("After %d steps: player entity at (%.2f, %.2f), coins %d/%d, %s\n", steps, fp.x, fp.z,
                rt.game().coinsCollected, rt.game().totalCoins,
                rt.game().won() ? "WON" : (rt.game().lost() ? "LOST" : "still playing"));
    std::printf("Live entities remaining: %d\n", taggedCount(reg));
    return rt.game().won() ? 0 : 1;
}
