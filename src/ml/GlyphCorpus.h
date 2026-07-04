#pragma once

#include "core/sim/Simulation.h" // sim::DeterministicRng
#include "cv/Vectorize.h"        // cv::Mask
#include "ml/GlyphNet.h"         // normalizeGlyph, GlyphClassifier

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file GlyphCorpus.h
 * @brief A labeled doodle-glyph corpus and recognition-accuracy metrics (issue #364).
 *
 * The glyph classifier (#173) needs a repeatable way to measure how well it recognizes
 * hand-drawn symbols, and CI needs a way to fail when that accuracy regresses. This is the
 * corpus and the ruler: a deterministic, procedurally rendered set of labeled freehand-style
 * glyphs (position / scale / stroke-thickness / rotation jitter), a stratified train/test
 * split, and an evaluator that reports overall and per-class accuracy plus a confusion
 * matrix. A unit test trains on the corpus and asserts an accuracy floor - the CV accuracy
 * gate - so a normalization or training regression trips CI instead of shipping.
 *
 * The corpus is generated rather than stored so it stays portable and diff-free; real
 * captured samples can be appended behind the same GlyphSample interface. Pure std + the
 * header-only Mask / RNG / GlyphNet. Header-only.
 */
namespace IKore {
namespace ml {

/// Canonical glyph shapes in the corpus. Each stands in for a game symbol label.
enum class GlyphShape { Circle, Square, Triangle, Cross, Plus, Bar };

/// A corpus class: a shape and the game symbol label it represents.
struct GlyphClassDef {
    GlyphShape shape;
    std::string label;
};

/// The corpus vocabulary: distinct, separable shapes mapped to real spawn labels.
inline std::vector<GlyphClassDef> glyphVocabulary() {
    return {
        {GlyphShape::Circle, "coin"},   {GlyphShape::Square, "exit"},
        {GlyphShape::Triangle, "player"}, {GlyphShape::Cross, "enemy"},
        {GlyphShape::Plus, "key"},      {GlyphShape::Bar, "switch"},
    };
}

namespace detail {

/// Stamp a square pen of half-width @p th at (fx, fy).
inline void stampInk(cv::Mask& m, float fx, float fy, int th) {
    const int cx = static_cast<int>(std::lround(fx)), cy = static_cast<int>(std::lround(fy));
    for (int dy = -th; dy <= th; ++dy)
        for (int dx = -th; dx <= th; ++dx) m.set(cx + dx, cy + dy, 1);
}

/// Bresenham stroke with pen thickness @p th.
inline void strokeLine(cv::Mask& m, float x0, float y0, float x1, float y1, int th) {
    int ix0 = static_cast<int>(std::lround(x0)), iy0 = static_cast<int>(std::lround(y0));
    const int ix1 = static_cast<int>(std::lround(x1)), iy1 = static_cast<int>(std::lround(y1));
    const int dx = std::abs(ix1 - ix0), dy = -std::abs(iy1 - iy0);
    const int sx = ix0 < ix1 ? 1 : -1, sy = iy0 < iy1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        stampInk(m, static_cast<float>(ix0), static_cast<float>(iy0), th);
        if (ix0 == ix1 && iy0 == iy1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; ix0 += sx; }
        if (e2 <= dx) { err += dx; iy0 += sy; }
    }
}

} // namespace detail

/// Render one glyph @p shape into a @p W x @p W ink mask at center (cx, cy), radius @p r,
/// pen thickness @p th, rotated by @p rot radians.
inline cv::Mask renderShape(GlyphShape shape, int W, float cx, float cy, float r, int th, float rot) {
    cv::Mask m(W, W);
    const float cs = std::cos(rot), sn = std::sin(rot);
    auto rp = [&](float lx, float ly) {
        return std::pair<float, float>{cx + (lx * cs - ly * sn), cy + (lx * sn + ly * cs)};
    };
    auto seg = [&](std::pair<float, float> a, std::pair<float, float> b) {
        detail::strokeLine(m, a.first, a.second, b.first, b.second, th);
    };
    switch (shape) {
        case GlyphShape::Circle:
            for (int a = 0; a < 48; ++a) {
                const float ang = 2.0f * 3.14159265f * a / 48.0f;
                detail::stampInk(m, cx + r * std::cos(ang), cy + r * std::sin(ang), th);
            }
            break;
        case GlyphShape::Square: {
            const auto a = rp(-r, -r), b = rp(r, -r), c = rp(r, r), d = rp(-r, r);
            seg(a, b); seg(b, c); seg(c, d); seg(d, a);
            break;
        }
        case GlyphShape::Triangle: {
            const auto a = rp(0, -r), b = rp(-r, r), c = rp(r, r);
            seg(a, b); seg(b, c); seg(c, a);
            break;
        }
        case GlyphShape::Cross: // an X: two diagonals
            seg(rp(-r, -r), rp(r, r));
            seg(rp(r, -r), rp(-r, r));
            break;
        case GlyphShape::Plus: // a +: axis-aligned bars
            seg(rp(-r, 0), rp(r, 0));
            seg(rp(0, -r), rp(0, r));
            break;
        case GlyphShape::Bar: // a single horizontal stroke
            seg(rp(-r, 0), rp(r, 0));
            break;
    }
    return m;
}

namespace detail {

/// Sprinkle @p n stray ink specks inside the central half of the image (near the glyph, so
/// the ink bounding box is not blown far out) to emulate binarization speckle.
inline void addSaltNoise(cv::Mask& m, sim::DeterministicRng& rng, int n) {
    const int lo = m.width / 4, hi = m.width - m.width / 4;
    for (int i = 0; i < n; ++i) m.set(rng.nextInt(lo, hi), rng.nextInt(lo, hi), 1);
}

} // namespace detail

/// A varied freehand-style rendering: jittered position, size, thickness, and rotation, with
/// @p noisePixels of stroke speckle so recognition is non-trivial (robustness under noise).
inline cv::Mask renderVariedShape(GlyphShape shape, int W, sim::DeterministicRng& rng,
                                  int noisePixels = 0) {
    const float cx = W / 2.0f + rng.nextInt(-4, 4);
    const float cy = W / 2.0f + rng.nextInt(-4, 4);
    const float r = 10.0f + rng.nextInt(0, 6);
    const int th = 1 + rng.nextInt(0, 1);
    const float rot = (rng.nextFloat() - 0.5f) * 0.5f; // +/- ~0.25 rad
    cv::Mask m = renderShape(shape, W, cx, cy, r, th, rot);
    if (noisePixels > 0) detail::addSaltNoise(m, rng, noisePixels);
    return m;
}

/// A labeled corpus sample: normalized features + class index + label name.
struct GlyphSample {
    std::vector<float> features;
    int label{-1};
    std::string labelName;
};

/// A labeled dataset: samples plus the index-to-label mapping.
struct GlyphDataset {
    std::vector<GlyphSample> samples;
    std::vector<std::string> labels;

    std::vector<std::vector<float>> featureMatrix() const {
        std::vector<std::vector<float>> X;
        X.reserve(samples.size());
        for (const GlyphSample& s : samples) X.push_back(s.features);
        return X;
    }
    std::vector<int> targets() const {
        std::vector<int> y;
        y.reserve(samples.size());
        for (const GlyphSample& s : samples) y.push_back(s.label);
        return y;
    }
    int numClasses() const { return static_cast<int>(labels.size()); }
};

struct CorpusParams {
    int imageSize{48};    ///< rendered mask size in px.
    int featureSize{12};  ///< normalized density-grid side.
    int perClass{80};     ///< varied samples per class.
    int noisePixels{6};   ///< stroke-speckle count per sample (0 = clean glyphs).
    std::uint64_t seed{1};
};

/// Build a deterministic labeled corpus: @p params.perClass varied renderings of each vocab
/// class. Each class draws from its own RNG stream, so the corpus is reproducible bit for
/// bit and independent of class order.
inline GlyphDataset buildCorpus(const CorpusParams& params = {},
                                const std::vector<GlyphClassDef>& vocab = glyphVocabulary()) {
    GlyphDataset ds;
    for (const GlyphClassDef& c : vocab) ds.labels.push_back(c.label);
    for (int k = 0; k < static_cast<int>(vocab.size()); ++k) {
        sim::DeterministicRng rng(params.seed + static_cast<std::uint64_t>(k) * 10007ULL + 1ULL);
        for (int i = 0; i < params.perClass; ++i) {
            cv::Mask m = renderVariedShape(vocab[k].shape, params.imageSize, rng, params.noisePixels);
            ds.samples.push_back(
                GlyphSample{normalizeGlyph(m, params.featureSize), k, vocab[k].label});
        }
    }
    return ds;
}

/// A train/test partition of a corpus.
struct DatasetSplit {
    GlyphDataset train;
    GlyphDataset test;
};

/// Stratified split: within each class, shuffle deterministically and route the first
/// @p trainFrac of samples to train and the rest to test. Every class appears in both
/// sides (given enough samples), and the two sides are disjoint.
inline DatasetSplit splitCorpus(const GlyphDataset& ds, float trainFrac = 0.7f,
                                std::uint64_t seed = 1) {
    DatasetSplit out;
    out.train.labels = ds.labels;
    out.test.labels = ds.labels;
    const int K = ds.numClasses();
    for (int k = 0; k < K; ++k) {
        std::vector<int> idx;
        for (int i = 0; i < static_cast<int>(ds.samples.size()); ++i)
            if (ds.samples[static_cast<std::size_t>(i)].label == k) idx.push_back(i);
        sim::DeterministicRng rng(seed + static_cast<std::uint64_t>(k) * 7919ULL + 5ULL);
        for (int i = static_cast<int>(idx.size()) - 1; i > 0; --i) {
            const int j = rng.nextInt(0, i);
            std::swap(idx[static_cast<std::size_t>(i)], idx[static_cast<std::size_t>(j)]);
        }
        const int nTrain = static_cast<int>(static_cast<float>(idx.size()) * trainFrac);
        for (int i = 0; i < static_cast<int>(idx.size()); ++i) {
            const GlyphSample& s = ds.samples[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
            (i < nTrain ? out.train : out.test).samples.push_back(s);
        }
    }
    return out;
}

/// Train a fresh classifier on a labeled dataset.
inline GlyphClassifier trainClassifier(const GlyphDataset& train, int epochs = 400,
                                       float lr = 0.5f, std::uint64_t seed = 1) {
    GlyphClassifier clf;
    clf.train(train.featureMatrix(), train.targets(), train.labels, epochs, lr, seed);
    return clf;
}

/// Overall + per-class recognition accuracy and a confusion matrix.
struct AccuracyReport {
    int total{0};
    int correct{0};
    double overall{0.0};
    std::vector<int> classTotal;
    std::vector<int> classCorrect;
    std::vector<double> classAccuracy;
    std::vector<std::vector<int>> confusion; ///< confusion[trueClass][predClass].

    /// Lowest per-class accuracy (worst-recognized symbol); 1.0 if there is nothing to score.
    double minClassAccuracy() const {
        double m = 1.0;
        bool any = false;
        for (std::size_t k = 0; k < classAccuracy.size(); ++k) {
            if (classTotal[k] == 0) continue;
            any = true;
            if (classAccuracy[k] < m) m = classAccuracy[k];
        }
        return any ? m : 1.0;
    }
};

/// Evaluate @p clf on @p test, scoring by class index (the classifier's argmax class), so
/// the metric is independent of the confidence threshold.
inline AccuracyReport evaluate(const GlyphClassifier& clf, const GlyphDataset& test) {
    AccuracyReport r;
    const int K = test.numClasses();
    r.classTotal.assign(static_cast<std::size_t>(K), 0);
    r.classCorrect.assign(static_cast<std::size_t>(K), 0);
    r.classAccuracy.assign(static_cast<std::size_t>(K), 0.0);
    r.confusion.assign(static_cast<std::size_t>(K), std::vector<int>(static_cast<std::size_t>(K), 0));
    for (const GlyphSample& s : test.samples) {
        const GlyphClassifier::Result res = clf.classify(s.features, 0.0f);
        ++r.total;
        if (s.label >= 0 && s.label < K) {
            ++r.classTotal[static_cast<std::size_t>(s.label)];
            if (res.index >= 0 && res.index < K)
                ++r.confusion[static_cast<std::size_t>(s.label)][static_cast<std::size_t>(res.index)];
            if (res.index == s.label) {
                ++r.correct;
                ++r.classCorrect[static_cast<std::size_t>(s.label)];
            }
        }
    }
    r.overall = r.total > 0 ? static_cast<double>(r.correct) / r.total : 0.0;
    for (int k = 0; k < K; ++k)
        r.classAccuracy[static_cast<std::size_t>(k)] =
            r.classTotal[static_cast<std::size_t>(k)] > 0
                ? static_cast<double>(r.classCorrect[static_cast<std::size_t>(k)]) /
                      r.classTotal[static_cast<std::size_t>(k)]
                : 0.0;
    return r;
}

} // namespace ml
} // namespace IKore
