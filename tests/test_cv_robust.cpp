// Test for the Phase 2 CV robustness layer - Milestone 16, issue #239.
//
// Verifies the acceptance criteria: uneven lighting/shadows, off-angle photos,
// colored paper, and freehand wobble still yield clean, closed wall polygons, while
// clean (Phase 1) input is unchanged or better. Covers each robustness primitive
// (illumination flattening, skew estimate + deskew, color-distance paper detection,
// collinear merge) plus a small regression corpus of synthesized "photos" run end
// to end (rectify -> vectorize -> topology).
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_cv_robust.cpp -o test_cv_robust

#include "cv/Image.h"
#include "cv/Rectify.h"
#include "cv/Vectorize.h"
#include "cv/Topology.h"
#include "cv/Robust.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace IKore::cv;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static bool nearf(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
static bool isMultipleOf(float v, float grid) {
    return std::fabs(v / grid - std::round(v / grid)) < 1e-3f;
}

// ---------------------------------------------------------------------------
// Shared synthetic-scene builders
// ---------------------------------------------------------------------------

// A closed rectangular room outline drawn on a colored sheet (3-channel).
static Image makeRoom(int W, int H, int x0, int y0, int x1, int y1,
                      int pr, int pg, int pb) {
    Image img(W, H, 3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            img.set(x, y, 0, static_cast<std::uint8_t>(pr));
            img.set(x, y, 1, static_cast<std::uint8_t>(pg));
            img.set(x, y, 2, static_cast<std::uint8_t>(pb));
        }
    }
    auto ink = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < W && y < H) {
            img.set(x, y, 0, 25);
            img.set(x, y, 1, 22);
            img.set(x, y, 2, 20);
        }
    };
    for (int x = x0; x <= x1; ++x) {
        for (int t = -1; t <= 1; ++t) { ink(x, y0 + t); ink(x, y1 + t); }
    }
    for (int y = y0; y <= y1; ++y) {
        for (int t = -1; t <= 1; ++t) { ink(x0 + t, y); ink(x1 + t, y); }
    }
    return img;
}

// A HARD cast-shadow band: a vertical stripe darkened by a step (sharp edges), the
// kind that survives illumination flattening as false vertical wall lines (issue #274).
static void applyShadowBand(Image& img, int xa, int xb, float factor) {
    for (int y = 0; y < img.height; ++y) {
        for (int x = xa; x <= xb && x < img.width; ++x) {
            for (int c = 0; c < img.channels; ++c) img.set(x, y, c, clampByte(img.at(x, y, c) * factor));
        }
    }
}

// A table with a strong horizontal lighting gradient (bright left -> dark right),
// with a flat kraft sheet + room outline composited in the center. The gradient
// destabilizes a single border-mean background estimate (issue #274).
static Image makeGradientTablePhoto(int W, int H) {
    Image photo(W, H, 3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float g = 240.0f - (x / static_cast<float>(W - 1)) * 90.0f; // 240..150
            photo.set(x, y, 0, clampByte(g));
            photo.set(x, y, 1, clampByte(g));
            photo.set(x, y, 2, clampByte(g + 2.0f));
        }
    }
    // Flat kraft sheet in the center (unaffected by the table's lighting).
    const int px0 = W / 4, py0 = H / 4, px1 = W - W / 4, py1 = H - H / 4;
    for (int y = py0; y < py1; ++y) {
        for (int x = px0; x < px1; ++x) { photo.set(x, y, 0, 185); photo.set(x, y, 1, 150); photo.set(x, y, 2, 110); }
    }
    // Room outline inside the sheet (dark ink, 3px).
    auto ink = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < W && y < H) { photo.set(x, y, 0, 30); photo.set(x, y, 1, 26); photo.set(x, y, 2, 22); }
    };
    const int rx0 = px0 + 8, ry0 = py0 + 8, rx1 = px1 - 8, ry1 = py1 - 8;
    for (int x = rx0; x <= rx1; ++x) for (int t = -1; t <= 1; ++t) { ink(x, ry0 + t); ink(x, ry1 + t); }
    for (int y = ry0; y <= ry1; ++y) for (int t = -1; t <= 1; ++t) { ink(rx0 + t, y); ink(rx1 + t, y); }
    return photo;
}

