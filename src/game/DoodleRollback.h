#pragma once

#include "game/DungeonGame.h"  // DungeonGame, GameInput, loadGame, GameStatus
#include "game/DoodleScene.h"  // LevelSpec, convert
#include "game/LevelFormat.h"  // fromLevelJson
#include "core/sim/StateHash.h" // sim::StateHash (desync digest)
#include "net/Rollback.h"      // net::RollbackSession (the netcode core it plugs into)

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/**
 * @file DoodleRollback.h
 * @brief Adapter that lets the rollback netcode core drive the doodle game (issue #291).
 *
 * The GGPO-style RollbackSession (#160) is templated on a value-type State snapshotted
 * per frame and a small Input; the doodle game (DungeonGame, #164) is deterministic but
 * was never wired to it. This provides that bridge so a doodle level can be played
 * online (co-op race / versus, #293) with prediction and rollback:
 *
 *   - DoodleNetState: one DungeonGame per player, all loaded from the same level. It is
 *     a copyable value type (the rollback snapshot) with exact equality, so a session
 *     can snapshot/restore/resimulate it and detect divergence.
 *   - doodleStep(): the deterministic per-frame step - advance each player's game by
 *     that player's input for the frame - matching RollbackSession's StepFn shape.
 *   - stateDigest(): a StateHash over the state for desync detection.
 *
 * The single-player DungeonGame is untouched; this is purely additive. DungeonGame is
 * float-deterministic (the same replay assumption the leaderboards/#242 rely on), so
 * snapshot/restore/resim reproduce identical state on a given platform.
 * Header-only, std only.
 */
namespace IKore {
namespace game {

/// One independent doodle game per player (all the same level), as a rollback State.
struct DoodleNetState {
    std::vector<DungeonGame> games;

    /// Exact equality over the mutated state of every player's game, so two sessions
    /// (or a session and a no-network reference) can be compared for convergence.
    bool operator==(const DoodleNetState& o) const {
        if (games.size() != o.games.size()) return false;
        for (std::size_t i = 0; i < games.size(); ++i) {
            const DungeonGame& a = games[i];
            const DungeonGame& b = o.games[i];
            if (a.status != b.status || a.coinsCollected != b.coinsCollected) return false;
            if (a.playerPosition.x != b.playerPosition.x || a.playerPosition.z != b.playerPosition.z) return false;
            if (a.coins.size() != b.coins.size() || a.enemies.size() != b.enemies.size()) return false;
            for (std::size_t c = 0; c < a.coins.size(); ++c) {
                if (a.coins[c].collected != b.coins[c].collected) return false;
            }
            for (std::size_t e = 0; e < a.enemies.size(); ++e) {
                if (a.enemies[e].position.x != b.enemies[e].position.x ||
                    a.enemies[e].position.z != b.enemies[e].position.z) return false;
            }
        }
        return true;
    }
    bool operator!=(const DoodleNetState& o) const { return !(*this == o); }
};

namespace detail {
/// Fold a float's bit pattern into a StateHash (DungeonGame is float, StateHash is
/// integer; hashing the raw bits keeps the digest exact same-platform for desync checks).
inline void hashFloat(sim::StateHash& h, float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    h.addU32(bits);
}
} // namespace detail

/**
 * @brief Build the initial rollback state for @p levelJson and @p numPlayers.
 *        Each player gets an independent game of the same level. Empty games vector if
 *        the level fails to parse.
 */
inline DoodleNetState makeDoodleNetState(const std::string& levelJson, int numPlayers) {
    DoodleNetState s;
    if (numPlayers < 1) numPlayers = 1;
    LevelSpec spec;
    if (!fromLevelJson(levelJson, spec)) return s;
    const DungeonGame base = loadGame(convert(spec));
    s.games.assign(static_cast<std::size_t>(numPlayers), base);
    return s;
}

/// The rollback step: advance each player's game by that player's input for the frame.
/// A deterministic pure function of (state, inputs), matching RollbackSession::StepFn.
inline void doodleStep(DoodleNetState& state, const std::vector<GameInput>& inputs, int /*frame*/,
                       float dt) {
    for (std::size_t p = 0; p < state.games.size(); ++p) {
        const GameInput in = p < inputs.size() ? inputs[p] : GameInput{};
        state.games[p].update(in, dt);
    }
}

/// A ready-to-use StepFn bound to a fixed timestep (what RollbackSession takes).
inline net::RollbackSession<DoodleNetState, GameInput>::StepFn makeDoodleStepFn(float dt) {
    return [dt](DoodleNetState& s, const std::vector<GameInput>& in, int frame) {
        doodleStep(s, in, frame, dt);
    };
}

/// A digest of the whole state for desync detection (bit-exact same-platform).
inline std::uint64_t stateDigest(const DoodleNetState& state) {
    sim::StateHash h;
    for (const DungeonGame& g : state.games) {
        h.addI32(static_cast<std::int32_t>(g.status));
        h.addI32(g.coinsCollected);
        detail::hashFloat(h, g.playerPosition.x);
        detail::hashFloat(h, g.playerPosition.z);
        for (const Coin& c : g.coins) h.addU32(c.collected ? 1u : 0u);
        for (const Enemy& e : g.enemies) {
            detail::hashFloat(h, e.position.x);
            detail::hashFloat(h, e.position.z);
        }
    }
    return h.digest();
}

} // namespace game
} // namespace IKore
