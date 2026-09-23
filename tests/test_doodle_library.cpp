// Test for the portable Doodle converter library - Milestone 17, #171.
//
// Verifies the issue's acceptance criteria: the library builds independently of
// the desktop renderer (this file includes ONLY the public umbrella header and
// links only the header-only `doodle` target - no renderer, no glm, no OpenGL),
// and it is covered by isolated unit tests exercising the public facade end to end.
//
//   g++ -std=c++17 -I src tests/test_doodle_library.cpp -o test_doodle_library

#include "doodle/Doodle.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static void putRGB(doodle::Image& im, int x, int y, int R, int G, int B) {
    if (!im.inBounds(x, y)) return;
    im.set(x, y, 0, static_cast<std::uint8_t>(R));
    im.set(x, y, 1, static_cast<std::uint8_t>(G));
    im.set(x, y, 2, static_cast<std::uint8_t>(B));
}

static doodle::Image whiteImage(int w, int h) {
    doodle::Image im(w, h, 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) putRGB(im, x, y, 250, 250, 250);
    }
    return im;
}

// Draw a thick dark rectangle outline (walls) into a top-down image.
static void drawRoom(doodle::Image& im, int x0, int y0, int x1, int y1) {
    for (int t = -1; t <= 1; ++t) {
        for (int x = x0; x <= x1; ++x) {
            putRGB(im, x, y0 + t, 20, 20, 20);
            putRGB(im, x, y1 + t, 20, 20, 20);
        }
        for (int y = y0; y <= y1; ++y) {
            putRGB(im, x0 + t, y, 20, 20, 20);
            putRGB(im, x1 + t, y, 20, 20, 20);
        }
    }
}

static void testSceneAndJsonFacade() {
    doodle::LevelSpec spec;
    spec.walls.push_back(doodle::Wall{{{10, 0, 10}, {40, 0, 10}, {40, 0, 40}, {10, 0, 40}, {10, 0, 10}}});
    spec.symbols.push_back(doodle::Symbol{"player", doodle::Vec3{15, 0, 15}, 0.0f});
    spec.symbols.push_back(doodle::Symbol{"exit", doodle::Vec3{35, 0, 35}, 0.0f});

    const doodle::SceneDescription scene = doodle::buildScene(spec);
    CHECK(!scene.wallBoxes.empty());        // walls extruded to geometry
    CHECK(!scene.wallMesh.vertices.empty());
    CHECK(scene.spawns.size() == 2);

    // JSON round-trips through the library facade.
    const std::string json = doodle::saveLevel(spec);
    doodle::LevelSpec reloaded;
    CHECK(doodle::loadLevel(json, reloaded));
    CHECK(!doodle::loadLevel(json + " trailing data", reloaded));
    CHECK(reloaded.walls.size() == spec.walls.size());
    CHECK(reloaded.symbols.size() == spec.symbols.size());
    CHECK(doodle::buildScene(reloaded).spawns.size() == 2);
}

static void testInterpretPhotoWalls() {
    doodle::Image top = whiteImage(90, 90);
    drawRoom(top, 20, 20, 70, 70);

    doodle::Options opt;
    opt.rectifyFirst = false; // already a top-down drawing
    const doodle::InterpretedLevel level = doodle::interpretPhoto(top, opt);
    CHECK(!level.walls.empty()); // the room outline was vectorized into walls
}

static void testInterpretPhotoSymbol() {
    doodle::Image top = whiteImage(60, 60);
    for (int y = 23; y <= 37; ++y) {
        for (int x = 23; x <= 37; ++x) {
            if ((x - 30) * (x - 30) + (y - 30) * (y - 30) <= 49) putRGB(top, x, y, 30, 180, 30);
        }
    }
    doodle::Options opt;
    opt.rectifyFirst = false;
    const doodle::InterpretedLevel level = doodle::interpretPhoto(top, opt);
    CHECK(level.hasSymbolType("player")); // the green mark became a player start
}

static void testEndToEndPlayable() {
    doodle::Image top = whiteImage(90, 90);
    drawRoom(top, 20, 20, 70, 70);

    doodle::Options opt;
    opt.rectifyFirst = false;
    doodle::InterpretedLevel level = doodle::interpretPhoto(top, opt);

    // The host reviews and places the required objects.
    level.addSymbol("player", doodle::Vec3{30, 0, 30});
    level.addSymbol("exit", doodle::Vec3{60, 0, 60});
    level.addSymbol("coin", doodle::Vec3{45, 0, 45});
    CHECK(level.readyToPlay());

    const doodle::SceneDescription scene = doodle::buildScene(level);
    const doodle::DungeonGame game = doodle::loadGame(scene);
    CHECK(game.hasExit);
    CHECK(game.totalCoins == 1);
    CHECK(game.playerPosition.x == 30.0f && game.playerPosition.z == 30.0f);
}

int main() {
    testSceneAndJsonFacade();
    testInterpretPhotoWalls();
    testInterpretPhotoSymbol();
    testEndToEndPlayable();

    if (g_failures == 0) {
        std::printf("All doodle library tests passed.\n");
        return 0;
    }
    std::printf("%d doodle library test(s) failed.\n", g_failures);
    return 1;
}