// Smooth, edge-free uneven lighting (linear gradient * soft radial vignette).
static void applySmoothLighting(Image& img) {
    const int W = img.width, H = img.height;
    const float cx = W * 0.35f, cy = H * 0.30f;
    const float R = std::sqrt(static_cast<float>(W * W + H * H));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float grad = 0.72f + (x / static_cast<float>(W - 1)) * 0.28f;
            const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / R;
            const float f = grad * (1.0f - 0.45f * d);
            for (int c = 0; c < 3; ++c) img.set(x, y, c, clampByte(img.at(x, y, c) * f));
        }
    }
}

static const Polyline* longestPolyline(const std::vector<Polyline>& walls) {
    const Polyline* best = nullptr;
    for (const Polyline& p : walls) {
        if (best == nullptr || p.size() > best->size()) best = &p;
    }
    return best;
}

static bool isAxisAlignedOnGrid(const Polyline& p, float grid) {
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (!isMultipleOf(p[i].x, grid) || !isMultipleOf(p[i].y, grid)) return false;
        if (i > 0) {
            const bool axis = nearf(p[i].x, p[i - 1].x, 1e-3f) || nearf(p[i].y, p[i - 1].y, 1e-3f);
            if (!axis) return false;
        }
    }
    return true;
}

static bool isClosed(const Polyline& p) {
    return p.size() >= 4 && nearf(p.front().x, p.back().x, 1e-3f) && nearf(p.front().y, p.back().y, 1e-3f);
}

// ---------------------------------------------------------------------------
// Primitive unit tests
// ---------------------------------------------------------------------------

static void testFlattenCancelsShadow() {
    // Brightness gradient + a deep shadow corner, with a dark ink line crossing both.
    Image img(60, 60, 1);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) {
            float base = 120.0f + (x / 59.0f) * 100.0f; // 120..220
            if (x > 35 && y > 35) base *= 0.45f;         // shadow
            if (y >= 29 && y <= 31) base *= 0.25f;       // ink line
            img.set(x, y, 0, clampByte(base));
        }
    }
    const Image f = flattenIllumination(img, 12);
    // Paper reads near-white in both the lit and shadowed regions...
    CHECK(f.at(5, 5, 0) > 200);
    CHECK(f.at(50, 50, 0) > 200);
    // ...while the ink line reads dark in both, so one threshold separates them.
    CHECK(f.at(10, 30, 0) < 150);
    CHECK(f.at(50, 30, 0) < 150);
    CHECK(f.at(5, 5, 0) - f.at(10, 30, 0) > 80);   // strong lit contrast
    CHECK(f.at(50, 50, 0) - f.at(50, 30, 0) > 80); // strong shadowed contrast
}

static void testSkewEstimateAndDeskew() {
    // Grayscale image: dark near-axis lines rotated by +6 degrees on white paper.
    Image img(90, 90, 1);
    for (int y = 0; y < 90; ++y) {
        for (int x = 0; x < 90; ++x) img.set(x, y, 0, 245);
    }
    const float slope = std::tan(6.0f * 3.14159265f / 180.0f);
    for (int y0 : {25, 60}) {
        for (int x = 10; x <= 80; ++x) {
            const int y = y0 + static_cast<int>(std::lround(slope * (x - 45)));
            for (int t = -1; t <= 1; ++t) img.set(x, y + t, 0, 20);
        }
    }
    for (int x0 : {25, 60}) {
        for (int y = 10; y <= 80; ++y) {
            const int x = x0 - static_cast<int>(std::lround(slope * (y - 45)));
            for (int t = -1; t <= 1; ++t) img.set(x + t, y, 0, 20);
        }
    }
    Mask m = binarizeAdaptive(img, 6, 10.0f);
    denoise(m, 8);
    const float est = estimateSkewAngle(m, 12.0f, 0.5f);
    CHECK(nearf(est, -6.0f, 2.0f)); // correction opposes the +6 skew

    // Deskewing by the estimate leaves almost no residual skew.
    const Image fixed = deskewImage(img, est);
    Mask m2 = binarizeAdaptive(fixed, 6, 10.0f);
    denoise(m2, 8);
    const float residual = estimateSkewAngle(m2, 12.0f, 0.5f);
    CHECK(std::fabs(residual) < std::fabs(est));
    CHECK(std::fabs(residual) < 2.0f);
}

