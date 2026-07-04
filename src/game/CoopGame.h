#pragma once

#include "core/sim/StateHash.h" // sim::StateHash (desync digest)
#include "game/DoodleScene.h"   // convert, LevelSpec
#include "game/DungeonGame.h"   // DungeonGame, GameInput, GameStatus, loadGame
#include "game/LevelFormat.h"   // fromLevelJson

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/**
 * @file CoopGame.h
 * @brief Shared-world co-op: N avatars in one DungeonGame (issue #356).
 *
 * The rollback adapter (#291/#337) runs one independent game per player. Co-op is the
 * opposite: a single shared world that N avatars inhabit at once. CoopGame reuses a
 * DungeonGame purely for its geometry and shared state - walls and closed-door collision
 * (blocked()), coins/keys/switches/toggle-walls/hazards, enemies and the exit - but drives
 * several avatars through it, folding N inputs into one deterministic step(). Pickups and
 * switches are shared (any avatar collects a coin, flips a switch), enemies chase the
 * nearest living avatar, a hazard or enemy kills the avatar it touches, the level clears
 * when every living avatar stands on the exit with all coins collected, and it is lost when
 * every avatar has died.
 *
 * coopDigest() hashes the whole shared state (every avatar + the world) so a rollback
 * session can detect desync, exactly like stateDigest (#337). Deterministic (no RNG, no wall
 * clock) and header-only; the single-player DungeonGame is untouched.
 */
namespace IKore {
namespace game {

struct CoopAvatar {
    ecs::Vec3 position{};
    bool alive{true};
};

/// A single shared world inhabited by N avatars.
struct CoopGame {
    DungeonGame world;               ///< geometry + shared collectibles/hazards/enemies/exit.
    std::vector<CoopAvatar> avatars; ///< the players in the one world.
    GameStatus status{GameStatus::Playing};

    int aliveCount() const {
        int n = 0;
        for (const CoopAvatar& a : avatars) n += a.alive ? 1 : 0;
        return n;
    }
    bool won() const { return status == GameStatus::Won; }
    bool lost() const { return status == GameStatus::Lost; }

    /// Advance the shared world one tick from one input per avatar (missing -> no move).
    void step(const std::vector<GameInput>& inputs, float dt) {
        if (status != GameStatus::Playing) return;

        // 1. Move each living avatar with wall/door collision (axis slide).
        for (std::size_t i = 0; i < avatars.size(); ++i) {
            CoopAvatar& av = avatars[i];
            if (!av.alive) continue;
            const GameInput in = i < inputs.size() ? inputs[i] : GameInput{};
            const float len = std::sqrt(in.moveX * in.moveX + in.moveZ * in.moveZ);
            if (len > 1e-6f) {
                const float step = world.playerSpeed * dt;
                av.position = slide(av.position, (in.moveX / len) * step, (in.moveZ / len) * step,
                                    world.playerRadius);
            }
        }

        // 2. Shared pickups + switches (any avatar triggers them).
        for (Coin& c : world.coins) {
            if (c.collected) continue;
            for (const CoopAvatar& av : avatars)
                if (av.alive && within(av.position, c.position, world.playerRadius + world.coinRadius)) {
                    c.collected = true;
                    ++world.coinsCollected;
                    break;
                }
        }
        for (Key& k : world.keys) {
            if (k.collected) continue;
            for (const CoopAvatar& av : avatars)
                if (av.alive && within(av.position, k.position, world.playerRadius + world.keyRadius)) {
                    k.collected = true;
                    for (LockedDoor& d : world.lockedDoors)
                        if (d.id == k.id) d.open = true;
                    break;
                }
        }
        for (Switch& sw : world.switches) {
            bool pressed = false;
            for (const CoopAvatar& av : avatars)
                if (av.alive && within(av.position, sw.position, world.playerRadius + world.switchRadius)) {
                    pressed = true;
                    break;
                }
            if (pressed && !sw.wasPressed)
                for (ToggleWall& w : world.toggleWalls)
                    if (w.id == sw.id) w.solid = !w.solid;
            sw.wasPressed = pressed;
        }

        // 3. Enemies chase the nearest living avatar; contact kills that avatar.
        for (Enemy& e : world.enemies) {
            int target = nearestAlive(e.position);
            if (target >= 0) {
                const ecs::Vec3 tp = avatars[static_cast<std::size_t>(target)].position;
                const float dx = tp.x - e.position.x, dz = tp.z - e.position.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d > 1e-6f) {
                    const float step = world.enemySpeed * dt;
                    e.position = slide(e.position, (dx / d) * step, (dz / d) * step, world.enemyRadius);
                }
            }
            for (CoopAvatar& av : avatars)
                if (av.alive && within(av.position, e.position, world.playerRadius + world.enemyRadius))
                    av.alive = false;
        }

        // 4. Hazards kill an avatar on contact.
        for (const Hazard& h : world.hazards)
            for (CoopAvatar& av : avatars)
                if (av.alive && within(av.position, h.position, world.playerRadius + world.hazardRadius))
                    av.alive = false;

