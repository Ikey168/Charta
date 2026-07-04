// Labeled glyph corpus + recognition-accuracy gate - issue #364.
//
// Builds a deterministic, procedurally rendered corpus of labeled freehand-style glyphs
// (with stroke speckle so recognition is non-trivial), splits it stratified into train/test,
// trains the glyph classifier, and asserts an accuracy floor - the CV accuracy gate that
// fails CI when normalization or training regresses. Also checks corpus determinism, the
// split's disjointness and coverage, and generalization to an unseen-seed held-out set.
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_glyph_corpus.cpp -o test_glyph_corpus

#include "ml/GlyphCorpus.h"

#include <cstdio>
#include <set>
#include <vector>

using namespace IKore;
using namespace IKore::ml;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

// The recognition-accuracy gate: floors chosen with margin below the observed accuracy
// (overall ~0.98, worst class ~0.88) so intentional improvements never trip them, while a
// real regression (a broken normalize collapses accuracy toward chance, ~0.17) fails loudly.
static constexpr double kOverallFloor = 0.90;
static constexpr double kPerClassFloor = 0.75;

int main() {
    // 1. The corpus is deterministic: same params -> byte-identical samples.
    {
        const GlyphDataset a = buildCorpus();
        const GlyphDataset b = buildCorpus();
        CHECK(a.samples.size() == b.samples.size());
        bool identical = a.samples.size() == b.samples.size();
        for (std::size_t i = 0; identical && i < a.samples.size(); ++i)
            identical = a.samples[i].label == b.samples[i].label &&
                        a.samples[i].features == b.samples[i].features;
        CHECK(identical);
    }

    // 2. Corpus shape: one label per vocab class, perClass samples each, feature dim = size^2.
    {
        CorpusParams p;
        p.perClass = 30;
        p.featureSize = 12;
        const GlyphDataset ds = buildCorpus(p);
        const std::vector<GlyphClassDef> vocab = glyphVocabulary();
        CHECK(ds.numClasses() == static_cast<int>(vocab.size()));
        CHECK(ds.samples.size() == static_cast<std::size_t>(p.perClass) * vocab.size());
        CHECK(ds.labels[0] == "coin" && ds.labels[3] == "enemy" && ds.labels[5] == "switch");
        CHECK(ds.samples.front().features.size() ==
              static_cast<std::size_t>(p.featureSize) * p.featureSize);
        std::vector<int> perClass(static_cast<std::size_t>(ds.numClasses()), 0);
        for (const GlyphSample& s : ds.samples) ++perClass[static_cast<std::size_t>(s.label)];
        for (int c : perClass) CHECK(c == p.perClass);
    }

    // 3. Stratified split: disjoint train/test, both cover every class, ~trainFrac ratio.
    {
        const GlyphDataset ds = buildCorpus();
        const DatasetSplit sp = splitCorpus(ds, 0.7f, 1);
        CHECK(sp.train.samples.size() + sp.test.samples.size() == ds.samples.size());
        CHECK(!sp.train.samples.empty() && !sp.test.samples.empty());
        // Every class present on both sides.
        std::set<int> trainClasses, testClasses;
        for (const GlyphSample& s : sp.train.samples) trainClasses.insert(s.label);
        for (const GlyphSample& s : sp.test.samples) testClasses.insert(s.label);
        CHECK(static_cast<int>(trainClasses.size()) == ds.numClasses());
        CHECK(static_cast<int>(testClasses.size()) == ds.numClasses());
        // Train side is the larger side of a 70/30 split.
        CHECK(sp.train.samples.size() > sp.test.samples.size());
    }

    // 4. Accuracy gate: train on the split, evaluate on held-out test split.
    {
        const GlyphDataset ds = buildCorpus();
        const DatasetSplit sp = splitCorpus(ds, 0.7f, 1);
        const GlyphClassifier clf = trainClassifier(sp.train, 400, 0.5f, 1);
        const AccuracyReport r = evaluate(clf, sp.test);
        std::printf("[info] split accuracy overall %.3f (%d/%d), min-class %.3f\n", r.overall,
                    r.correct, r.total, r.minClassAccuracy());
        CHECK(r.overall >= kOverallFloor);
        CHECK(r.minClassAccuracy() >= kPerClassFloor);
        // The confusion matrix is diagonal-dominant: each true class is most often itself.
        for (int i = 0; i < static_cast<int>(r.confusion.size()); ++i) {
            int best = 0;
            for (int j = 0; j < static_cast<int>(r.confusion.size()); ++j)
                if (r.confusion[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] >
                    r.confusion[static_cast<std::size_t>(i)][static_cast<std::size_t>(best)])
                    best = j;
            CHECK(best == i);
        }
    }

    // 5. Generalization gate: train on one corpus, evaluate on a fresh unseen-seed corpus.
    {
        const GlyphClassifier clf = trainClassifier(buildCorpus(), 400, 0.5f, 1);
        CorpusParams tp;
        tp.seed = 500000; // a disjoint RNG stream -> unseen jitter/noise
        const AccuracyReport r = evaluate(clf, buildCorpus(tp));
        std::printf("[info] held-out(seed) accuracy overall %.3f, min-class %.3f\n", r.overall,
                    r.minClassAccuracy());
        CHECK(r.overall >= kOverallFloor);
        CHECK(r.minClassAccuracy() >= kPerClassFloor);
    }

    // 6. A clean canonical glyph is recognized as its labeled symbol.
    {
        const GlyphClassifier clf = trainClassifier(buildCorpus(), 400, 0.5f, 1);
        const cv::Mask circle = renderShape(GlyphShape::Circle, 48, 24, 24, 13, 1, 0.0f);
        const GlyphClassifier::Result r = clf.classifyGlyph(circle, 12, 0.4f);
        CHECK(r.label == "coin");
        const cv::Mask tri = renderShape(GlyphShape::Triangle, 48, 24, 24, 13, 1, 0.0f);
        CHECK(clf.classifyGlyph(tri, 12, 0.4f).label == "player");
    }

    if (g_failures == 0) {
        std::printf("test_glyph_corpus: all checks passed\n");
        return 0;
    }
    std::printf("test_glyph_corpus: %d check(s) failed\n", g_failures);
    return 1;
}