static void testRobustPaperBeatsLuminance() {
    // Dark-blue paper on a LIGHT gray table: the paper is not the brightest region,
    // so a luminance detector fails but color distance succeeds.
    Image img(120, 120, 3);
    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 120; ++x) { img.set(x, y, 0, 205); img.set(x, y, 1, 205); img.set(x, y, 2, 210); }
    }
    for (int y = 20; y < 100; ++y) {
        for (int x = 25; x < 95; ++x) { img.set(x, y, 0, 40); img.set(x, y, 1, 55); img.set(x, y, 2, 150); }
    }
    const Quad robust = detectPaperQuadRobust(img, 60.0f);
    CHECK(nearf(robust.tl.x, 25, 4) && nearf(robust.tl.y, 20, 4));
    CHECK(nearf(robust.br.x, 94, 4) && nearf(robust.br.y, 99, 4));

    // The Phase 1 luminance detector picks the bright table (near the full frame).
    const Quad lum = detectPaperQuad(img);
    const bool lumIsFullFrame = nearf(lum.tl.x, 0, 3) && nearf(lum.tl.y, 0, 3) &&
                                nearf(lum.br.x, 119, 3) && nearf(lum.br.y, 119, 3);
    CHECK(lumIsFullFrame);
}

static void testForegroundAndLargestComponent() {
    Image img(60, 60, 3);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) { img.set(x, y, 0, 230); img.set(x, y, 1, 230); img.set(x, y, 2, 232); }
    }
    // A big colored sheet and a small bright speck of clutter in a corner.
    for (int y = 15; y < 50; ++y) {
        for (int x = 15; x < 45; ++x) { img.set(x, y, 0, 60); img.set(x, y, 1, 90); img.set(x, y, 2, 40); }
    }
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 5; ++x) { img.set(x, y, 0, 20); img.set(x, y, 1, 20); img.set(x, y, 2, 20); }
    }
    Mask fg = foregroundMask(img, 60.0f);
    CHECK(fg.get(30, 30) == 1); // sheet is foreground
    keepLargestComponent(fg);
    CHECK(fg.get(30, 30) == 1); // sheet kept
    CHECK(fg.get(3, 3) == 0);   // clutter speck removed
}

static void testMergeCollinear() {
    // A wobbly straight run collapses to its endpoints; a real corner is preserved.
    const Polyline wobble = {{0, 0}, {10, 0}, {20, 1}, {30, 0}, {40, 0}};
    const Polyline merged = mergeCollinear(wobble, 18.0f);
    CHECK(merged.size() == 2);
    CHECK(nearf(merged.front().x, 0, 1e-3f) && nearf(merged.back().x, 40, 1e-3f));

    const Polyline corner = {{0, 0}, {10, 0}, {10, 10}};
    CHECK(mergeCollinear(corner, 18.0f).size() == 3); // the 90-degree corner stays
}

// ---------------------------------------------------------------------------
// Regression corpus (end-to-end)
// ---------------------------------------------------------------------------

static void testCorpusCleanRoomUnchanged() {
    // Clean, axis-aligned room on white paper: Phase 2 must match Phase 1 (one room).
    const Image room = makeRoom(120, 120, 30, 30, 90, 85, 250, 250, 248);
    RobustOptions opt;
    opt.vec.gridSize = 8.0f;
    opt.vec.minComponentArea = 8;
    const std::vector<Polyline> w2 = vectorizeWallsRobust(room, opt);
    const Topology t2 = buildTopology(w2, 8.0f, 2);

    VectorizeOptions v1;
    v1.gridSize = 8.0f;
    v1.minComponentArea = 8;
    const std::vector<Polyline> w1 = vectorizeWalls(room, v1);
    const Topology t1 = buildTopology(w1, 8.0f, 2);

    CHECK(t2.interiorRoomCount() == 1);
    CHECK(t2.interiorRoomCount() >= t1.interiorRoomCount()); // unchanged or better
    const Polyline* best = longestPolyline(w2);
    CHECK(best != nullptr && isClosed(*best));
    if (best) CHECK(isAxisAlignedOnGrid(*best, 8.0f));
}

