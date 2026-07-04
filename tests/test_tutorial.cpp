// Tutorial and onboarding levels - issue #349.
//
// The onboarding sequence introduces one mechanic per level (move, collect, key/door,
// switch/wall, hazard), in order. Each level loads, is Solver-verified solvable, and is
// actually playable end to end (driving the intended route wins, exercising its mechanic).
// The set wires up as the campaign's first world (#347): clearing them all unlocks the
// next world. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_tutorial.cpp -o test_tutorial

#include "game/Campaign.h"
#include "game/DungeonGame.h"
#include "game/Solver.h"
#include "game/Tutorial.h"

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

static int countSym(const LevelSpec& s, const std::string& type) {
    int n = 0;
    for (const Symbol& sym : s.symbols) if (sym.type == type) ++n;
    return n;
}

static bool drive(DungeonGame g, const std::vector<ecs::Vec3>& wps) {
    const float dt = 1.0f / 60.0f;
    for (const ecs::Vec3& wp : wps) {
        for (int i = 0; i < 3000 && g.status == GameStatus::Playing; ++i) {
            const float dx = wp.x - g.playerPosition.x, dz = wp.z - g.playerPosition.z;
            if (dx * dx + dz * dz < 0.0025f) break;
            g.update(GameInput{dx, dz}, dt);
        }
    }
    return g.won();
}

int main() {
    const std::vector<TutorialLevel> levels = tutorialLevels();
    CHECK(levels.size() == 5);

    // Every level loads, is solver-solvable, and has ordered, non-empty hints.
    for (const TutorialLevel& t : levels) {
        const SolveResult r = solve(loadGame(convert(t.spec)));
        CHECK(r.solvable);
        CHECK(!t.name.empty());
        CHECK(!t.hints.empty());
        for (const std::string& h : t.hints) CHECK(!h.empty());
    }

    // Mechanics are introduced in order.
    CHECK(countSym(levels[0].spec, "coin") == 0);          // move only
    CHECK(countSym(levels[0].spec, "exit") == 1);
    CHECK(countSym(levels[1].spec, "coin") == 2);          // collecting
    CHECK(countSym(levels[2].spec, "key@1") == 1);         // keys + doors
    CHECK(countSym(levels[2].spec, "lock@1") == 1);
    CHECK(countSym(levels[3].spec, "switch@1") == 1);      // switches + walls
    CHECK(countSym(levels[3].spec, "toggle@1") == 1);
    CHECK(countSym(levels[4].spec, "hazard") == 1);        // hazards

    // Each level is actually clearable by playing its mechanic.
    CHECK(drive(loadGame(convert(levels[0].spec)), {{10, 0, 6}}));
    CHECK(drive(loadGame(convert(levels[1].spec)), {{6, 0, 4}, {9, 0, 2}, {10, 0, 6}}));
    CHECK(drive(loadGame(convert(levels[2].spec)), {{5, 0, 0}, {16, 0, 0}}));   // key then door
    CHECK(drive(loadGame(convert(levels[3].spec)), {{5, 0, 0}, {16, 0, 0}}));   // switch then wall
    CHECK(drive(loadGame(convert(levels[4].spec)), {{8, 0, 3}, {14, 0, 0}}));   // around the hazard

    // Determinism: the same sequence every call.
    const std::vector<TutorialLevel> again = tutorialLevels();
    CHECK(again.size() == levels.size());
    for (std::size_t i = 0; i < levels.size(); ++i) {
        CHECK(again[i].name == levels[i].name);
        CHECK(again[i].spec.symbols.size() == levels[i].spec.symbols.size());
    }

    // Wire as the campaign's first world (#347): a fresh campaign starts on the tutorial,
    // and clearing all of it unlocks the next world.
    CampaignWorld next;
    next.name = "Grasslands";
    next.levels = {CampaignLevel{"g0", "G1", ""}};
    Campaign camp(std::vector<CampaignWorld>{tutorialWorld(), next});
    CHECK(camp.worldCount() == 2);
    CHECK(camp.world(0).name == "Tutorial");
    CHECK(camp.levelCount(0) == 5);
    CHECK(camp.worldUnlocked(0));
    CHECK(camp.stateOf(0, 0) == LevelState::Unlocked);
    CHECK(!camp.worldUnlocked(1));
    for (std::size_t l = 0; l < camp.levelCount(0); ++l)
        CHECK(!camp.level(0, l).levelJson.empty()); // each tutorial carries its content

    for (std::size_t l = 0; l < camp.levelCount(0); ++l)
        camp.recordClear(0, l, /*stars=*/3, /*time=*/10.0);
    CHECK(camp.worldClears(0) == 5);
    CHECK(camp.worldUnlocked(1)); // tutorial complete -> next world unlocked

    if (g_failures == 0) {
        std::printf("test_tutorial: all checks passed\n");
        return 0;
    }
    std::printf("test_tutorial: %d check(s) failed\n", g_failures);
    return 1;
}
