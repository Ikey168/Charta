#pragma once

#include "game/DungeonGame.h" // DungeonGame, GameInput, GameStatus
#include "game/Solver.h"      // solve, SolveResult

#include <cstdint>
#include <vector>

/**
 * @file AiOpponent.h
 * @brief Deterministic AI opponent for Versus / FFA (issue #357).
 *
 * A bot that turns the Solver (#316) into a per-tick GameInput stream so a human can face the
 * engine and the result still replay-verifies. The Solver is the oracle: the bot only plays a
 * level it proves solvable, and the optimal par sizes the difficulty budget. It then steers
 * toward the nearest objective - the closest uncollected coin, then the exit - which reliably
 * collects and clears (the solver route is what guarantees such a policy can win). Difficulty
 * tiers shape play with a seeded LCG: a tier-3 bot drives straight, while lower tiers hesitate
 * and occasionally jink, so they clear the same level more slowly. Fully deterministic (seed +
 * tier + level fix the stream, no wall clock, no std::rand), so a bot stream drops straight
 * into replayRun / replayFFA (#242/#318) as one player's inputs and a bot-vs-bot match
 * reproduces exactly. Header-only.
 */
namespace IKore {
namespace game {

struct BotConfig {
    int tier{2};        ///< 1..3 difficulty; 3 = near-optimal, 1 = sloppy.
    unsigned seed{1};   ///< fixes the (deterministic) hesitation/jink pattern.
};

namespace detail {
/// A tiny deterministic PRNG (its own algorithm, so the stream is identical on any platform).
struct BotLcg {
    std::uint32_t s;
    explicit BotLcg(unsigned seed) : s(seed ? seed : 1u) {}
    std::uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    int pct() { return static_cast<int>(next() % 100u); }
};
} // namespace detail

/**
 * @brief A deterministic input stream that clears @p base by following the solver route,
 *        shaped by the bot's difficulty tier. Empty if the level is unsolvable.
 */
inline std::vector<GameInput> botInputStream(const DungeonGame& base, const BotConfig& cfg,
                                             float dt = 1.0f / 60.0f, int maxSteps = 6000) {
    std::vector<GameInput> out;
    if (!solve(base).solvable) return out; // oracle: only play a winnable level

    const int tier = cfg.tier < 1 ? 1 : (cfg.tier > 3 ? 3 : cfg.tier);
    const int idleChance = tier >= 3 ? 0 : (tier == 2 ? 8 : 20);   // % chance to hesitate a frame
    const int jinkChance = tier >= 3 ? 0 : (tier == 2 ? 0 : 6);    // % chance to veer for a frame
    detail::BotLcg rng(cfg.seed * 2654435761u + static_cast<unsigned>(tier));

    DungeonGame g = base;
    for (int i = 0; i < maxSteps && g.status == GameStatus::Playing; ++i) {
        // Aim at the nearest uncollected coin, else the exit.
        ecs::Vec3 tgt = g.exitPosition;
        float best = 1e30f;
        for (const Coin& c : g.coins) {
            if (c.collected) continue;
            const float dx = c.position.x - g.playerPosition.x, dz = c.position.z - g.playerPosition.z;
            const float d = dx * dx + dz * dz;
            if (d < best) { best = d; tgt = c.position; }
        }
        GameInput in{tgt.x - g.playerPosition.x, tgt.z - g.playerPosition.z};
        if (idleChance && rng.pct() < idleChance) {
            in = GameInput{0.0f, 0.0f};                          // hesitate
        } else if (jinkChance && rng.pct() < jinkChance) {
            in = GameInput{-in.moveZ, in.moveX};                 // veer perpendicular a frame
        }
        g.update(in, dt);
        out.push_back(in);
    }
    return out;
}

/// Outcome of running a bot stream against the level (for tests / tuning).
struct BotRun {
    bool won{false};
    int steps{0};
    std::vector<GameInput> inputs;
};

/// Generate a bot stream and report whether (and in how many steps) it clears @p base.
inline BotRun runBot(const DungeonGame& base, const BotConfig& cfg, float dt = 1.0f / 60.0f) {
    BotRun r;
    r.inputs = botInputStream(base, cfg, dt);
    DungeonGame g = base;
    for (std::size_t i = 0; i < r.inputs.size(); ++i) {
        g.update(r.inputs[i], dt);
        if (g.won()) { r.won = true; r.steps = static_cast<int>(i) + 1; break; }
        if (g.lost()) break;
    }
    return r;
}

} // namespace game
} // namespace IKore
