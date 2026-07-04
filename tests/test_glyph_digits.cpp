// Digit-glyph recognition for @id feature links - issue #361.
//
// Trains the GlyphNet classifier (#173) on a synthetic digit font, recognizes a digit drawn
// beside a linkable symbol, and forms "type@id" spawn strings - pairing a key and door that
// share the drawn digit, while an unreadable (smudged) digit falls back to auto-numbering
// rather than a wrong link. Pure std + the header-only CV / GlyphNet cores:
//   g++ -std=c++17 -I src tests/test_glyph_digits.cpp -o test_glyph_digits

#include "game/GlyphDigits.h"

#include <cstdio>
#include <string>
#include <vector>

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

int main() {
    const ml::GlyphClassifier clf = trainDigitClassifier();

    // 1. Every digit is recognized (at a trained scale and at an unseen scale - the
    //    normalization makes recognition scale/position invariant).
    for (int d = 0; d <= 9; ++d) {
        CHECK(recognizeDigit(clf, renderDigit(d, /*scale=*/3)) == d);
        CHECK(recognizeDigit(clf, renderDigit(d, /*scale=*/5, /*pad=*/4)) == d);
    }

    // 2. An empty / smudged crop is unreadable (-1), not a wrong digit.
    {
        cv::Mask smudge(12, 12); // no ink
        CHECK(recognizeDigit(clf, smudge) == -1);
    }

    // 3. A key and a door labelled with the same drawn digit pair up via @id.
    {
        const std::vector<LinkableSymbol> symbols = {
            {"key", 0.0f, 0.0f}, {"lockeddoor", 10.0f, 0.0f}, {"switch", 20.0f, 0.0f}};
        std::vector<DigitMark> marks;
        marks.push_back(DigitMark{renderDigit(2), 0.6f, 0.0f});   // "2" by the key
        marks.push_back(DigitMark{renderDigit(2), 10.6f, 0.0f});  // "2" by the door
        marks.push_back(DigitMark{cv::Mask(12, 12), 20.6f, 0.0f}); // a smudge by the switch

        const std::vector<std::string> out = linkDigitIds(symbols, marks, clf);
        CHECK(out.size() == 3);
        CHECK(out[0] == "key@2");
        CHECK(out[1] == "lockeddoor@2"); // the matching pair
        // The unreadable one is auto-numbered, never mispaired onto @2.
        CHECK(out[2] == "switch@1");
    }

    // 4. A different drawn digit links a distinct pair.
    {
        const std::vector<LinkableSymbol> symbols = {{"switch", 0.0f, 0.0f}, {"togglewall", 5.0f, 0.0f}};
        std::vector<DigitMark> marks = {DigitMark{renderDigit(7), 0.5f, 0.0f},
                                        DigitMark{renderDigit(7), 5.5f, 0.0f}};
        const std::vector<std::string> out = linkDigitIds(symbols, marks, clf);
        CHECK(out[0] == "switch@7");
        CHECK(out[1] == "togglewall@7");
    }

    if (g_failures == 0) {
        std::printf("test_glyph_digits: all checks passed\n");
        return 0;
    }
    std::printf("test_glyph_digits: %d check(s) failed\n", g_failures);
    return 1;
}
