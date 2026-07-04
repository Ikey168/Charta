#pragma once

#include "game/DungeonGame.h" // DungeonGame, GameInput, GameStatus, ecs::Vec3

#include <string>
#include <vector>

/**
 * @file GameEvents.h
 * @brief Detect gameplay events and map them to particle + audio effects (issue #353).
 *
 * The particle system (#267) and audio system (#258) exist; this is the renderer-agnostic
 * seam that decides "what feedback, where". detectEvents() diffs two snapshots of the same
 * game across one step into discrete events (a coin/key picked up, a door opened, a switch
 * flipped, a win, a loss), each carrying the world position it happened at. effectFor() maps
 * an event type to an EffectSpec (a particle burst count + color and a sound cue name). The
 * live engine feeds each event's EffectSpec to ParticleSystem::emitBurst and
 * AudioSystem::play; here it stays header-only and headlessly testable (no GL/OpenAL), and
 * fully deterministic - the same run yields the same events.
 */
namespace IKore {
namespace game {

enum class GameEventType { CoinPickup, KeyPickup, DoorOpen, SwitchToggle, Win, Lose };

/// A discrete gameplay event and where it happened.
struct GameEvent {
    GameEventType type{GameEventType::CoinPickup};
    ecs::Vec3 position{};
};

/// The feedback for an event: a particle burst (count + color) and a sound cue name.
struct EffectSpec {
    int particles{0};
    float r{1.0f}, g{1.0f}, b{1.0f};
    std::string sound;
};

/// Deterministic per-type effect (burst size, color, sound cue).
inline EffectSpec effectFor(GameEventType type) {
    switch (type) {
        case GameEventType::CoinPickup: return {14, 0.92f, 0.85f, 0.20f, "coin"};
        case GameEventType::KeyPickup: return {16, 0.95f, 0.80f, 0.20f, "key"};
        case GameEventType::DoorOpen: return {10, 0.60f, 0.40f, 0.20f, "door"};
        case GameEventType::SwitchToggle: return {8, 0.40f, 0.85f, 0.90f, "switch"};
        case GameEventType::Win: return {40, 0.20f, 0.80f, 0.35f, "win"};
        case GameEventType::Lose: return {30, 0.90f, 0.20f, 0.15f, "lose"};
    }
    return {};
}

/// The EffectSpec for a concrete event.
inline EffectSpec effectFor(const GameEvent& e) { return effectFor(e.type); }

/**
 * @brief Diff two snapshots of the same game (before/after one update) into the events that
 *        occurred: coin/key pickups, doors opened, switches flipped, and a win or loss.
 */
inline std::vector<GameEvent> detectEvents(const DungeonGame& before, const DungeonGame& after) {
    std::vector<GameEvent> ev;
    for (std::size_t i = 0; i < after.coins.size() && i < before.coins.size(); ++i)
        if (!before.coins[i].collected && after.coins[i].collected)
            ev.push_back({GameEventType::CoinPickup, after.coins[i].position});
    for (std::size_t i = 0; i < after.keys.size() && i < before.keys.size(); ++i)
        if (!before.keys[i].collected && after.keys[i].collected)
            ev.push_back({GameEventType::KeyPickup, after.keys[i].position});
    for (std::size_t i = 0; i < after.lockedDoors.size() && i < before.lockedDoors.size(); ++i)
        if (!before.lockedDoors[i].open && after.lockedDoors[i].open)
            ev.push_back({GameEventType::DoorOpen, after.lockedDoors[i].box.center});
    for (std::size_t i = 0; i < after.switches.size() && i < before.switches.size(); ++i)
        if (!before.switches[i].wasPressed && after.switches[i].wasPressed)
            ev.push_back({GameEventType::SwitchToggle, after.switches[i].position});
    if (before.status == GameStatus::Playing && after.status == GameStatus::Won)
        ev.push_back({GameEventType::Win, after.playerPosition});
    if (before.status == GameStatus::Playing && after.status == GameStatus::Lost)
        ev.push_back({GameEventType::Lose, after.playerPosition});
    return ev;
}

/// Step @p game one tick and return the events that occurred (the live driver's per-frame call).
inline std::vector<GameEvent> stepAndDetect(DungeonGame& game, const GameInput& in, float dt) {
    const DungeonGame before = game;
    game.update(in, dt);
    return detectEvents(before, game);
}

} // namespace game
} // namespace IKore