        // 5. Resolve the shared win/lose.
        if (aliveCount() == 0) {
            status = GameStatus::Lost;
            return;
        }
        if (world.coinsCollected >= world.totalCoins && world.hasExit && allAliveAtExit()) {
            status = GameStatus::Won;
        }
    }

    bool allAliveAtExit() const {
        bool any = false;
        for (const CoopAvatar& av : avatars) {
            if (!av.alive) continue;
            any = true;
            if (!within(av.position, world.exitPosition, world.playerRadius + world.exitRadius))
                return false;
        }
        return any;
    }

private:
    static bool within(const ecs::Vec3& a, const ecs::Vec3& b, float r) {
        const float dx = a.x - b.x, dz = a.z - b.z;
        return dx * dx + dz * dz <= r * r;
    }
    ecs::Vec3 slide(const ecs::Vec3& from, float dx, float dz, float radius) const {
        const ecs::Vec3 full{from.x + dx, from.y, from.z + dz};
        if (!world.blocked(full, radius)) return full;
        const ecs::Vec3 xo{from.x + dx, from.y, from.z};
        if (!world.blocked(xo, radius)) return xo;
        const ecs::Vec3 zo{from.x, from.y, from.z + dz};
        if (!world.blocked(zo, radius)) return zo;
        return from;
    }
    int nearestAlive(const ecs::Vec3& p) const {
        int best = -1;
        float bestD = 0.0f;
        for (std::size_t i = 0; i < avatars.size(); ++i) {
            if (!avatars[i].alive) continue;
            const float dx = avatars[i].position.x - p.x, dz = avatars[i].position.z - p.z;
            const float d = dx * dx + dz * dz;
            if (best < 0 || d < bestD) { best = static_cast<int>(i); bestD = d; }
        }
        return best;
    }
};

/// Build a co-op game from a loaded level: @p numAvatars avatars at the player spawn.
inline CoopGame makeCoopGame(const DungeonGame& base, int numAvatars) {
    CoopGame g;
    g.world = base;
    if (numAvatars < 1) numAvatars = 1;
    g.avatars.assign(static_cast<std::size_t>(numAvatars), CoopAvatar{base.playerPosition, true});
    return g;
}

/// Build a co-op game from a level's JSON (empty avatars if the level fails to parse).
inline CoopGame makeCoopGameJson(const std::string& levelJson, int numAvatars) {
    LevelSpec spec;
    if (!fromLevelJson(levelJson, spec)) return CoopGame{};
    return makeCoopGame(loadGame(convert(spec)), numAvatars);
}

namespace detail {
inline void coopHashFloat(sim::StateHash& h, float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    h.addU32(bits);
}
} // namespace detail

/// A digest of the whole shared state (avatars + world) for desync detection.
inline std::uint64_t coopDigest(const CoopGame& g) {
    sim::StateHash h;
    h.addI32(static_cast<std::int32_t>(g.status));
    h.addI32(g.world.coinsCollected);
    for (const CoopAvatar& a : g.avatars) {
        detail::coopHashFloat(h, a.position.x);
        detail::coopHashFloat(h, a.position.z);
        h.addU32(a.alive ? 1u : 0u);
    }
    for (const Coin& c : g.world.coins) h.addU32(c.collected ? 1u : 0u);
    for (const Key& k : g.world.keys) h.addU32(k.collected ? 1u : 0u);
    for (const LockedDoor& d : g.world.lockedDoors) h.addU32(d.open ? 1u : 0u);
    for (const Switch& s : g.world.switches) h.addU32(s.wasPressed ? 1u : 0u);
    for (const ToggleWall& w : g.world.toggleWalls) h.addU32(w.solid ? 1u : 0u);
    for (const Enemy& e : g.world.enemies) {
        detail::coopHashFloat(h, e.position.x);
        detail::coopHashFloat(h, e.position.z);
    }
    return h.digest();
}

/// A minimal deterministic co-op session (mirrors the Versus/FFA entry points): fold one
/// input per avatar each tick and expose a digest for desync detection.
class CoopSession {
public:
    CoopSession(const std::string& levelJson, int numAvatars, float dt = 1.0f / 60.0f)
        : m_game(makeCoopGameJson(levelJson, numAvatars)), m_dt(dt) {}
    CoopSession(const DungeonGame& base, int numAvatars, float dt = 1.0f / 60.0f)
        : m_game(makeCoopGame(base, numAvatars)), m_dt(dt) {}

    bool valid() const { return !m_game.avatars.empty(); }
    void advance(const std::vector<GameInput>& inputs) {
        m_game.step(inputs, m_dt);
        ++m_tick;
    }
    int tick() const { return m_tick; }
    GameStatus status() const { return m_game.status; }
    const CoopGame& game() const { return m_game; }
    std::uint64_t digest() const { return coopDigest(m_game); }

private:
    CoopGame m_game;
    float m_dt;
    int m_tick{0};
};

} // namespace game
} // namespace IKore
