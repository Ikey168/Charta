#pragma once

#include "game/DoodleRollback.h" // DoodleNetState, makeDoodleNetState, makeDoodleStepFn
#include "game/DungeonGame.h"    // DungeonGame, GameInput, GameStatus
#include "game/DoodleScene.h"    // LevelSpec, convert
#include "game/LevelFormat.h"    // fromLevelJson
#include "game/Versus.h"         // VersusStanding, versusProgress (shared with 2-player versus)
#include "net/Rollback.h"        // net::RollbackSession

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

/**
 * @file VersusFFA.h
 * @brief N-player (3-4) free-for-all versus over the rollback session (issue #318).
 *
 * Versus (#293) and RollbackSession are already parameterized on player count, but the
 * mode was fixed at two. This extends the same rollback-synced, first-to-clear, anti-cheat
 * model to N players (N in [2,4]) on one seed-shared level, adding a final standings order
 * over all N. The 2-player case is exactly this generalization, so behavior is unchanged.
 *
 *   - replayFFA(): the anti-cheat truth, mirroring replayVersus for N traces. Steps every
 *     player's game in lockstep, resolves the winner by first clear (deterministic
 *     lower-index tie-break), and ranks all N into a final standings order (cleared first
 *     by clear step, then the rest by progress).
 *   - FFAMatchRecord / verifyFFA: a self-contained N-way record whose claimed winner is
 *     trusted only when the deterministic replay reproduces it.
 *   - VersusFFA: one instance per peer over a RollbackSession; per-tick standings for all N
 *     for a HUD, and a verifiable record from the confirmed input prefix. N peers that
 *     exchange all inputs hold identical confirmed traces and agree on winner and standings.
 *
 * Header-only, std only, no wall clock, deterministic.
 */
namespace IKore {
namespace game {

/// The decided outcome of an N-player match, from a deterministic re-simulation.
struct FFAResult {
    bool decided{false};
    int winner{-1};
    int winnerClearStep{-1};
    std::vector<int> clearStep;  ///< per-player first-clear step (-1 if never).
    std::vector<char> lost;      ///< per-player caught-by-enemy at the decision.
    std::vector<int> standings;  ///< player indices, best-first (final ranking).
    int players() const { return static_cast<int>(clearStep.size()); }
};

/**
 * @brief Re-simulate an N-player match: load N fresh games of @p levelJson, step each by
 *        its per-frame inputs at @p dt in lockstep, and resolve the winner as the first
 *        player to clear (a same-step tie goes to the lower index). Ranks all N into a
 *        final standings order: players who cleared first (by clear step, then index), then
 *        the rest by descending progress (then index). Deterministic on any instance.
 */
inline FFAResult replayFFA(const std::string& levelJson, float dt,
                           const std::vector<std::vector<GameInput>>& inputs) {
    FFAResult r;
    const int n = static_cast<int>(inputs.size());
    if (n < 1) return r;
    LevelSpec spec;
    if (!fromLevelJson(levelJson, spec)) return r;
    const DungeonGame base = loadGame(convert(spec));
    std::vector<DungeonGame> games(static_cast<std::size_t>(n), base);
    r.clearStep.assign(static_cast<std::size_t>(n), -1);
    r.lost.assign(static_cast<std::size_t>(n), 0);

    std::size_t maxLen = 0;
    for (const std::vector<GameInput>& t : inputs) maxLen = t.size() > maxLen ? t.size() : maxLen;

    bool anyClear = false;
    for (std::size_t i = 0; i < maxLen && !anyClear; ++i) {
        for (int p = 0; p < n; ++p) {
            if (games[static_cast<std::size_t>(p)].status != GameStatus::Playing) continue;
            const std::vector<GameInput>& tr = inputs[static_cast<std::size_t>(p)];
            const GameInput in = i < tr.size() ? tr[i] : GameInput{};
            games[static_cast<std::size_t>(p)].update(in, dt);
        }
        for (int p = 0; p < n; ++p) {
            if (r.clearStep[static_cast<std::size_t>(p)] < 0 && games[static_cast<std::size_t>(p)].won()) {
                r.clearStep[static_cast<std::size_t>(p)] = static_cast<int>(i) + 1;
                anyClear = true;
            }
            r.lost[static_cast<std::size_t>(p)] = games[static_cast<std::size_t>(p)].lost() ? 1 : 0;
        }
    }

    for (int p = 0; p < n; ++p) {
        const int cs = r.clearStep[static_cast<std::size_t>(p)];
        if (cs < 0) continue;
        if (r.winner < 0 || cs < r.winnerClearStep) { r.winner = p; r.winnerClearStep = cs; }
    }
    r.decided = r.winner >= 0;

    std::vector<int> order(static_cast<std::size_t>(n));
    for (int p = 0; p < n; ++p) order[static_cast<std::size_t>(p)] = p;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int ca = r.clearStep[static_cast<std::size_t>(a)];
        const int cb = r.clearStep[static_cast<std::size_t>(b)];
        if ((ca >= 0) != (cb >= 0)) return ca >= 0; // cleared players rank first
        if (ca >= 0 && cb >= 0) return ca != cb ? ca < cb : a < b;
        const float pa = versusProgress(games[static_cast<std::size_t>(a)]);
        const float pb = versusProgress(games[static_cast<std::size_t>(b)]);
        return pa != pb ? pa > pb : a < b;
    });
    r.standings = order;
    return r;
}

/// A self-contained, verifiable record of a finished N-player match.
struct FFAMatchRecord {
    std::string levelJson;
    double dt{1.0 / 60.0};
    std::vector<std::vector<GameInput>> inputs;
    int claimedWinner{-1};
};