static void testCorpusUnevenLitTintedRoom() {
    // A rectified room under smooth uneven lighting with a warm color tint.
    Image room = makeRoom(130, 130, 30, 30, 95, 90, 250, 244, 226);
    applySmoothLighting(room);
    RobustOptions opt;
    opt.vec.gridSize = 8.0f;
    opt.vec.minComponentArea = 8;
    const std::vector<Polyline> walls = vectorizeWallsRobust(room, opt);
    const Topology t = buildTopology(walls, 8.0f, 2);
    CHECK(t.interiorRoomCount() >= 1);
    CHECK(t.ambiguousGaps == 0);
    const Polyline* best = longestPolyline(walls);
    CHECK(best != nullptr && isClosed(*best));
    if (best) CHECK(isAxisAlignedOnGrid(*best, 8.0f));
}

static void testCorpusOffAngleColoredPaperPhoto() {
    // The flagship case: an off-angle photo of a colored (kraft) sheet on a bright
    // table, unevenly lit. Phase 1's luminance detector picks the table and breaks;
    // the robust pipeline recovers a single clean, closed room.
    const Image pat = makeRoom(100, 100, 20, 20, 80, 75, 180, 150, 110);
    Image photo(200, 200, 3);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) { photo.set(x, y, 0, 245); photo.set(x, y, 1, 245); photo.set(x, y, 2, 248); }
    }
    const Point quad[4] = {{45, 32}, {170, 58}, {152, 178}, {28, 150}};
    const Point patCorners[4] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const Mat3 photoToPat = computeHomography(quad, patCorners);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            const Point p = applyHomography(photoToPat, static_cast<float>(x), static_cast<float>(y));
            if (p.x >= 0 && p.x < 100 && p.y >= 0 && p.y < 100) {
                const float f = 0.65f + (p.x / 99.0f) * 0.35f; // uneven light on the paper
                for (int c = 0; c < 3; ++c) photo.set(x, y, c, clampByte(pat.sample(p.x, p.y, c) * f));
            }
        }
    }

    // Robust detection recovers the paper corners; the luminance detector does not.
    const Quad qr = detectPaperQuadRobust(photo, 60.0f);
    CHECK(nearf(qr.tl.x, 45, 12) && nearf(qr.tl.y, 32, 12));
    CHECK(nearf(qr.br.x, 152, 12) && nearf(qr.br.y, 178, 12));

    // Phase 2 end to end.
    const Image rect2 = rectifyRobust(photo, 100, 60.0f);
    RobustOptions opt;
    opt.vec.gridSize = 8.0f;
    opt.vec.minComponentArea = 8;
    const std::vector<Polyline> w2 = vectorizeWallsRobust(rect2, opt);
    const Topology t2 = buildTopology(w2, 8.0f, 2);
    CHECK(t2.interiorRoomCount() == 1);
    CHECK(t2.ambiguousGaps == 0);
    const Polyline* best = longestPolyline(w2);
    CHECK(best != nullptr && isClosed(*best));
    if (best) CHECK(isAxisAlignedOnGrid(*best, 8.0f));

    // Phase 1 on the same photo does strictly worse (it cannot find the room cleanly).
    const Image rect1 = rectify(photo, 100);
    VectorizeOptions v1;
    v1.gridSize = 8.0f;
    v1.minComponentArea = 8;
    const std::vector<Polyline> w1 = vectorizeWalls(rect1, v1);
    const Topology t1 = buildTopology(w1, 8.0f, 2);
    const bool phase1Clean = t1.interiorRoomCount() == 1 && t1.ambiguousGaps == 0;
    CHECK(!phase1Clean); // Phase 2 is the improvement
}

