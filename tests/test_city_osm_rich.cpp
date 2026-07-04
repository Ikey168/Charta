// Richer OSM import into CityGame - issue #365.
//
// The GeoJSON importer now classifies regions (water/park/grass), imports Point POIs, and
// imports barrier ways; CityGame turns that richer data into gameplay: barrier ways become
// thin wall obstacles, water regions are tiled with hazards so stepping into water is a
// loss, and map POIs become the preferred coin placements. Verifies parsing, the scene
// conversion, and the resulting playable behavior. Pure std + the header-only importer /
// game:
//   g++ -std=c++17 -I src tests/test_city_osm_rich.cpp -o test_city_osm_rich

#include "game/CityGame.h"
#include "world/GeoJsonImporter.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

static bool approx(float a, float b, float eps = 0.1f) { return std::fabs(a - b) <= eps; }

// Origin = first coordinate (0,0), lat0=0 so 1 deg = 111320 m (square). Layout (meters):
//   road:    (0,0) -> (89.056,0)                 along +X at z=0 (spawn at 0, exit at 89)
//   water:   x[11.132,33.396]  z[22.264,44.528]  natural=water (north of the road)
//   park:    x[55.660,77.924]  z[22.264,44.528]  leisure=park
//   barrier: x=66.792  z[-11.132,11.132]         barrier=fence crossing the road
//   pois:    cafe @ (22.264,0), fountain @ (44.528,0)  (on the road, before the barrier)
static const char* kGeoJson = R"JSON({
  "type": "FeatureCollection",
  "features": [
    { "type":"Feature", "properties":{"highway":"residential"},
      "geometry":{"type":"LineString","coordinates":[[0,0],[0.0008,0]]} },
    { "type":"Feature", "properties":{"natural":"water"},
      "geometry":{"type":"Polygon","coordinates":[[[0.0001,0.0002],[0.0003,0.0002],[0.0003,0.0004],[0.0001,0.0004],[0.0001,0.0002]]]} },
    { "type":"Feature", "properties":{"leisure":"park"},
      "geometry":{"type":"Polygon","coordinates":[[[0.0005,0.0002],[0.0007,0.0002],[0.0007,0.0004],[0.0005,0.0004],[0.0005,0.0002]]]} },
    { "type":"Feature", "properties":{"barrier":"fence"},
      "geometry":{"type":"LineString","coordinates":[[0.0006,-0.0001],[0.0006,0.0001]]} },
    { "type":"Feature", "properties":{"amenity":"cafe","name":"Corner Cafe"},
      "geometry":{"type":"Point","coordinates":[0.0002,0]} },
    { "type":"Feature", "properties":{"amenity":"fountain"},
      "geometry":{"type":"Point","coordinates":[0.0004,0]} }
  ]
})JSON";

static int countType(const game::SceneDescription& s, const char* t) {
    int n = 0;
    for (const auto& sp : s.spawns) if (sp.type == t) ++n;
    return n;
}

