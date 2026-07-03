// Performance-regression gate - issue #295.
//
// The engine has benchmark cores and tests, but nothing failed CI on a performance
// regression, only on a crash. This tool times a set of deterministic, CPU-bound
// workloads built on the header-only sim/game cores and turns them into a CI guardrail.
//
// Robustness to runner noise is the whole game here. Two ideas do the heavy lifting:
//
//   1. Ratio normalization. Every workload is reported *relative* to a fixed calibration
//      loop (a tiny LCG that will never change), not in absolute nanoseconds. A faster or
//      slower runner speeds up the workload and the calibration together, so the ratio is
//      roughly machine-independent - the committed baseline transfers across machines in a
//      way absolute times never could.
//
//   2. Auto-scaled iterations + median of repetitions + warm-up. Each workload is scaled
//      until a single repetition runs long enough to time precisely, warmed up to settle
//      caches/turbo, then measured as the median of several repetitions.
//
// With a generous tolerance on top, only a gross regression (an accidental O(n^2), a lost
// fast path, a 2x slowdown) trips the gate; ordinary noise does not.
//
// Usage:
//   perf_gate --emit                     write the baseline CSV (metric,ratio) to stdout
//   perf_gate --check <baseline.csv>     measure and fail (exit 1) if a ratio regresses
//                                        beyond the tolerance; prints a per-metric diff
//   perf_gate [--tolerance F] ...        override the fractional tolerance (default 0.75)
//
// Pure std + header-only cores:
//   g++ -std=c++17 -O2 -I src tools/perf_gate.cpp -o perf_gate

#include "core/sim/Simulation.h" // DeterministicRng
#include "core/sim/Fixed.h"      // Fixed
#include "core/sim/StateHash.h"  // StateHash
#include "game/DungeonGame.h"    // DungeonGame, GameInput, Coin

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Sink for workload checksums so the optimizer cannot delete the measured work.
volatile std::uint64_t g_sink = 0;

// --- workloads: each does `iters` operations and returns a checksum -----------------

// Calibration reference: a bare LCG. Deliberately trivial and frozen, so it is the stable
// yardstick every other metric is measured against.
std::uint64_t workCalibrate(std::uint64_t iters) {
    std::uint64_t x = 0x123456789ABCDEFULL;
    for (std::uint64_t i = 0; i < iters; ++i) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    return x;
}

// Deterministic RNG stream (sim/netcode hot path).
std::uint64_t workRng(std::uint64_t iters) {
    IKore::sim::DeterministicRng rng(0xC0FFEEULL);
    std::uint64_t acc = 0;
    for (std::uint64_t i = 0; i < iters; ++i) acc ^= rng.nextU64();
    return acc;
}

// Fixed-point multiply/add chain (deterministic math core).
std::uint64_t workFixed(std::uint64_t iters) {
    using IKore::sim::Fixed;
    Fixed a = Fixed::fromFraction(3, 7);
    const Fixed k = Fixed::fromFraction(11, 13);
    const Fixed b = Fixed::fromInt(1);
    for (std::uint64_t i = 0; i < iters; ++i) {
        a = a * k + b;
        if (a > Fixed::fromInt(1000) || a < Fixed::fromInt(-1000)) a = Fixed::fromFraction(3, 7);
    }
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(a.raw));
}

// FNV-1a state digest (lockstep/desync hot path).
std::uint64_t workStateHash(std::uint64_t iters) {
    IKore::sim::StateHash h;
    for (std::uint64_t i = 0; i < iters; ++i) h.add(i * 0x9E3779B97F4A7C15ULL);
    return h.digest();
}

// Build a game that stays in Playing forever (no exit, no enemies), so each update() does
// a constant amount of work (movement + wall slide + coin scan) regardless of iteration.
IKore::game::DungeonGame makeSimGame() {
    using namespace IKore;
    game::DungeonGame g;
    g.hasExit = false; // never wins
    g.playerPosition = ecs::Vec3{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 64; ++i) {
        const float x = static_cast<float>((i % 8) * 5 + 100); // far from the player's path
        const float z = static_cast<float>((i / 8) * 5 + 100);
        g.coins.push_back(game::Coin{ecs::Vec3{x, 0.0f, z}, false});
    }
    g.totalCoins = static_cast<int>(g.coins.size());
    return g;
}

// One deterministic dungeon step per op.
std::uint64_t workSim(std::uint64_t iters) {
    using namespace IKore;
    game::DungeonGame g = makeSimGame();
    const game::GameInput in{1.0f, 0.37f};
    std::uint64_t acc = 0;
    for (std::uint64_t i = 0; i < iters; ++i) {
        g.update(in, 1.0f / 60.0f);
        std::uint32_t bits;
        const float px = g.playerPosition.x;
        std::memcpy(&bits, &px, sizeof(bits));
        acc ^= bits + i;
    }
    return acc;
}

struct Workload {
    std::string name;
    std::uint64_t (*fn)(std::uint64_t);
};