/// Re-simulate a record and check the claimed winner (the anti-cheat gate).
inline bool verifyFFA(const FFAMatchRecord& rec, FFAResult& out) {
    out = replayFFA(rec.levelJson, static_cast<float>(rec.dt), rec.inputs);
    return out.decided && out.winner == rec.claimedWinner;
}

/**
 * @brief A live N-player FFA match driven by the rollback session (one instance per peer).
 *
 * Same shape as Versus, generalized to N in [2,4]. advance() steps a frame with the local
 * input (remote inputs predicted); addRemoteInput() feeds authoritative inputs and rolls
 * back on a misprediction. result() re-simulates the confirmed prefix to the verifiable
 * winner and standings, and record() emits an FFAMatchRecord.
 */
class VersusFFA {
public:
    VersusFFA(const std::string& levelJson, int localPlayer, int numPlayers, float dt,
              int maxRollbackFrames = 256)
        : m_dt(dt), m_local(localPlayer), m_players(numPlayers), m_level(levelJson) {
        if (numPlayers < 2 || numPlayers > 4) return;
        if (localPlayer < 0 || localPlayer >= numPlayers) return;
        DoodleNetState init = makeDoodleNetState(levelJson, numPlayers);
        if (static_cast<int>(init.games.size()) != numPlayers) return;
        m_inputs.assign(static_cast<std::size_t>(numPlayers), {});
        m_have.assign(static_cast<std::size_t>(numPlayers), {});
        m_session.reset(new net::RollbackSession<DoodleNetState, GameInput>(
            numPlayers, localPlayer, init, makeDoodleStepFn(dt), maxRollbackFrames));
        m_valid = true;
    }

    bool valid() const { return m_valid; }
    int players() const { return m_players; }
    int localPlayer() const { return m_local; }
    int currentFrame() const { return m_valid ? m_session->currentFrame() : 0; }
    int rollbackCount() const { return m_valid ? m_session->rollbackCount() : 0; }

    void advance(const GameInput& localInput) {
        if (!m_valid) return;
        record(m_local, m_session->currentFrame(), localInput);
        m_session->advanceFrame(localInput);
    }

    void addRemoteInput(int player, int frame, const GameInput& input) {
        if (!m_valid || frame < 0 || player < 0 || player >= m_players) return;
        record(player, frame, input);
        m_session->addRemoteInput(player, frame, input);
    }

    const DoodleNetState& state() const { return m_session->state(); }

    VersusStanding standing(int player) const {
        VersusStanding s;
        s.player = player;
        if (!m_valid || player < 0 || player >= m_players) return s;
        const DungeonGame& g = m_session->state().games[static_cast<std::size_t>(player)];
        s.coinsCollected = g.coinsCollected;
        s.finished = g.status != GameStatus::Playing;
        s.won = g.won();
        s.lost = g.lost();
        s.progress = versusProgress(g);
        return s;
    }

    /// Live standings for all N (HUD), best-first by progress then index.
    std::vector<VersusStanding> standings() const {
        std::vector<VersusStanding> v;
        for (int p = 0; p < m_players; ++p) v.push_back(standing(p));
        std::stable_sort(v.begin(), v.end(), [](const VersusStanding& a, const VersusStanding& b) {
            return a.progress != b.progress ? a.progress > b.progress : a.player < b.player;
        });
        return v;
    }

    std::vector<GameInput> confirmedInputs(int player) const {
        std::vector<GameInput> out;
        if (!m_valid || player < 0 || player >= m_players) return out;
        const int len = confirmedPrefixLen();
        out.assign(m_inputs[static_cast<std::size_t>(player)].begin(),
                   m_inputs[static_cast<std::size_t>(player)].begin() + len);
        return out;
    }

    FFAResult result() const {
        if (!m_valid) return FFAResult{};
        std::vector<std::vector<GameInput>> all;
        for (int p = 0; p < m_players; ++p) all.push_back(confirmedInputs(p));
        return replayFFA(m_level, m_dt, all);
    }

    bool decided() const { return result().decided; }

    FFAMatchRecord record() const {
        FFAMatchRecord m;
        m.levelJson = m_level;
        m.dt = static_cast<double>(m_dt);
        for (int p = 0; p < m_players; ++p) m.inputs.push_back(confirmedInputs(p));
        m.claimedWinner = result().winner;
        return m;
    }

private:
    void record(int player, int frame, const GameInput& in) {
        if (player < 0 || player >= m_players) return;
        std::vector<GameInput>& v = m_inputs[static_cast<std::size_t>(player)];
        std::vector<bool>& h = m_have[static_cast<std::size_t>(player)];
        if (static_cast<int>(v.size()) <= frame) {
            v.resize(static_cast<std::size_t>(frame) + 1);
            h.resize(static_cast<std::size_t>(frame) + 1, false);
        }
        v[static_cast<std::size_t>(frame)] = in;
        h[static_cast<std::size_t>(frame)] = true;
    }

    int confirmedPrefixLen() const {
        for (int f = 0;; ++f) {
            for (int p = 0; p < m_players; ++p) {
                if (f >= static_cast<int>(m_have[static_cast<std::size_t>(p)].size()) ||
                    !m_have[static_cast<std::size_t>(p)][static_cast<std::size_t>(f)]) {
                    return f;
                }
            }
        }
    }

    float m_dt;
    int m_local;
    int m_players;
    std::string m_level;
    bool m_valid{false};
    std::vector<std::vector<GameInput>> m_inputs;
    std::vector<std::vector<bool>> m_have;
    std::unique_ptr<net::RollbackSession<DoodleNetState, GameInput>> m_session;
};

} // namespace game
} // namespace IKore
