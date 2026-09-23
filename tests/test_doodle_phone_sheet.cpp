// End-to-end regression for the four-color printed Android demo sheet.
// Build independently of desktop graphics: g++ -std=c++17 -O2 -Isrc
// tests/test_doodle_phone_sheet.cpp -o test_doodle_phone_sheet
#include "doodle/Doodle.h"

#include <cstdio>
#include <cstdint>
#include <string>

using IKore::doodle::Image;

static void pixel(Image& image, int x, int y, int r, int g, int b) {
    if (!image.inBounds(x, y)) return;
    image.set(x, y, 0, static_cast<std::uint8_t>(r));
    image.set(x, y, 1, static_cast<std::uint8_t>(g));
    image.set(x, y, 2, static_cast<std::uint8_t>(b));
}

static void disk(Image& image, int x, int y, int radius, int r, int g, int b) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) pixel(image, x + dx, y + dy, r, g, b);
        }
    }
}

static void line(Image& image, int x1, int y1, int x2, int y2) {
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) disk(image, x, y, 1, 24, 24, 24);
    }
}

static bool convertsToFourObjects(const Image& image, const char* label) {
    IKore::doodle::Options options;
    options.rectifyFirst = true; // match the Android photo-import path
    options.worldScale = 0.05f;
    const auto level = IKore::doodle::interpretPhoto(image, options);
    if (!level.readyToPlay() || level.walls.empty() || level.symbols.size() != 4) {
        std::printf("FAIL (%s): expected a review-ready four-symbol level\n", label);
        return false;
    }
    for (const char* type : {"player", "exit", "coin", "enemy"}) {
        int count = 0;
        for (const auto& symbol : level.symbols) count += symbol.type == type;
        if (count != 1) {
            std::printf("FAIL (%s): expected one %s, found %d\n", label, type, count);
            return false;
        }
    }
    const auto scene = IKore::doodle::buildScene(level);
    if (scene.wallBoxes.empty() || scene.spawns.size() != 4) {
        std::printf("FAIL (%s): level did not produce a playable scene\n", label);
        return false;
    }
    return true;
}

int main() {
    Image image(256, 256, 3);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) pixel(image, x, y, 255, 255, 255);

    // A three-room floor plan with two visible door gaps, matching the printable sheet.
    line(image, 23, 23, 233, 23);
    line(image, 23, 233, 233, 233);
    line(image, 23, 23, 23, 233);
    line(image, 233, 23, 233, 233);
    line(image, 93, 23, 93, 116);
    line(image, 93, 140, 93, 233);
    line(image, 163, 23, 163, 116);
    line(image, 163, 140, 163, 233);
    disk(image, 55, 128, 8, 30, 164, 54);    // green start
    disk(image, 128, 128, 7, 223, 180, 0);   // yellow coin
    disk(image, 128, 189, 7, 226, 43, 43);   // red enemy
    disk(image, 201, 128, 8, 40, 97, 220);   // blue exit

    if (!convertsToFourObjects(image, "normal")) return 1;
    for (int brightness : {65, 135}) {
        Image variant = image;
        for (auto& channel : variant.data) {
            channel = static_cast<std::uint8_t>(std::min(255, channel * brightness / 100));
        }
        if (!convertsToFourObjects(variant, brightness < 100 ? "dim" : "bright")) return 1;
    }
    std::printf("PASS: normal, dim and bright three-room sheets convert to playable scenes\n");
    return 0;
}
