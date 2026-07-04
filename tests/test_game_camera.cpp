// Game camera and theme pass for Doodlebound - issue #354 (headless core).
//
// Verifies the play-view camera frames the player (top-down and follow), tracks the player
// as it moves, smooths toward its goal, and frames a whole level to fit its bounds; and that
// the theme palette gives distinct, cohesive colors per material with a fallback to the
// default palette. The GL render from the camera + theme is exercised by test_game_camera_gl.
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_game_camera.cpp -o test_game_camera

#include "game/GameCamera.h"

#include <cmath>
#include <cstdio>

using namespace IKore;
using namespace IKore::game;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    // 1. Top-down frames directly above the player and tracks it.
    {
        GameCamera cam;
        cam.mode = GameCameraMode::TopDown;
        cam.height = 12.0f;
        cam.update(ecs::Vec3{3, 0, 5});
        CHECK(near(cam.eye.x, 3.0f) && near(cam.eye.z, 5.0f));
        CHECK(near(cam.eye.y, 12.0f));
        CHECK(near(cam.target.x, 3.0f) && near(cam.target.z, 5.0f));
        CHECK(near(cam.up.z, 1.0f) && near(cam.up.y, 0.0f)); // top-down up is +Z

        cam.update(ecs::Vec3{6, 0, 5}); // player moved east
        CHECK(near(cam.eye.x, 6.0f) && near(cam.target.x, 6.0f));
    }

    // 2. Follow frames from behind and above, looking at the player.
    {
        GameCamera cam;
        cam.mode = GameCameraMode::Follow;
        cam.followDist = 8.0f;
        cam.followHeight = 7.0f;
        cam.update(ecs::Vec3{2, 0, 2});
        CHECK(near(cam.eye.x, 2.0f));
        CHECK(near(cam.eye.y, 7.0f));
        CHECK(near(cam.eye.z, 2.0f - 8.0f));
        CHECK(near(cam.target.x, 2.0f) && near(cam.target.z, 2.0f));
        CHECK(near(cam.up.y, 1.0f)); // follow up is +Y
    }

    // 3. Smoothing eases toward the goal instead of snapping.
    {
        GameCamera cam;
        cam.mode = GameCameraMode::TopDown;
        cam.height = 10.0f;
        cam.smooth = 0.5f;
        cam.eye = ecs::Vec3{0, 0, 0};
        cam.target = ecs::Vec3{0, 0, 0};
        cam.update(ecs::Vec3{10, 0, 0}); // goal eye x = 10
        CHECK(near(cam.eye.x, 5.0f));    // moved halfway
        cam.update(ecs::Vec3{10, 0, 0});
        CHECK(cam.eye.x > 5.0f && cam.eye.x < 10.0f); // converging, not snapping
    }

    // 4. frameLevel fits the level bounds: a bigger level lifts the eye higher and centers it.
    {
        SceneDescription small, big;
        world::Box a; a.center = ecs::Vec3{0, 0, 0}; a.size = ecs::Vec3{1, 1, 1};
        world::Box b; b.center = ecs::Vec3{6, 0, 6}; b.size = ecs::Vec3{1, 1, 1};
        small.wallBoxes = {a, b};
        world::Box c; c.center = ecs::Vec3{0, 0, 0}; c.size = ecs::Vec3{1, 1, 1};
        world::Box d; d.center = ecs::Vec3{30, 0, 30}; d.size = ecs::Vec3{1, 1, 1};
        big.wallBoxes = {c, d};
        small.spawns = {EntitySpawn{"player", ecs::Vec3{0, 0, 0}, 0.0f}};
        big.spawns = {EntitySpawn{"player", ecs::Vec3{0, 0, 0}, 0.0f}};

        ecs::Vec3 minB, maxB;
        GameCamera camS;
        levelBounds(loadGame(small), minB, maxB);
        camS.frameLevel(minB, maxB);
        GameCamera camB;
        levelBounds(loadGame(big), minB, maxB);
        camB.frameLevel(minB, maxB);
        CHECK(camB.eye.y > camS.eye.y);        // bigger level -> higher eye
        CHECK(near(camB.target.x, 15.0f));     // centered on the bounds
        CHECK(near(camB.eye.x, 15.0f));
    }

    // 5. Theme palette: distinct cohesive colors, with a fallback to the default palette.
    {
        const Theme dun = dungeonTheme();
        const Theme grs = grassTheme();
        CHECK(dun.name == "dungeon" && grs.name == "grass");
        // The two themes are visibly different.
        CHECK(!(dun.background.r == grs.background.r && dun.background.g == grs.background.g &&
                dun.background.b == grs.background.b));
        CHECK(!(dun.wall.r == grs.wall.r && dun.wall.g == grs.wall.g && dun.wall.b == grs.wall.b));
        // themedColor returns the theme's color for core types...
        const RenderColor w = themedColor("wall", dun);
        CHECK(w.r == dun.wall.r && w.g == dun.wall.g && w.b == dun.wall.b);
        const RenderColor p = themedColor("player", grs);
        CHECK(p.r == grs.player.r);
        // ...and falls back to the default palette for types it does not override.
        const RenderColor key = themedColor("key", dun);
        const RenderColor keyDefault = renderColorForType("key");
        CHECK(key.r == keyDefault.r && key.g == keyDefault.g && key.b == keyDefault.b);
    }

    if (g_failures == 0) {
        std::printf("test_game_camera: all checks passed\n");
        return 0;
    }
    std::printf("test_game_camera: %d check(s) failed\n", g_failures);
    return 1;
}
