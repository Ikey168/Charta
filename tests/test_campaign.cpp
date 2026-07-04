// Campaign structure and persistent progress - issue #347.
//
// Builds an ordered two-world campaign, checks the unlock rules (a level unlocks when the
// previous in its world is cleared; a world unlocks once its predecessor has enough clears),
// records per-level bests (max stars, min time), and round-trips progress through the
// Settings store (#58) so it survives across sessions. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_campaign.cpp -o test_campaign

#include "game/Campaign.h"

#include <cmath>
#include <cstdio>
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

// World 0 "Grasslands": 3 levels, clear 2 to advance. World 1 "Caverns": 2 levels.
static Campaign makeCampaign() {
    CampaignWorld w0;
    w0.name = "Grasslands";
    w0.requiredToAdvance = 2;
    w0.levels = {CampaignLevel{"", "L1", ""}, CampaignLevel{"", "L2", ""}, CampaignLevel{"", "L3", ""}};
    CampaignWorld w1;
    w1.name = "Caverns";
    w1.requiredToAdvance = 0; // 0 => all levels
    w1.levels = {CampaignLevel{"", "C1", ""}, CampaignLevel{"", "C2", ""}};
    return Campaign(std::vector<CampaignWorld>{w0, w1});
}

int main() {
    Campaign camp = makeCampaign();
    CHECK(camp.worldCount() == 2);
    CHECK(camp.levelCount(0) == 3);
    CHECK(camp.levelCount(1) == 2);

    // 1. Initial gating: only world 0's first level is playable.
    CHECK(camp.worldUnlocked(0));
    CHECK(!camp.worldUnlocked(1));
    CHECK(camp.stateOf(0, 0) == LevelState::Unlocked);
    CHECK(camp.stateOf(0, 1) == LevelState::Locked);
    CHECK(camp.stateOf(0, 2) == LevelState::Locked);
    CHECK(camp.stateOf(1, 0) == LevelState::Locked);

    // 2. Clearing a level unlocks the next in its world; the world stays locked until N.
    camp.recordClear(0, 0, /*stars=*/3, /*time=*/10.0);
    CHECK(camp.stateOf(0, 0) == LevelState::Completed);
    CHECK(camp.stateOf(0, 1) == LevelState::Unlocked);
    CHECK(camp.stateOf(0, 2) == LevelState::Locked);
    CHECK(!camp.worldUnlocked(1)); // only 1 clear < 2 required

    // 3. Second clear reaches the world's requiredToAdvance -> next world unlocks.
    camp.recordClear(0, 1, 2, 12.0);
    CHECK(camp.worldClears(0) == 2);
    CHECK(camp.worldUnlocked(1));
    CHECK(camp.stateOf(1, 0) == LevelState::Unlocked);
    CHECK(camp.stateOf(1, 1) == LevelState::Locked); // its prev not cleared yet
    CHECK(camp.stateOf(0, 2) == LevelState::Unlocked);

    // 4. Best-of tracking: max stars, min time; a worse run never regresses the best.
    camp.recordClear(0, 0, 1, 8.0);
    camp.recordClear(0, 0, 2, 20.0);
    const LevelProgress p00 = camp.progressOf(0, 0);
    CHECK(p00.bestStars == 3);
    CHECK(std::fabs(p00.bestTime - 8.0) < 1e-6);
    CHECK(camp.totalStars() == 5); // (0,0)=3 + (0,1)=2

    // 5. Progress persists across sessions via the Settings store round trip.
    const std::string saved = camp.serializeProgress();
    CHECK(!saved.empty());
    CHECK(camp.serializeProgress() == saved); // deterministic

    Campaign restored = makeCampaign();
    // A fresh campaign starts locked...
    CHECK(restored.stateOf(0, 1) == LevelState::Locked);
    CHECK(!restored.worldUnlocked(1));
    // ...and restoring the saved progress reproduces the unlocked/completed world map.
    restored.loadProgress(saved);
    CHECK(restored.stateOf(0, 0) == LevelState::Completed);
    CHECK(restored.stateOf(0, 1) == LevelState::Completed);
    CHECK(restored.worldUnlocked(1));
    CHECK(restored.stateOf(1, 0) == LevelState::Unlocked);
    CHECK(restored.stateOf(1, 1) == LevelState::Locked);
    const LevelProgress r00 = restored.progressOf(0, 0);
    CHECK(r00.bestStars == 3);
    CHECK(std::fabs(r00.bestTime - 8.0) < 1e-6);
    CHECK(restored.totalStars() == 5);

    // 6. The Settings object form is usable directly (save/load via Settings, #58).
    Settings s;
    camp.save(s);
    Settings s2;
    s2.load(s.serialize());
    Campaign restored2 = makeCampaign();
    restored2.load(s2);
    CHECK(restored2.progressOf(0, 1).bestStars == 2);
    CHECK(restored2.stateOf(1, 0) == LevelState::Unlocked);

    if (g_failures == 0) {
        std::printf("test_campaign: all checks passed\n");
        return 0;
    }
    std::printf("test_campaign: %d check(s) failed\n", g_failures);
    return 1;
}
