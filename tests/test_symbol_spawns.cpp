// Test for the drawing-symbols -> game-spawns bridge - issue #314.
//
// Exercises the full chain that had no single test: a (rectified) RGB drawing with
// colored symbol blobs -> detectSymbols (recognize) -> placeSpawns (world XZ) ->
// loadGame (playable). Asserts each color is recognized as the right game object,
// lands at the right world cell, and yields the expected playable actors. Pure std +
// the header-only CV / game:
//   g++ -std=c++17 -I src tests/test_symbol_spawns.cpp -o test_symbol_spawns

#include "cv/Image.h"
#include "game/SymbolSpawns.h"

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

static bool approx(float a, float b, float eps = 1.0f) { return std::fabs(a - b) <= eps; }

// Fill a solid side x side square whose top-left corner is (x0, y0). Its centroid
// is (x0 + (side-1)/2, y0 + (side-1)/2).
static void fillSquare(cv::Image& img, int x0, int y0, int side, int r, int g, int b) {
    for (int y = y0; y < y0 + side; ++y) {
        for (int x = x0; x < x0 + side; ++x) {
            img.set(x, y, 0, static_cast<std::uint8_t>(r));
            img.set(x, y, 1, static_cast<std::uint8_t>(g));
            img.set(x, y, 2, static_cast<std::uint8_t>(b));
        }
    }
}

static const game::EntitySpawn* find(const std::vector<game::EntitySpawn>& v, const char* type) {
    for (const auto& s : v) if (s.type == type) return &s;
    return nullptr;
}

int main() {
    // Black background (uncolored); four saturated symbol squares, side 10, so each
    // centroid is at corner + 4.5.
    cv::Image img(80, 80, 3);
    fillSquare(img, 15, 15, 10, 0, 200, 0);   // green  -> player, centroid (19.5, 19.5)
    fillSquare(img, 55, 15, 10, 220, 0, 0);   // red    -> enemy,  centroid (59.5, 19.5)
    fillSquare(img, 15, 55, 10, 220, 220, 0); // yellow -> coin,   centroid (19.5, 59.5)
    fillSquare(img, 55, 55, 10, 0, 0, 220);   // blue   -> exit,   centroid (59.5, 59.5)

    game::SymbolSpawnOptions opt; // worldScale 1.0
    const std::vector<game::EntitySpawn> spawns = game::detectSpawns(img, opt);

    // Every symbol recognized and placed.
    CHECK(spawns.size() == 4);
    const game::EntitySpawn* player = find(spawns, "player");
    const game::EntitySpawn* enemy = find(spawns, "enemy");
    const game::EntitySpawn* coin = find(spawns, "coin");
    const game::EntitySpawn* exit = find(spawns, "exit");
    CHECK(player && enemy && coin && exit);

    // Colors mapped to the right game objects at the right world cells.
    if (player) CHECK(approx(player->position.x, 19.5f) && approx(player->position.z, 19.5f));
    if (enemy) CHECK(approx(enemy->position.x, 59.5f) && approx(enemy->position.z, 19.5f));
    if (coin) CHECK(approx(coin->position.x, 19.5f) && approx(coin->position.z, 59.5f));
    if (exit) CHECK(approx(exit->position.x, 59.5f) && approx(exit->position.z, 59.5f));

    // The recognized drawing loads into a playable game with the expected actors.
    game::DungeonGame gm = game::loadSymbolGame(img, opt);
    CHECK(gm.totalCoins == 1);
    CHECK(gm.enemies.size() == 1);
    CHECK(gm.hasExit);
    if (player) CHECK(approx(gm.playerPosition.x, 19.5f) && approx(gm.playerPosition.z, 19.5f));

    // worldScale actually scales placement.
    game::SymbolSpawnOptions half;
    half.worldScale = 0.5f;
    const std::vector<game::EntitySpawn> scaled = game::detectSpawns(img, half);
    const game::EntitySpawn* p2 = find(scaled, "player");
    CHECK(p2 && approx(p2->position.x, 9.75f) && approx(p2->position.z, 9.75f));

    // Determinism: same image -> identical spawns.
    const std::vector<game::EntitySpawn> again = game::detectSpawns(img, opt);
    bool identical = again.size() == spawns.size();
    for (std::size_t i = 0; i < spawns.size() && identical; ++i) {
        identical = again[i].type == spawns[i].type &&
                    again[i].position.x == spawns[i].position.x &&
                    again[i].position.z == spawns[i].position.z;
    }
    CHECK(identical);

    if (g_failures == 0) {
        std::printf("test_symbol_spawns: all checks passed\n");
        return 0;
    }
    std::printf("test_symbol_spawns: %d check(s) failed\n", g_failures);
    return 1;
}
