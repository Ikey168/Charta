// Extended, on-demand soak driver (#419, building on the bounded #375 harness).
//
// test_soak runs a fixed 200-iteration loop on every sanitizer job. This driver runs the same
// game/Soak.h loop for much longer and across several seeds, checks the same invariants, and
// prints a Markdown findings report (stdout, or --report <file>). The scheduled
// soak-extended.yml workflow builds it under ASan/UBSan, so a slow leak or corruption fails
// the run instead of passing silently.
//
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I src tools/soak_extended.cpp -o soak_extended
//   ./soak_extended --iterations 20000 --steps 600 --seeds 4 --report soak-report.md

#include "game/Soak.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace IKore::game;

namespace {

struct Options {
    int iterations{5000};
    int steps{480};
    int seeds{3};
    std::size_t recentCap{16};
    std::string report;
};

bool parseInt(const char* s, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (!end || *end != '\0' || v <= 0 || v > 100000000L) return false;
    out = static_cast<int>(v);
    return true;
}

int usage() {
    std::fprintf(stderr,
                 "usage: soak_extended [--iterations N] [--steps N] [--seeds N] [--cap N] "
                 "[--report FILE]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool hasValue = i + 1 < argc;
        int v = 0;
        if (!std::strcmp(a, "--iterations") && hasValue && parseInt(argv[++i], v)) opt.iterations = v;
        else if (!std::strcmp(a, "--steps") && hasValue && parseInt(argv[++i], v)) opt.steps = v;
        else if (!std::strcmp(a, "--seeds") && hasValue && parseInt(argv[++i], v)) opt.seeds = v;
        else if (!std::strcmp(a, "--cap") && hasValue && parseInt(argv[++i], v)) opt.recentCap = static_cast<std::size_t>(v);
        else if (!std::strcmp(a, "--report") && hasValue) opt.report = argv[++i];
        else return usage();
    }

    struct Row {
        std::uint64_t seed;
        SoakStats stats;
        bool deterministic;
        std::vector<std::string> failures;
        double seconds;
    };
    std::vector<Row> rows;
    int failed = 0;

    for (int s = 0; s < opt.seeds; ++s) {
        SoakConfig cfg;
        cfg.iterations = opt.iterations;
        cfg.stepsPerSession = opt.steps;
        cfg.recentCap = opt.recentCap;
        cfg.seed = 1000u + static_cast<std::uint64_t>(s);

        const auto t0 = std::chrono::steady_clock::now();
        const SoakStats st = runSoak(cfg);
        const auto t1 = std::chrono::steady_clock::now();

        // Determinism: a short re-run of the same seed must reproduce the same prefix stats.
        SoakConfig shortCfg = cfg;
        shortCfg.iterations = opt.iterations < 200 ? opt.iterations : 200;
        const SoakStats a = runSoak(shortCfg);
        const SoakStats b = runSoak(shortCfg);
        const bool det = a.gamesPlayed == b.gamesPlayed && a.editsApplied == b.editsApplied &&
                         a.sharesRoundTripped == b.sharesRoundTripped && a.recentCodes == b.recentCodes;

        Row r{cfg.seed, st, det, {}, std::chrono::duration<double>(t1 - t0).count()};
        if (st.iterations != opt.iterations) r.failures.push_back("iteration count mismatch");
        if (st.gamesPlayed != opt.iterations) r.failures.push_back("games played mismatch");
        if (st.sharesRoundTripped != opt.iterations) r.failures.push_back("share count mismatch");
        if (!st.allShareRoundTripsOk) r.failures.push_back("a share code failed to round-trip");
        if (st.recentCodes > opt.recentCap) r.failures.push_back("recent cache exceeded its cap");
        if (!det) r.failures.push_back("same seed produced different stats");
        if (!r.failures.empty()) ++failed;
        rows.push_back(r);
        std::fprintf(stderr, "[soak] seed %llu: %d iterations in %.1fs, %s\n",
                     static_cast<unsigned long long>(cfg.seed), st.iterations, r.seconds,
                     r.failures.empty() ? "ok" : "FAILED");
    }

    std::string md;
    char line[512];
    std::snprintf(line, sizeof line,
                  "## Extended soak report\n\n%d seed(s) x %d iterations x %d steps, recent cap %zu.\n\n",
                  opt.seeds, opt.iterations, opt.steps, opt.recentCap);
    md += line;
    md += "| Seed | Iterations | Edits | Shares | Recent cache | Deterministic | Seconds | Result |\n";
    md += "|---|---|---|---|---|---|---|---|\n";
    for (const Row& r : rows) {
        std::string result = r.failures.empty() ? "pass" : "FAIL: ";
        for (std::size_t i = 0; i < r.failures.size(); ++i) {
            if (i) result += "; ";
            result += r.failures[i];
        }
        std::snprintf(line, sizeof line, "| %llu | %d | %d | %d | %zu | %s | %.1f | %s |\n",
                      static_cast<unsigned long long>(r.seed), r.stats.iterations, r.stats.editsApplied,
                      r.stats.sharesRoundTripped, r.stats.recentCodes, r.deterministic ? "yes" : "no",
                      r.seconds, result.c_str());
        md += line;
    }
    md += failed ? "\n**Result: FAILED.** Reproduce locally with the command in tools/soak_extended.cpp.\n"
                 : "\n**Result: all seeds passed.** Sanitizer findings, if any, appear in the job log.\n";

    std::fputs(md.c_str(), stdout);
    if (!opt.report.empty()) {
        if (FILE* f = std::fopen(opt.report.c_str(), "w")) {
            std::fputs(md.c_str(), f);
            std::fclose(f);
        } else {
            std::fprintf(stderr, "could not write report %s\n", opt.report.c_str());
            return 1;
        }
    }
    return failed ? 1 : 0;
}
