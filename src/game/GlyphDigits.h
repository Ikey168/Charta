#pragma once

#include "cv/Vectorize.h"  // cv::Mask
#include "ml/GlyphNet.h"   // ml::normalizeGlyph, ml::GlyphClassifier

#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>

/**
 * @file GlyphDigits.h
 * @brief Read a hand-drawn digit next to a linkable symbol to form its @id (issue #361).
 *
 * The drawing vocabulary links features by an @id (a drawn key opens the matching drawn door,
 * #319/#336), but the id had to be supplied out of band. This lets players write the link on
 * paper: it trains the GlyphNet classifier (#173) on a small digit font, recognizes the digit
 * drawn beside a linkable symbol, and emits the correct "type@id" spawn string. An unreadable
 * digit falls back to auto-numbering rather than a wrong link, so a smudge never mispairs.
 *
 * Header-only, deterministic (the classifier trains from a fixed synthetic font with a fixed
 * seed), and dependency-free beyond the header-only CV / GlyphNet cores - unit-testable
 * headless with no external model.
 */
namespace IKore {
namespace game {

/// A 5x7 bitmap for digit @p d (0-9); rows top-to-bottom, '#' = ink.
inline std::array<const char*, 7> digitFont(int d) {
    static const std::array<std::array<const char*, 7>, 10> kFont = {{
        {{".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}}, // 0
        {{"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}}, // 1
        {{".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}}, // 2
        {{"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}}, // 3
        {{"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}}, // 4
        {{"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}}, // 5
        {{"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}}, // 6
        {{"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}}, // 7
        {{".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}}, // 8
        {{".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}}, // 9
    }};
    return kFont[static_cast<std::size_t>(d < 0 ? 0 : (d > 9 ? 9 : d))];
}

/// Rasterize digit @p d into an ink mask, each font pixel a @p scale square, @p pad border.
inline cv::Mask renderDigit(int d, int scale = 3, int pad = 2) {
    const auto font = digitFont(d);
    cv::Mask m(5 * scale + 2 * pad, 7 * scale + 2 * pad);
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 5; ++c)
            if (font[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] == '#')
                for (int yy = 0; yy < scale; ++yy)
                    for (int xx = 0; xx < scale; ++xx)
                        m.set(pad + c * scale + xx, pad + r * scale + yy, 1);
    return m;
}

/// Train a digit classifier (0-9) on the synthetic font at a few scales. Deterministic.
inline ml::GlyphClassifier trainDigitClassifier(int size = 12) {
    std::vector<std::vector<float>> X;
    std::vector<int> y;
    const std::vector<std::string> labels = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int d = 0; d <= 9; ++d)
        for (int scale : {2, 3, 4}) {
            X.push_back(ml::normalizeGlyph(renderDigit(d, scale), size));
            y.push_back(d);
        }
    ml::GlyphClassifier clf;
    clf.train(X, y, labels, /*epochs=*/400, /*lr=*/0.5f, /*seed=*/1);
    return clf;
}

/// Recognize a single digit from an ink mask; -1 if below @p minConfidence (unreadable).
inline int recognizeDigit(const ml::GlyphClassifier& clf, const cv::Mask& mask, int size = 12,
                          float minConfidence = 0.6f) {
    const ml::GlyphClassifier::Result r = clf.classifyGlyph(mask, size, minConfidence);
    if (r.lowConfidence || r.label.empty()) return -1;
    return r.label[0] - '0';
}

// --- @id linking -------------------------------------------------------------

struct LinkableSymbol {
    std::string type; ///< "key" / "lockeddoor" / "switch" / "togglewall".
    float x{0.0f}, z{0.0f};
};
struct DigitMark {
    cv::Mask mask;
    float x{0.0f}, z{0.0f};
};

/**
 * @brief Assign each linkable symbol an @id and emit "type@id" spawn strings. A recognized
 *        digit is attached to its nearest symbol; symbols left without a confident digit are
 *        auto-numbered with the smallest unused id (so an unreadable digit never mispairs).
 */
inline std::vector<std::string> linkDigitIds(const std::vector<LinkableSymbol>& symbols,
                                             const std::vector<DigitMark>& marks,
                                             const ml::GlyphClassifier& clf, int size = 12,
                                             float minConfidence = 0.6f) {
    std::vector<int> id(symbols.size(), -1);
    for (const DigitMark& mk : marks) {
        const int d = recognizeDigit(clf, mk.mask, size, minConfidence);
        if (d < 0 || symbols.empty()) continue;
        std::size_t best = 0;
        float bestDist = 1e30f;
        for (std::size_t j = 0; j < symbols.size(); ++j) {
            const float dx = symbols[j].x - mk.x, dz = symbols[j].z - mk.z;
            const float dist = dx * dx + dz * dz;
            if (dist < bestDist) { bestDist = dist; best = j; }
        }
        if (id[best] < 0) id[best] = d; // first confident digit wins the symbol
    }
    std::set<int> used;
    for (int v : id) if (v >= 0) used.insert(v);
    int nextAuto = 1;
    std::vector<std::string> out;
    out.reserve(symbols.size());
    for (std::size_t j = 0; j < symbols.size(); ++j) {
        if (id[j] < 0) {
            while (used.count(nextAuto)) ++nextAuto;
            id[j] = nextAuto;
            used.insert(nextAuto);
        }
        out.push_back(symbols[j].type + "@" + std::to_string(id[j]));
    }
    return out;
}

} // namespace game
} // namespace IKore
