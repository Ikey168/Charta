// Test for the extended color+shape drawing vocabulary - issue #336.
//
// Verifies that with extendedVocabulary the recognizer maps color+shape to the richer
// DungeonGame types (keys, locked doors, switches, hazards, and enemy behaviors), that the
// Tier-1 color-only mapping is unchanged when it is off, and that the recognized types load
// into a playable game with those features. Pure std + the header-only CV / game:
//   g++ -std=c++17 -I src tests/test_symbol_vocabulary.cpp -o test_symbol_vocabulary

#include "cv/Image.h"
#include "game/SymbolSpawns.h"

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

static void px(cv::Image& img, int x, int y, int r, int g, int b) {
    if (!img.inBounds(x, y)) return;
    img.set(x, y, 0, static_cast<std::uint8_t>(r));
    img.set(x, y, 1, static_cast<std::uint8_t>(g));
    img.set(x, y, 2, static_cast<std::uint8_t>(b));
}
// Filled square: fill ratio 1.0 -> Square.
static void square(cv::Image& img, int x0, int y0, int s, int r, int g, int b) {
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x) px(img, x0 + x, y0 + y, r, g, b);
}
// Right triangle: fill ratio ~0.54 -> Triangle.
static void triangle(cv::Image& img, int x0, int y0, int s, int r, int g, int b) {
    for (int y = 0; y < s; ++y)
        for (int x = 0; x <= y && x < s; ++x) px(img, x0 + x, y0 + y, r, g, b);
}
// Disk: fill ratio ~0.785 -> Circle.
static void circle(cv::Image& img, int x0, int y0, int s, int r, int g, int b) {
    const float c = (s - 1) / 2.0f, rad = s / 2.0f;
    for (int y = 0; y < s; ++y)
        for (int x = 0; x < s; ++x)
            if ((x - c) * (x - c) + (y - c) * (y - c) <= rad * rad) px(img, x0 + x, y0 + y, r, g, b);
}
// Plus/cross (arm thickness 2): fill ratio ~0.3 -> Cross.
static void cross(cv::Image& img, int x0, int y0, int s, int r, int g, int b) {
    const int t = 2, lo = (s - t) / 2;
    for (int y = 0; y < s; ++y)
        for (int x = lo; x < lo + t; ++x) px(img, x0 + x, y0 + y, r, g, b); // vertical bar
    for (int x = 0; x < s; ++x)
        for (int y = lo; y < lo + t; ++y) px(img, x0 + x, y0 + y, r, g, b); // horizontal bar
}

static int countType(const std::vector<game::EntitySpawn>& v, const char* t) {
    int n = 0;
    for (const auto& s : v) if (s.type == t) ++n;
    return n;
}

// Draw the vocabulary sample: 8 shapes on a black background, 24px cells.
static cv::Image makeVocabularyImage() {
    cv::Image img(96, 48, 3);
    const int G[3] = {0, 200, 0}, R[3] = {220, 0, 0}, Y[3] = {220, 220, 0}, B[3] = {0, 0, 220};
    // Row 0
    square(img, 2, 2, 12, G[0], G[1], G[2]);       // green square  -> player
    square(img, 26, 2, 12, R[0], R[1], R[2]);      // red square    -> enemy
    triangle(img, 50, 2, 12, R[0], R[1], R[2]);    // red triangle  -> enemy_ranged
    triangle(img, 74, 2, 12, Y[0], Y[1], Y[2]);    // yellow triangle -> key
    // Row 1
    triangle(img, 2, 26, 12, B[0], B[1], B[2]);    // blue triangle -> lockeddoor
    cross(img, 26, 26, 12, B[0], B[1], B[2]);      // blue cross    -> switch
    cross(img, 50, 26, 12, G[0], G[1], G[2]);      // green cross   -> hazard
    circle(img, 74, 26, 12, R[0], R[1], R[2]);     // red circle    -> enemy_flee
    return img;
}

int main() {
    const cv::Image img = makeVocabularyImage();

    // --- Extended vocabulary: color+shape -> richer types ----------------------
    game::SymbolSpawnOptions ext;
    ext.extendedVocabulary = true;
    const std::vector<game::EntitySpawn> spawns = game::detectSpawns(img, ext);
    CHECK(spawns.size() == 8);
    CHECK(countType(spawns, "player") == 1);
    CHECK(countType(spawns, "enemy") == 1);          // the red square
    CHECK(countType(spawns, "enemy_ranged") == 1);
    CHECK(countType(spawns, "enemy_flee") == 1);
    CHECK(countType(spawns, "key") == 1);
    CHECK(countType(spawns, "lockeddoor") == 1);
    CHECK(countType(spawns, "switch") == 1);
    CHECK(countType(spawns, "hazard") == 1);

    // --- Tier-1 mapping unchanged when the extended vocabulary is off ----------
    const std::vector<game::EntitySpawn> tier1 = game::detectSpawns(img, game::SymbolSpawnOptions{});
    CHECK(tier1.size() == 8);
    CHECK(countType(tier1, "enemy") == 3);           // all three reds fall back to enemy
    CHECK(countType(tier1, "exit") == 2);            // both blues are just exit
    CHECK(countType(tier1, "coin") == 1);            // the yellow triangle is just coin
    CHECK(countType(tier1, "key") == 0);             // no richer types without the flag
    CHECK(countType(tier1, "hazard") == 0);
    CHECK(countType(tier1, "player") == 2);          // green square + green cross both player

    // --- Recognized types load into a playable game with those features --------
    game::DungeonGame g = game::loadGame(game::symbolsToScene(img, ext));
    CHECK(g.keys.size() == 1);
    CHECK(g.lockedDoors.size() == 1);
    CHECK(g.switches.size() == 1);
    CHECK(g.hazards.size() == 1);
    CHECK(g.enemies.size() == 3); // chase + ranged + flee
    bool hasRanged = false, hasFlee = false, hasChase = false;
    for (const game::Enemy& e : g.enemies) {
        hasRanged = hasRanged || e.behavior == game::EnemyBehavior::Ranged;
        hasFlee = hasFlee || e.behavior == game::EnemyBehavior::Flee;
        hasChase = hasChase || e.behavior == game::EnemyBehavior::Chase;
    }
    CHECK(hasRanged && hasFlee && hasChase);

    if (g_failures == 0) {
        std::printf("test_symbol_vocabulary: all checks passed\n");
        return 0;
    }
    std::printf("test_symbol_vocabulary: %d check(s) failed\n", g_failures);
    return 1;
}
