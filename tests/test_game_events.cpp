// Particles and audio for game events - issue #353 (headless core).
//
// Verifies the renderer-agnostic seam: detectEvents() turns sim state transitions into
// discrete events (coin/key pickup, door open, switch toggle, win, lose), each at its world
// position, and effectFor() maps each to a distinct particle burst + sound cue. Driving the
// deterministic sim produces the expected events; the GL burst is exercised by
// test_game_fx_gl. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_game_events.cpp -o test_game_events

#include "game/GameEvents.h"

#include <cstdio>
#include <set>
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

static EntitySpawn sp(const char* t, float x) { return EntitySpawn{t, ecs::Vec3{x, 0.0f, 0.0f}, 0.0f}; }

// Drive +x for up to n steps, collecting every event.
static std::vector<GameEvent> driveEast(DungeonGame g, int n) {
    std::vector<GameEvent> all;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < n && g.status == GameStatus::Playing; ++i) {
        const std::vector<GameEvent> e = stepAndDetect(g, GameInput{1.0f, 0.0f}, dt);
        all.insert(all.end(), e.begin(), e.end());
    }
    return all;
}

static bool has(const std::vector<GameEvent>& v, GameEventType t) {
    for (const GameEvent& e : v) if (e.type == t) return true;
    return false;
}
static int countType(const std::vector<GameEvent>& v, GameEventType t) {
    int n = 0;
    for (const GameEvent& e : v) if (e.type == t) ++n;
    return n;
}

int main() {
    // 1. Coin pickup event, at the coin's position.
    {
        SceneDescription s;
        s.spawns = {sp("player", 0), sp("coin", 1)};
        const std::vector<GameEvent> ev = driveEast(loadGame(s), 120);
        CHECK(countType(ev, GameEventType::CoinPickup) == 1);
        for (const GameEvent& e : ev)
            if (e.type == GameEventType::CoinPickup) CHECK(e.position.x == 1.0f);
    }

    // 2. Key pickup opens the matching door -> both events.
    {
        SceneDescription s;
        s.spawns = {sp("player", 0), sp("key@1", 1), sp("lock@1", 5)};
        const std::vector<GameEvent> ev = driveEast(loadGame(s), 120);
        CHECK(has(ev, GameEventType::KeyPickup));
        CHECK(has(ev, GameEventType::DoorOpen));
    }

    // 3. Switch toggle event (rising edge).
    {
        SceneDescription s;
        s.spawns = {sp("player", 0), sp("switch@1", 1), sp("toggle@1", 5)};
        const std::vector<GameEvent> ev = driveEast(loadGame(s), 120);
        CHECK(has(ev, GameEventType::SwitchToggle));
    }

    // 4. Win event when the last coin is collected and the exit reached.
    {
        SceneDescription s;
        s.spawns = {sp("player", 0), sp("coin", 1), sp("exit", 2)};
        const std::vector<GameEvent> ev = driveEast(loadGame(s), 200);
        CHECK(has(ev, GameEventType::CoinPickup));
        CHECK(has(ev, GameEventType::Win));
        CHECK(!has(ev, GameEventType::Lose));
    }

    // 5. Lose event when walking into a hazard.
    {
        SceneDescription s;
        s.spawns = {sp("player", 0), sp("hazard", 1)};
        const std::vector<GameEvent> ev = driveEast(loadGame(s), 120);
        CHECK(has(ev, GameEventType::Lose));
        CHECK(!has(ev, GameEventType::Win));
    }

    // 6. Effects are distinct per event type (burst size, color, sound cue).
    {
        const GameEventType types[] = {GameEventType::CoinPickup, GameEventType::KeyPickup,
                                       GameEventType::DoorOpen,   GameEventType::SwitchToggle,
                                       GameEventType::Win,        GameEventType::Lose};
        std::set<std::string> sounds;
        for (GameEventType t : types) {
            const EffectSpec fx = effectFor(t);
            CHECK(fx.particles > 0);
            CHECK(!fx.sound.empty());
            sounds.insert(fx.sound);
        }
        CHECK(sounds.size() == 6); // every event has its own sound cue
        // Win reads green, lose reads red.
        CHECK(effectFor(GameEventType::Win).g > 0.5f && effectFor(GameEventType::Win).r < 0.5f);
        CHECK(effectFor(GameEventType::Lose).r > 0.5f && effectFor(GameEventType::Lose).g < 0.5f);
    }

    // 7. Deterministic: the same run yields the same event sequence.
    {
        SceneDescription s;
        s.spawns = {sp("player", 0), sp("coin", 1), sp("exit", 2)};
        const std::vector<GameEvent> a = driveEast(loadGame(s), 200);
        const std::vector<GameEvent> b = driveEast(loadGame(s), 200);
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i].type == b[i].type);
    }

    if (g_failures == 0) {
        std::printf("test_game_events: all checks passed\n");
        return 0;
    }
    std::printf("test_game_events: %d check(s) failed\n", g_failures);
    return 1;
}
