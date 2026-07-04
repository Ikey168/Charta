#pragma once

#include "cv/Image.h"
#include "cv/Vectorize.h" // Mask

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/**
 * @file Symbols.h
 * @brief Tier-1 symbol recognition: color-class + shape-subtype (issue #169).
 *
 * Milestone 16. Classifies the drawn symbols in a (rectified) image into the
 * Tier-1 vocabulary - start, exit, coin, enemy - and maps them to game objects:
 *   - Color class is the primary discriminator (green=start/player, red=enemy,
 *     yellow=coin, blue=exit), by hue with a saturation gate.
 *   - Shape subtype (from the blob fill ratio: square / circle / triangle / cross)
 *     is recorded and folds into the confidence.
 * A low-confidence or unrecognizable mark falls back to a sensible default object
 * rather than failing.
 *
 * Pure pixel logic on the dependency-free Image / Mask (no OpenCV), unit-testable
 * headless. Header-only.
 */
namespace IKore {
namespace cv {

enum class ColorClass { Green, Red, Yellow, Blue, Unknown };
enum class ShapeClass { Circle, Square, Triangle, Cross, Unknown };

/// The game object a color class maps to (matching the LevelSpec spawn vocabulary).
inline const char* colorObject(ColorClass c) {
    switch (c) {
        case ColorClass::Green: return "player"; // start
        case ColorClass::Red: return "enemy";
        case ColorClass::Yellow: return "coin";
        case ColorClass::Blue: return "exit";
        default: return "";
    }
}

/**
 * @brief Extended color+shape vocabulary (#334/#319/#320): the square shape keeps the
 *        Tier-1 mapping (green=player, red=enemy, yellow=coin, blue=exit), and the other
 *        shapes select the richer drawable types the DungeonGame now understands. The
 *        returned strings are exactly loadGame()'s spawn vocabulary.
 *
 *   red    circle -> enemy_flee   triangle -> enemy_ranged  cross -> enemy_patrol
 *   yellow triangle -> key
 *   blue   triangle -> lockeddoor cross -> switch
 *   green  triangle -> toggle     cross -> hazard
 */
inline const char* extendedObject(ColorClass c, ShapeClass s) {
    switch (c) {
        case ColorClass::Green:
            if (s == ShapeClass::Triangle) return "toggle";
            if (s == ShapeClass::Cross) return "hazard";
            return "player";
        case ColorClass::Red:
            if (s == ShapeClass::Circle) return "enemy_flee";
            if (s == ShapeClass::Triangle) return "enemy_ranged";
            if (s == ShapeClass::Cross) return "enemy_patrol";
            return "enemy";
        case ColorClass::Yellow:
            if (s == ShapeClass::Triangle) return "key";
            return "coin";
        case ColorClass::Blue:
            if (s == ShapeClass::Triangle) return "lockeddoor";
            if (s == ShapeClass::Cross) return "switch";
            return "exit";
        default:
            return "";
    }
}

struct ColorScore {
    ColorClass cls{ColorClass::Unknown};
    float confidence{0.0f};
};

/// Classify an RGB color into a Tier-1 color class (hue nearest-reference + sat).
inline ColorScore classifyColor(int R, int G, int B) {
    const float r = static_cast<float>(R), g = static_cast<float>(G), b = static_cast<float>(B);
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float sat = mx > 0.0f ? (mx - mn) / mx : 0.0f;
    if (mx < 50.0f || sat < 0.25f) return {ColorClass::Unknown, 0.0f}; // too dark or gray

    const float d = mx - mn;
    float h = 0.0f;
    if (mx == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;

    const std::pair<float, ColorClass> refs[4] = {{0.0f, ColorClass::Red},
                                                  {60.0f, ColorClass::Yellow},
                                                  {120.0f, ColorClass::Green},
                                                  {240.0f, ColorClass::Blue}};
    float best = 1e9f;
    ColorClass bc = ColorClass::Unknown;
    for (const std::pair<float, ColorClass>& rf : refs) {
        float dd = std::fabs(h - rf.first);
        dd = std::min(dd, 360.0f - dd);
        if (dd < best) {
            best = dd;
            bc = rf.second;
        }
    }
    const float hueConf = std::max(0.0f, 1.0f - best / 45.0f);
    const float satConf = std::min(1.0f, (sat - 0.2f) / 0.5f);
    const float conf = std::max(0.0f, std::min(1.0f, hueConf * satConf));
    if (conf <= 0.0f) return {ColorClass::Unknown, 0.0f};
    return {bc, conf};
}

struct ShapeScore {
    ShapeClass cls{ShapeClass::Unknown};
    float confidence{0.0f};
};

/// Classify a blob's shape from its fill ratio (area / bounding-box area).
inline ShapeScore classifyShape(int bboxW, int bboxH, int area) {
    if (bboxW <= 0 || bboxH <= 0) return {ShapeClass::Unknown, 0.0f};
    const float fill = static_cast<float>(area) / static_cast<float>(bboxW * bboxH);
    const std::pair<float, ShapeClass> cands[4] = {{1.0f, ShapeClass::Square},
                                                   {0.785f, ShapeClass::Circle},
                                                   {0.5f, ShapeClass::Triangle},
                                                   {0.38f, ShapeClass::Cross}};
    float best = 1e9f;
    ShapeClass bc = ShapeClass::Unknown;
    for (const std::pair<float, ShapeClass>& c : cands) {
        const float dd = std::fabs(fill - c.first);
        if (dd < best) {
            best = dd;
            bc = c.second;
        }
    }
    return {bc, std::max(0.0f, 1.0f - best / 0.2f)};
}

struct SymbolResult {
    std::string object;              ///< game object (or the default on low confidence).
    ColorClass color{ColorClass::Unknown};
    ShapeClass shape{ShapeClass::Unknown};
    float confidence{0.0f};
    bool lowConfidence{false};       ///< true when it fell back to the default.
};

/// Recognize a symbol from its average color and blob shape. Falls back to
/// @p defaultObject when the color is unknown or the combined confidence is low.
inline SymbolResult recognizeSymbol(int r, int g, int b, int bboxW, int bboxH, int area,
                                    const std::string& defaultObject = "coin",
                                    float minConfidence = 0.35f) {
    const ColorScore cs = classifyColor(r, g, b);
    const ShapeScore sh = classifyShape(bboxW, bboxH, area);
    SymbolResult out;
    out.color = cs.cls;
    out.shape = sh.cls;
    if (cs.cls == ColorClass::Unknown) {
        out.object = defaultObject;
        out.confidence = cs.confidence;
        out.lowConfidence = true;
        return out;
    }
    const float conf = 0.7f * cs.confidence + 0.3f * sh.confidence;
    out.confidence = conf;
    if (conf < minConfidence) {
        out.object = defaultObject;
        out.lowConfidence = true;
        return out;
    }
    out.object = colorObject(cs.cls);
    out.lowConfidence = false;
    return out;
}

struct SymbolInstance {
    float cx{0.0f};
    float cy{0.0f};
    int minX{0}, minY{0}, maxX{0}, maxY{0};
    int area{0};
    SymbolResult result;
};

struct SymbolOptions {
    float minSaturation{0.25f};
    int minArea{20};
    std::string defaultObject{"coin"};
    float minConfidence{0.35f};
};

/// Detect and classify all colored symbol blobs in @p img (needs 3+ channels).
inline std::vector<SymbolInstance> detectSymbols(const Image& img, const SymbolOptions& opt = {}) {
    std::vector<SymbolInstance> out;
    if (img.channels < 3 || img.width <= 0 || img.height <= 0) return out;

    auto colored = [&](int x, int y) {
        const float r = img.at(x, y, 0), g = img.at(x, y, 1), b = img.at(x, y, 2);
        const float mx = std::max({r, g, b});
        const float mn = std::min({r, g, b});
        const float sat = mx > 0.0f ? (mx - mn) / mx : 0.0f;
        return mx >= 50.0f && sat >= opt.minSaturation;
    };

    Mask visited(img.width, img.height);
    const int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    const int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    std::vector<int> stack;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            if (!colored(x, y) || visited.get(x, y)) continue;
            stack.clear();
            stack.push_back(y * img.width + x);
            visited.set(x, y, 1);
            long sumR = 0, sumG = 0, sumB = 0;
            int area = 0, minX = x, minY = y, maxX = x, maxY = y;
            double sx = 0, sy = 0;
            while (!stack.empty()) {
                const int cell = stack.back();
                stack.pop_back();
                const int cxx = cell % img.width, cyy = cell / img.width;
                ++area;
                sumR += img.at(cxx, cyy, 0);
                sumG += img.at(cxx, cyy, 1);
                sumB += img.at(cxx, cyy, 2);
                sx += cxx;
                sy += cyy;
                minX = std::min(minX, cxx);
                minY = std::min(minY, cyy);
                maxX = std::max(maxX, cxx);
                maxY = std::max(maxY, cyy);
                for (int i = 0; i < 8; ++i) {
                    const int ax = cxx + dx8[i], ay = cyy + dy8[i];
                    if (ax < 0 || ay < 0 || ax >= img.width || ay >= img.height) continue;
                    if (visited.get(ax, ay) || !colored(ax, ay)) continue;
                    visited.set(ax, ay, 1);
                    stack.push_back(ay * img.width + ax);
                }
            }
            if (area < opt.minArea) continue;
            SymbolInstance s;
            s.area = area;
            s.minX = minX;
            s.minY = minY;
            s.maxX = maxX;
            s.maxY = maxY;
            s.cx = static_cast<float>(sx / area);
            s.cy = static_cast<float>(sy / area);
            s.result = recognizeSymbol(static_cast<int>(sumR / area), static_cast<int>(sumG / area),
                                       static_cast<int>(sumB / area), maxX - minX + 1, maxY - minY + 1,
                                       area, opt.defaultObject, opt.minConfidence);
            out.push_back(std::move(s));
        }
    }
    return out;
}

} // namespace cv
} // namespace IKore