int main() {
    const world::City city = world::importString(kGeoJson, world::GeoImportOptions{});

    // 1. Regions are classified; POIs and barriers are imported as new geometry.
    CHECK(city.roads.size() == 1);
    CHECK(city.buildings.empty());
    CHECK(city.regions.size() == 2);
    CHECK(city.regions[0].kind == world::RegionKind::Water);
    CHECK(city.regions[1].kind == world::RegionKind::Park);
    CHECK(city.pois.size() == 2);
    CHECK(city.pois[0].category == "cafe" && city.pois[0].name == "Corner Cafe");
    CHECK(city.pois[1].category == "fountain");
    CHECK(city.barriers.size() == 1);
    CHECK(city.barriers[0].kind == "fence");
    CHECK(city.barriers[0].line.size() == 2);

    const game::SceneDescription scene = game::cityToScene(city);

    // 2. POIs are the preferred coin source (two POIs -> two coins, at the POI positions).
    CHECK(countType(scene, "coin") == 2);
    CHECK(countType(scene, "player") == 1 && countType(scene, "exit") == 1);
    // Find the coins and confirm they sit on the POIs.
    bool cafeCoin = false, fountainCoin = false;
    for (const auto& sp : scene.spawns) {
        if (sp.type != "coin") continue;
        if (approx(sp.position.x, 22.264f) && approx(sp.position.z, 0.0f)) cafeCoin = true;
        if (approx(sp.position.x, 44.528f) && approx(sp.position.z, 0.0f)) fountainCoin = true;
    }
    CHECK(cafeCoin && fountainCoin);

    // 3. The barrier became exactly one thin wall box (no buildings in this map).
    CHECK(scene.wallBoxes.size() == 1);

    // 4. Water is tiled with hazards; every hazard lies within the water footprint (x < 40,
    //    z in ~[22,45]) and none appear in the park's x-range (>= 55).
    const int hazards = countType(scene, "hazard");
    CHECK(hazards > 0);
    ecs::Vec3 aHazard{};
    for (const auto& sp : scene.spawns) {
        if (sp.type != "hazard") continue;
        aHazard = sp.position;
        CHECK(sp.position.x >= 11.0f && sp.position.x <= 34.0f);
        CHECK(sp.position.z >= 22.0f && sp.position.z <= 45.0f);
    }

    // 5. The imported map is playable: spawn is clear, the barrier is solid, off to the side
    //    it is not.
    game::DungeonGame gm = game::loadCityGame(city);
    CHECK(gm.totalCoins == 2);
    CHECK(gm.hasExit);
    CHECK(!gm.hitsWall(gm.playerPosition, gm.playerRadius)); // spawns on the road
    CHECK(gm.hitsWall(ecs::Vec3{66.792f, 0.0f, 0.0f}, 0.4f)); // barrier crosses the road
    CHECK(!gm.hitsWall(ecs::Vec3{66.792f, 0.0f, 20.0f}, 0.4f)); // ...but only where it spans

    // 6. Stepping into water is a loss; standing on the road is not.
    {
        game::DungeonGame water = game::loadCityGame(city);
        water.playerPosition = aHazard; // stand in the water
        water.update(game::GameInput{0.0f, 0.0f}, 1.0f / 60.0f);
        CHECK(water.lost());
    }
    {
        game::DungeonGame safe = game::loadCityGame(city);
        safe.update(game::GameInput{0.0f, 0.0f}, 1.0f / 60.0f);
        CHECK(!safe.lost());
    }

    // 7. Driving east collects both POI coins, then the fence blocks further progress.
    {
        game::DungeonGame drive = game::loadCityGame(city);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 6000 && drive.status == game::GameStatus::Playing; ++i)
            drive.update(game::GameInput{1.0f, 0.0f}, dt);
        CHECK(drive.coinsCollected == 2);       // both roadside POIs grabbed
        CHECK(drive.playerPosition.x < 67.0f);  // stopped at the fence
        CHECK(drive.playerPosition.x > 60.0f);  // ...having reached it
        CHECK(!drive.won());                    // exit is beyond the fence
    }

    // 8. Determinism: converting the same city twice yields identical spawns.
    const game::SceneDescription scene2 = game::cityToScene(city);
    bool identical = scene.spawns.size() == scene2.spawns.size();
    for (std::size_t i = 0; i < scene.spawns.size() && identical; ++i)
        identical = scene.spawns[i].type == scene2.spawns[i].type &&
                    scene.spawns[i].position.x == scene2.spawns[i].position.x &&
                    scene.spawns[i].position.z == scene2.spawns[i].position.z;
    CHECK(identical);

    if (g_failures == 0) {
        std::printf("test_city_osm_rich: all checks passed\n");
        return 0;
    }
    std::printf("test_city_osm_rich: %d check(s) failed\n", g_failures);
    return 1;
}