// New Phase 2b corpus case: a hard cast-shadow band across the paper (issue #274).
// The shadow edges must not become false walls; the room comes out single and clean.
static void testCorpusCastShadowBand() {
    Image room = makeRoom(130, 130, 30, 30, 95, 90, 250, 250, 248);
    applyShadowBand(room, 55, 75, 0.4f); // vertical band, hard edges at x=55 and x=75

    RobustOptions opt;
    opt.vec.gridSize = 8.0f;
    opt.vec.minComponentArea = 8;

    const std::vector<Polyline> walls = vectorizeWallsRobust(room, opt);
    const Topology t = buildTopology(walls, 8.0f, 2);
    CHECK(t.interiorRoomCount() == 1);   // one clean room, not split by the shadow edges
    CHECK(t.ambiguousGaps == 0);
    const Polyline* best = longestPolyline(walls);
    CHECK(best != nullptr && isClosed(*best));
    if (best) CHECK(isAxisAlignedOnGrid(*best, 8.0f));

    // Without suppression the shadow edges leak in, so the result is strictly worse
    // (extra rooms, ambiguous gaps, or a non-clean single room) - the fix is real.
    RobustOptions off = opt;
    off.suppressShadowEdges = false;
    const std::vector<Polyline> wallsOff = vectorizeWallsRobust(room, off);
    const Topology tOff = buildTopology(wallsOff, 8.0f, 2);
    const bool cleanOff = tOff.interiorRoomCount() == 1 && tOff.ambiguousGaps == 0;
    CHECK(!cleanOff);
}

// New Phase 2b corpus case: a strong lighting gradient across the table (issue #274).
// The per-corner background estimate isolates the sheet where a single mean cannot.
static void testCorpusGradientLitTable() {
    const Image photo = makeGradientTablePhoto(160, 160);

    // The single border mean is stranded mid-gradient, so a bright table pixel reads
    // as far from it (a naive detector would mark the table as foreground)...
    const std::vector<float> singleMean = estimateBackgroundColor(photo, 2);
    float dTable = 0.0f;
    for (int c = 0; c < 3; ++c) dTable += std::fabs(static_cast<float>(photo.at(6, 80, c)) - singleMean[static_cast<std::size_t>(c)]);
    CHECK(dTable > 60.0f); // single mean misclassifies the lit table corner

    // ...but the per-corner field tracks the gradient, so the table is background and
    // only the sheet is foreground.
    const Mask fg = foregroundMask(photo, 60.0f);
    CHECK(fg.get(6, 80) == 0);   // bright-left table: background
    CHECK(fg.get(154, 80) == 0); // dark-right table: background
    CHECK(fg.get(80, 80) == 1);  // the sheet: foreground

    // The paper quad is recovered tightly around the sheet ([40,40]..[119,119]).
    const Quad q = detectPaperQuadRobust(photo, 60.0f);
    CHECK(nearf(q.tl.x, 40, 6) && nearf(q.tl.y, 40, 6));
    CHECK(nearf(q.br.x, 119, 6) && nearf(q.br.y, 119, 6));

    // End to end: rectify -> vectorize -> topology yields one clean, closed room.
    const Image rect = rectifyRobust(photo, 100, 60.0f);
    RobustOptions opt;
    opt.vec.gridSize = 8.0f;
    opt.vec.minComponentArea = 8;
    const std::vector<Polyline> walls = vectorizeWallsRobust(rect, opt);
    const Topology t = buildTopology(walls, 8.0f, 2);
    CHECK(t.interiorRoomCount() == 1);
    CHECK(t.ambiguousGaps == 0);
    const Polyline* best = longestPolyline(walls);
    CHECK(best != nullptr && isClosed(*best));
}

int main() {
    testFlattenCancelsShadow();
    testSkewEstimateAndDeskew();
    testRobustPaperBeatsLuminance();
    testForegroundAndLargestComponent();
    testMergeCollinear();
    testCorpusCleanRoomUnchanged();
    testCorpusUnevenLitTintedRoom();
    testCorpusOffAngleColoredPaperPhoto();
    testCorpusCastShadowBand();
    testCorpusGradientLitTable();

    if (g_failures == 0) {
        std::printf("All CV robustness tests passed.\n");
        return 0;
    }
    std::printf("%d CV robustness test(s) failed.\n", g_failures);
    return 1;
}
