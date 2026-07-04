// Launch content: campaign, catalog, and weekly rotation - issue #371.
//
// Verifies every shipped level loads, passes the solver/fairness gate with a recorded par,
// exercises the mechanic vocabulary, and that the weekly rotation selects deterministically
// for a given week. Pure std + the header-only game / solver:
//   g++ -std=c++17 -I src tests/test_launch_content.cpp -o test_launch_content

#include "game/LaunchContent.h"

#include <cstdio>
#include <set>
#include <string>

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
    const std::vector<ContentLevel> manifest = launchManifest();

    // 1. The manifest is non-empty, uniquely id'd, and campaign-ordered.
    {
        CHECK(manifest.size() == 6);
        std::set<std::string> ids;
        for (std::size_t i = 0; i < manifest.size(); ++i) {
            CHECK(manifest[i].order == static_cast<int>(i) + 1); // 1-based, ascending
            CHECK(!manifest[i].id.empty() && manifest[i].build != nullptr);
            ids.insert(manifest[i].id);
        }
        CHECK(ids.size() == manifest.size()); // unique ids
    }

    // 2. Every shipped level is solvable and carries a recorded par (the fairness gate).
    {
        const std::vector<ContentCheck> checks = validateContent(manifest);
        CHECK(checks.size() == manifest.size());
        for (const ContentCheck& c : checks) {
            std::printf("[info] %-18s solvable=%d par=%d\n", c.id.c_str(), c.solvable ? 1 : 0, c.par);
            CHECK(c.solvable);
            CHECK(c.par >= 0);
        }
        CHECK(allContentFair(manifest));
    }

    // 3. The content exercises the mechanic vocabulary.
    {
        const DungeonGame chase = loadGame(content::level_the_chase());
        CHECK(!chase.enemies.empty());
        const DungeonGame keys = loadGame(content::level_lock_and_key());
        CHECK(!keys.keys.empty() && !keys.lockedDoors.empty());
        const DungeonGame gauntlet = loadGame(content::level_the_gauntlet());
        CHECK(!gauntlet.hazards.empty() && !gauntlet.switches.empty());
        const DungeonGame push = loadGame(content::level_the_push());
        CHECK(!push.blocks.empty());
        // Every level has coins and an exit.
        for (const ContentLevel& lvl : manifest) {
            const DungeonGame g = loadGame(lvl.build());
            CHECK(g.hasExit);
            CHECK(g.totalCoins >= 1);
        }
    }

    // 4. The first level is actually playable to a win, not just solver-solvable.
    {
        DungeonGame g = loadGame(content::level_first_steps()); // player 0,0 -> coin 3,0 -> exit 6,0
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 2000 && g.status == GameStatus::Playing; ++i)
            g.update(GameInput{1.0f, 0.0f}, dt);
        CHECK(g.won());
    }

    // 5. Weekly rotation is deterministic per week, varies across weeks, and is valid.
    {
        std::set<std::string> ids;
        for (const ContentLevel& l : manifest) ids.insert(l.id);

        const std::vector<std::string> w7a = weeklyRotation(manifest, 7, 3);
        const std::vector<std::string> w7b = weeklyRotation(manifest, 7, 3);
        const std::vector<std::string> w8 = weeklyRotation(manifest, 8, 3);
        CHECK(w7a.size() == 3);
        CHECK(w7a == w7b);   // deterministic for a given week
        CHECK(w7a != w8);    // rotates across weeks
        for (const std::string& id : w7a) CHECK(ids.count(id) == 1); // valid ids
    }

    if (g_failures == 0) {
        std::printf("test_launch_content: all checks passed\n");
        return 0;
    }
    std::printf("test_launch_content: %d check(s) failed\n", g_failures);
    return 1;
}
