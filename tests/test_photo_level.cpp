// Test for the photographed-drawing -> playable path - issue #315.
//
// Proves the de-warp step (Rectify #166) lets the symbol pipeline survive a photo
// taken at an angle: a flat drawing with four colored symbols is warped onto a
// convex quad over a dark background (a synthetic angled photo), then
// detectSpawnsFromPhoto rectifies and recognizes it. The four symbols must come
// back with the right types and the right corner layout (de-warp preserves both).
// Pure std + the header-only CV / game:
//   g++ -std=c++17 -I src tests/test_photo_level.cpp -o test_photo_level

#include "cv/Image.h"
#include "cv/Rectify.h" // computeHomography / applyHomography for building the photo
#include "game/PhotoLevel.h"

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

static void fillSquare(cv::Image& img, int x0, int y0, int side, int r, int g, int b) {
    for (int y = y0; y < y0 + side; ++y)
        for (int x = x0; x < x0 + side; ++x) {
            img.set(x, y, 0, static_cast<std::uint8_t>(r));
            img.set(x, y, 1, static_cast<std::uint8_t>(g));
            img.set(x, y, 2, static_cast<std::uint8_t>(b));
        }
}

static const game::EntitySpawn* find(const std::vector<game::EntitySpawn>& v, const char* t) {
    for (const auto& s : v) if (s.type == t) return &s;
    return nullptr;
}

int main() {
    // Flat white "paper" drawing, 80x80, with four inset colored symbols so the paper
    // corners stay white (the bright extremes the detector locks onto).
    cv::Image flat(80, 80, 3);
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 80; ++x)
            for (int c = 0; c < 3; ++c) flat.set(x, y, c, 255);
    fillSquare(flat, 10, 10, 16, 0, 200, 0);   // green  -> player (top-left)
    fillSquare(flat, 54, 10, 16, 220, 220, 0); // yellow -> coin   (top-right)
    fillSquare(flat, 10, 54, 16, 220, 0, 0);   // red    -> enemy  (bottom-left)
    fillSquare(flat, 54, 54, 16, 0, 0, 220);   // blue   -> exit   (bottom-right)

    // Synthetic angled photo: dark background, the drawing painted onto a convex quad.
    cv::Image photo(220, 220, 3);
    for (int y = 0; y < photo.height; ++y)
        for (int x = 0; x < photo.width; ++x) {
            photo.set(x, y, 0, 26);
            photo.set(x, y, 1, 28);
            photo.set(x, y, 2, 24);
        }
    const cv::Point quad[4] = {{45, 35}, {180, 60}, {165, 195}, {35, 170}};
    const cv::Point flatCorners[4] = {{0, 0}, {80, 0}, {80, 80}, {0, 80}};
    const cv::Mat3 photoToFlat = cv::computeHomography(quad, flatCorners);
    for (int y = 0; y < photo.height; ++y)
        for (int x = 0; x < photo.width; ++x) {
            const cv::Point p = cv::applyHomography(photoToFlat, static_cast<float>(x), static_cast<float>(y));
            if (p.x >= 0 && p.x < 80 && p.y >= 0 && p.y < 80)
                for (int c = 0; c < 3; ++c) photo.set(x, y, c, cv::clampByte(flat.sample(p.x, p.y, c)));
        }

    game::PhotoLevelOptions opt;
    opt.rectifiedSize = 160;
    const std::vector<game::EntitySpawn> spawns = game::detectSpawnsFromPhoto(photo, opt);

    // All four symbols survived the de-warp and were recognized.
    CHECK(spawns.size() == 4);
    const game::EntitySpawn* player = find(spawns, "player");
    const game::EntitySpawn* coin = find(spawns, "coin");
    const game::EntitySpawn* enemy = find(spawns, "enemy");
    const game::EntitySpawn* exit = find(spawns, "exit");
    CHECK(player && coin && enemy && exit);

    // Corner layout preserved through rectification (rectified to 160, halves at 80):
    // player top-left, coin top-right, enemy bottom-left, exit bottom-right.
    const float mid = 80.0f;
    if (player) CHECK(player->position.x < mid && player->position.z < mid);
    if (coin) CHECK(coin->position.x > mid && coin->position.z < mid);
    if (enemy) CHECK(enemy->position.x < mid && enemy->position.z > mid);
    if (exit) CHECK(exit->position.x > mid && exit->position.z > mid);

    // The photographed drawing loads into a playable game with the expected actors.
    game::DungeonGame gm = game::loadPhotoGame(photo, opt);
    CHECK(gm.totalCoins == 1);
    CHECK(gm.enemies.size() == 1);
    CHECK(gm.hasExit);

    // Determinism: same photo -> identical spawns.
    const std::vector<game::EntitySpawn> again = game::detectSpawnsFromPhoto(photo, opt);
    bool identical = again.size() == spawns.size();
    for (std::size_t i = 0; i < spawns.size() && identical; ++i)
        identical = again[i].type == spawns[i].type &&
                    again[i].position.x == spawns[i].position.x &&
                    again[i].position.z == spawns[i].position.z;
    CHECK(identical);

    if (g_failures == 0) {
        std::printf("test_photo_level: all checks passed\n");
        return 0;
    }
    std::printf("test_photo_level: %d check(s) failed\n", g_failures);
    return 1;
}
