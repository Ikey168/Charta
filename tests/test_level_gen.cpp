// Solver-as-oracle procedural level generator - issue #348.
//
// Every generated level is Solver-verified clearable; the same seed yields the same level;
// difficulty (size + coins) raises par; and an impossible par window is correctly refused.
// Pure std + the header-only game / solver:
//   g++ -std=c++17 -I src tests/test_level_gen.cpp -o test_level_gen

#include "game/LevelGen.h"

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

// A structural fingerprint of a level: wall count + each symbol's type and position.
static std::string digest(const LevelSpec& s) {
    std::string d = "W" + std::to_string(s.walls.size()) + ";";
    for (const Symbol& sym : s.symbols) {
        d += sym.type + "@" + std::to_string(static_cast<int>(sym.position.x)) + "," +
             std::to_string(static_cast<int>(sym.position.z)) + ";";
    }
    return d;
}

static int countSymbols(const LevelSpec& s, const std::string& type) {
    int n = 0;
    for (const Symbol& sym : s.symbols) if (sym.type == type) ++n;
    return n;
}

int main() {
    // 1. A default generated level is solvable, with all coins reachable and its objectives.
    {
        GenResult g = generateLevel(12345u);
        CHECK(g.ok);
        CHECK(g.solve.solvable);
        CHECK(g.solve.par > 0);
        CHECK(g.attempts >= 1);
        CHECK(countSymbols(g.spec, "player") == 1);
        CHECK(countSymbols(g.spec, "exit") == 1);
        CHECK(countSymbols(g.spec, "coin") == 3);
        CHECK(g.solve.totalCoins == 3);
        CHECK(g.solve.reachableCoins == 3);
        CHECK(g.solve.exitReachable);
    }

    // 2. Deterministic per seed; different seeds (almost always) differ.
    {
        const GenResult a1 = generateLevel(777u);
        const GenResult a2 = generateLevel(777u);
        CHECK(a1.ok && a2.ok);
        CHECK(digest(a1.spec) == digest(a2.spec));
        CHECK(a1.solve.par == a2.solve.par);

        const GenResult b = generateLevel(778u);
        CHECK(b.ok);
        CHECK(digest(a1.spec) != digest(b.spec));
    }

    // 3. Every generated level (many seeds) is Solver-verified solvable.
    {
        for (unsigned s = 1; s <= 12; ++s) {
            const GenResult g = generateLevel(s);
            CHECK(g.ok);
            CHECK(g.solve.solvable);
            CHECK(g.solve.reachableCoins == g.solve.totalCoins);
        }
    }

    // 4. Difficulty: a bigger grid with more coins yields a larger par than a small one.
    {
        GenParams easy;
        easy.gridW = 5; easy.gridH = 4; easy.coins = 1;
        GenParams hard;
        hard.gridW = 14; hard.gridH = 10; hard.coins = 6;

        const GenResult e = generateLevel(2024u, easy);
        const GenResult h = generateLevel(2024u, hard);
        CHECK(e.ok && h.ok);
        CHECK(countSymbols(e.spec, "coin") == 1);
        CHECK(countSymbols(h.spec, "coin") == 6);
        CHECK(h.solve.par > e.solve.par);
    }

    // 5. Par window: a satisfiable window is honored; an impossible one is refused.
    {
        const GenResult base = generateLevel(99u);
        CHECK(base.ok);

        GenParams wide;
        wide.minPar = 1;
        wide.maxPar = base.solve.par + 1000;
        const GenResult w = generateLevel(99u, wide);
        CHECK(w.ok);
        CHECK(w.solve.par >= 1 && w.solve.par <= base.solve.par + 1000);

        GenParams impossible;
        impossible.maxPar = 1; // no level clears in a single grid step
        impossible.maxAttempts = 50;
        const GenResult imp = generateLevel(99u, impossible);
        CHECK(!imp.ok);
    }

    if (g_failures == 0) {
        std::printf("test_level_gen: all checks passed\n");
        return 0;
    }
    std::printf("test_level_gen: %d check(s) failed\n", g_failures);
    return 1;
}