// --- timing ------------------------------------------------------------------------

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n == 0) return 0.0;
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double timeOnce(std::uint64_t (*fn)(std::uint64_t), std::uint64_t iters) {
    const auto t0 = Clock::now();
    const std::uint64_t r = fn(iters);
    const auto t1 = Clock::now();
    g_sink ^= r;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

// Auto-scale iters until one rep takes >= targetNs, then return median ns/op over `reps`
// measured repetitions after `warmup` warm-up reps.
double measureNsPerOp(std::uint64_t (*fn)(std::uint64_t), double targetNs, int reps, int warmup) {
    std::uint64_t iters = 1024;
    while (timeOnce(fn, iters) < targetNs && iters < (std::uint64_t(1) << 34)) iters *= 2;
    for (int w = 0; w < warmup; ++w) timeOnce(fn, iters);
    std::vector<double> perOp;
    perOp.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        const double ns = timeOnce(fn, iters);
        perOp.push_back(ns / static_cast<double>(iters));
    }
    return medianOf(perOp);
}

// --- baseline I/O ------------------------------------------------------------------

std::map<std::string, double> measureRatios() {
    const std::vector<Workload> workloads = {
        {"rng", workRng},
        {"fixed", workFixed},
        {"state_hash", workStateHash},
        {"sim", workSim},
    };
    const double targetNs = 20.0 * 1e6; // ~20 ms per rep
    const int reps = 7, warmup = 2;

    const double calib = measureNsPerOp(workCalibrate, targetNs, reps, warmup);
    std::map<std::string, double> ratios;
    for (const Workload& w : workloads) {
        const double nsPerOp = measureNsPerOp(w.fn, targetNs, reps, warmup);
        ratios[w.name] = calib > 0.0 ? nsPerOp / calib : 0.0;
    }
    return ratios;
}

void emitCsv(const std::map<std::string, double>& ratios) {
    std::printf("metric,ratio\n");
    for (const auto& kv : ratios) std::printf("%s,%.6f\n", kv.first.c_str(), kv.second);
}

std::map<std::string, double> readBaseline(const std::string& path, bool& ok) {
    std::map<std::string, double> out;
    std::ifstream in(path);
    if (!in.good()) { ok = false; return out; }
    ok = true;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        const std::string key = line.substr(0, comma);
        const std::string val = line.substr(comma + 1);
        if (key == "metric") continue; // header
        try {
            out[key] = std::stod(val);
        } catch (...) {
            // ignore malformed rows
        }
    }
    return out;
}

int runCheck(const std::string& baselinePath, double tolerance) {
    bool ok = false;
    const std::map<std::string, double> baseline = readBaseline(baselinePath, ok);
    if (!ok || baseline.empty()) {
        std::fprintf(stderr, "perf_gate: cannot read baseline '%s'\n", baselinePath.c_str());
        return 2;
    }
    const std::map<std::string, double> current = measureRatios();

    std::printf("%-14s %10s %10s %9s  %s\n", "metric", "baseline", "current", "delta", "status");
    int regressions = 0;
    for (const auto& kv : baseline) {
        const std::string& name = kv.first;
        const double base = kv.second;
        auto it = current.find(name);
        if (it == current.end()) {
            std::printf("%-14s %10.4f %10s %9s  %s\n", name.c_str(), base, "-", "-", "MISSING");
            continue;
        }
        const double cur = it->second;
        const double deltaPct = base > 0.0 ? (cur - base) / base * 100.0 : 0.0;
        const bool regressed = cur > base * (1.0 + tolerance);
        std::printf("%-14s %10.4f %10.4f %8.1f%%  %s\n", name.c_str(), base, cur, deltaPct,
                    regressed ? "REGRESSED" : "ok");
        if (regressed) ++regressions;
    }
    std::printf("\ntolerance: +%.0f%% over baseline ratio\n", tolerance * 100.0);
    if (regressions > 0) {
        std::fprintf(stderr, "perf_gate: %d metric(s) regressed beyond tolerance\n", regressions);
        return 1;
    }
    std::printf("perf_gate: all metrics within tolerance\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string mode = "emit";
    std::string baselinePath;
    double tolerance = 0.75;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--emit") {
            mode = "emit";
        } else if (a == "--check") {
            mode = "check";
            if (i + 1 < argc) baselinePath = argv[++i];
        } else if (a == "--tolerance") {
            if (i + 1 < argc) tolerance = std::stod(argv[++i]);
        } else {
            std::fprintf(stderr, "perf_gate: unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }

    if (mode == "check") {
        if (baselinePath.empty()) {
            std::fprintf(stderr, "perf_gate: --check requires a baseline path\n");
            return 2;
        }
        const int rc = runCheck(baselinePath, tolerance);
        // Touch the sink so the workloads are never optimized away.
        std::fprintf(stderr, "(checksum %llu)\n", static_cast<unsigned long long>(g_sink));
        return rc;
    }

    // emit
    const std::map<std::string, double> ratios = measureRatios();
    emitCsv(ratios);
    std::fprintf(stderr, "(checksum %llu)\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
