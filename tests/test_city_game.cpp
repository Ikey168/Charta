// Test for the GeoJSON/OSM "-> playable world" bridge - issue #313.
//
// Verifies that an imported City (buildings + roads + intersections) converts into a
// playable DungeonGame: buildings become solid wall obstacles, the road network seeds
// the player spawn / coins / exit, the spawn is clear of walls, and the resulting map
// is actually walkable and clearable (a full headless playthrough wins). Pure std +
// the header-only importer / game:
//   g++ -std=c++17 -I src tests/test_city_game.cpp -o test_city_game

#include "game/CityGame.h"
#include "world/GeoJsonImporter.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

// Origin = first coordinate (0,0), lat0=0 so 1 deg lon/lat = 111320 m (square).
// Two roads sharing the node (0.0004,0) => one intersection; one building set well
// off both road legs so the streets stay walkable.
//   road1: (0,0)         -> (44.528,0)          along +X at z=0
//   road2: (44.528,0)    -> (44.528,44.528)     along +Z at x=44.528
//   building: x[11.1,22.3] z[11.1,22.3]         (clear of both legs)
static const char* kGeoJson = R"JSON({
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

static int countType(const game::SceneDescription& s, const char* t) {
    int n = 0;
    for (const auto& sp : s.spawns) if (sp.type == t) ++n;
    return n;
}

int main() {
    // Pass explicit options to disambiguate from the SVG importer's importString
    // overload (both are pulled in transitively via the game headers).
    const world::City city = world::importString(kGeoJson, world::GeoImportOptions{});
    CHECK(city.buildings.size() == 1);
    CHECK(city.roads.size() == 2);
    CHECK(city.intersections.size() == 1);

    const game::SceneDescription scene = game::cityToScene(city);

    // Buildings became wall boxes; the road graph seeded exactly one of each spawn.
    CHECK(scene.wallBoxes.size() == 1);
    CHECK(countType(scene, "player") == 1);
    CHECK(countType(scene, "exit") == 1);
    CHECK(countType(scene, "coin") == 1); // one intersection -> one coin

    game::DungeonGame gm = game::loadCityGame(city);
    CHECK(gm.totalCoins == 1);
    CHECK(gm.hasExit);
    CHECK(gm.enemies.empty());

    // The player spawns on a road, not inside a building.
    CHECK(!gm.hitsWall(gm.playerPosition, gm.playerRadius));
    // The building is solid: its center collides.
    const ecs::Vec3 bc = city.buildings[0].center;
    CHECK(gm.hitsWall(ecs::Vec3{bc.x, 0.0f, bc.z}, 0.4f));

    // Determinism: converting the same city twice yields identical spawns.
    const game::SceneDescription scene2 = game::cityToScene(city);
    CHECK(scene.spawns.size() == scene2.spawns.size());
    bool identical = scene.spawns.size() == scene2.spawns.size();
    for (std::size_t i = 0; i < scene.spawns.size() && identical; ++i) {
        identical = scene.spawns[i].type == scene2.spawns[i].type &&
                    scene.spawns[i].position.x == scene2.spawns[i].position.x &&
                    scene.spawns[i].position.z == scene2.spawns[i].position.z;
    }
    CHECK(identical);

    // The imported map is playable and clearable: walk +X to grab the coin at the
    // corner, then +Z to the exit; every coin collected on the exit == a win.
    const float dt = 1.0f / 60.0f;
    int steps = 0;
    // Leg 1: head east until we are past the coin/corner (x ~ 44.5).
    while (gm.playerPosition.x < 44.0f && gm.status == game::GameStatus::Playing && steps < 4000) {
        gm.update(game::GameInput{1.0f, 0.0f}, dt);
        ++steps;
    }
    CHECK(gm.coinsCollected == 1); // grabbed the intersection coin en route
    // Leg 2: head north up the second street to the exit.
    while (gm.status == game::GameStatus::Playing && steps < 8000) {
        gm.update(game::GameInput{0.0f, 1.0f}, dt);
        ++steps;
    }
    CHECK(gm.won());
    CHECK(!gm.lost());

    if (g_failures == 0) {
        std::printf("test_city_game: all checks passed\n");
        return 0;
    }
    std::printf("test_city_game: %d check(s) failed\n", g_failures);
    return 1;
}
