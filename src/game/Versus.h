#pragma once

#include "game/DoodleRollback.h" // DoodleNetState, makeDoodleNetState, makeDoodleStepFn, doodleStep
#include "game/DungeonGame.h"    // DungeonGame, GameInput, GameStatus
#include "game/DoodleScene.h"    // LevelSpec, convert
#include "game/LevelFormat.h"    // fromLevelJson
#include "game/Leaderboard.h"    // RunTrace (reuse the leaderboard replay-verification style, #242)
#include "net/Rollback.h"        // net::RollbackSession

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

/**
 * @file Versus.h
 * @brief Real-time two-player versus over the rollback session (issue #293).
 *
 * With the rollback adapter (#291) plus seed-shared levels (weekly challenge #273,
 * share codes #241), two players can race the same level in real time. This layers
 * both players' inputs onto a single RollbackSession over a DoodleNetState (one game
 * per player, same level) and resolves the winner by first clear, with a deterministic
 * tie-break.
 *
 * Anti-cheat reuses the leaderboard replay style (#242): DungeonGame is fully
 * deterministic, so a match is fully described by the level plus each player's
 * fixed-timestep input trace. replayVersus() re-simulates both traces and is the single
 * source of truth for the winner - a reported win that does not re-simulate to the same
 * outcome is rejected (verifyMatch). Because two peers on a rollback session converge to
 * the identical confirmed input history, they re-simulate to the identical winner.
 *
 * Live per-tick standings (each player's progress) are exposed for a HUD. The
 * single-player DungeonGame and the rollback core are untouched; this is purely
 * additive. Header-only, std only, no wall clock.
 */
namespace IKore {
namespace game {

/// A player's live standing at the current tick (for a HUD). Derived from the live
/// (possibly-predicted) session state, so it updates every frame.
struct VersusStanding {
    int player{0};
    float progress{0.0f};   ///< higher is further along (coins collected, then nearer the exit).
    int coinsCollected{0};
    bool finished{false};   ///< the player's game ended (won or lost).
    bool won{false};
    bool lost{false};
};

/// The decided outcome of a match (from a deterministic re-simulation).
struct VersusResult {
    bool decided{false};       ///< a player cleared, so there is a winner.
    int winner{-1};            ///< 0 or 1; -1 if undecided (neither cleared).
    int winnerClearStep{-1};   ///< step at which the winner cleared.
    int clearStep[2]{-1, -1};  ///< per-player first-clear step (-1 if never).
    bool lost[2]{false, false};///< per-player caught-by-enemy at the decision.
};

/// Progress metric shared by the live HUD and diagnostics: each collected coin
/// dominates, then nearness to the exit; a win is a large bonus.
inline float versusProgress(const DungeonGame& g) {
    float exitDist = 0.0f;
    if (g.hasExit) {
        const float dx = g.exitPosition.x - g.playerPosition.x;
        const float dz = g.exitPosition.z - g.playerPosition.z;
        exitDist = std::sqrt(dx * dx + dz * dz);
    }
    return static_cast<float>(g.coinsCollected) * 1000.0f - exitDist + (g.won() ? 1e6f : 0.0f);
}

/**
 * @brief Re-simulate a two-player match: the anti-cheat truth (mirrors
 *        Leaderboard::replayRun, #242). Loads two fresh games of @p levelJson, steps
 *        both by their per-frame inputs at the fixed @p dt in lockstep, and resolves the
 *        winner as the first player to clear. A tie (both clear on the same step) goes to
 *        the lower player index. The same traces always yield the same result on any
 *        instance, so a client and a verifier agree.
 */
inline VersusResult replayVersus(const std::string& levelJson, float dt,
                                 const std::vector<GameInput>& in0,
                                 const std::vector<GameInput>& in1) {
    VersusResult r;
    LevelSpec spec;
    if (!fromLevelJson(levelJson, spec)) return r;
    const DungeonGame base = loadGame(convert(spec));
    DungeonGame g0 = base, g1 = base;

    const std::size_t n = in0.size() > in1.size() ? in0.size() : in1.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GameInput a = i < in0.size() ? in0[i] : GameInput{};
        const GameInput b = i < in1.size() ? in1[i] : GameInput{};
        if (g0.status == GameStatus::Playing) g0.update(a, dt);
        if (g1.status == GameStatus::Playing) g1.update(b, dt);
        if (r.clearStep[0] < 0 && g0.won()) r.clearStep[0] = static_cast<int>(i) + 1;
        if (r.clearStep[1] < 0 && g1.won()) r.clearStep[1] = static_cast<int>(i) + 1;
        r.lost[0] = g0.lost();
        r.lost[1] = g1.lost();
        if (r.clearStep[0] >= 0 || r.clearStep[1] >= 0) break; // the first clear step decides the match
    }

    const int c0 = r.clearStep[0], c1 = r.clearStep[1];
    if (c0 >= 0 && (c1 < 0 || c0 <= c1)) { // lower index wins a same-step tie
        r.decided = true; r.winner = 0; r.winnerClearStep = c0;
    } else if (c1 >= 0) {
        r.decided = true; r.winner = 1; r.winnerClearStep = c1;
    }
    return r;
}

/// A self-contained, verifiable record of a finished match: the level, the fixed
/// timestep, both players' input traces, and the claimed winner. verifyMatch()
/// re-simulates it, so the claim cannot be faked.
struct MatchRecord {
    std::string levelJson;
    double dt{1.0 / 60.0};
    std::vector<GameInput> inputs0;
    std::vector<GameInput> inputs1;
    int claimedWinner{-1};
};

/**
 * @brief Re-simulate a match record and check the claimed winner. Returns true only if
 *        the match is decided and the re-simulated winner equals @p rec.claimedWinner.
 *        @p out receives the authoritative result. This is the anti-cheat gate: a
 *        reported win is trusted only when the deterministic replay reproduces it.
 */
inline bool verifyMatch(const MatchRecord& rec, VersusResult& out) {
    out = replayVersus(rec.levelJson, static_cast<float>(rec.dt), rec.inputs0, rec.inputs1);
    return out.decided && out.winner == rec.claimedWinner;
}

/**
 * @brief A live two-player versus match driven by the rollback session.
 *
 * One instance per peer (its own @p localPlayer). advance() steps a frame with the local
 * input (remote inputs predicted); addRemoteInput() feeds authoritative remote inputs and
 * rolls back on a misprediction. Both players' authoritative inputs are recorded as they
 * are confirmed, so result() re-simulates the confirmed prefix to the verifiable winner
 * and record() emits a MatchRecord. Two peers that exchange all inputs hold identical
 * confirmed traces and therefore agree on the winner.
 */
class Versus {
public:
    /// Build a 2-player match on @p levelJson for @p localPlayer (0 or 1). valid() is
    /// false if the level fails to parse.
    Versus(const std::string& levelJson, int localPlayer, float dt, int maxRollbackFrames = 256)
        : m_dt(dt), m_local(localPlayer), m_level(levelJson) {
        DoodleNetState init = makeDoodleNetState(levelJson, 2);
        if (init.games.size() == 2 && (localPlayer == 0 || localPlayer == 1)) {
            m_session.reset(new net::RollbackSession<DoodleNetState, GameInput>(
                2, localPlayer, init, makeDoodleStepFn(dt), maxRollbackFrames));
            m_valid = true;
        }
    }

    bool valid() const { return m_valid; }
    int localPlayer() const { return m_local; }
    int currentFrame() const { return m_valid ? m_session->currentFrame() : 0; }
    int rollbackCount() const { return m_valid ? m_session->rollbackCount() : 0; }

    /// Advance one frame with the local player's authoritative input; missing remote
    /// inputs are predicted (repeat-last).
    void advance(const GameInput& localInput) {
        if (!m_valid) return;
        record(m_local, m_session->currentFrame(), localInput);
        m_session->advanceFrame(localInput);
    }

    /// Apply an authoritative input from the remote peer for (player, frame); rolls back
    /// and resimulates if it corrects a misprediction.
    void addRemoteInput(int player, int frame, const GameInput& input) {
        if (!m_valid || frame < 0) return;
        record(player, frame, input);
        m_session->addRemoteInput(player, frame, input);
    }

    /// The live (possibly-predicted) rollback state, for rendering.
    const DoodleNetState& state() const { return m_session->state(); }

    /// Live standing for a HUD, from the current state.
    VersusStanding standing(int player) const {
        VersusStanding s;
        s.player = player;
        if (!m_valid || player < 0 || player > 1) return s;
        const DungeonGame& g = m_session->state().games[static_cast<std::size_t>(player)];
        s.coinsCollected = g.coinsCollected;
        s.finished = g.status != GameStatus::Playing;
        s.won = g.won();
        s.lost = g.lost();
        s.progress = versusProgress(g);
        return s;
    }

    /// Live leader by progress this tick: 1 player 0 ahead, -1 player 1 ahead, 0 tie.
    int liveLeader() const {
        const float d = standing(0).progress - standing(1).progress;
        if (d > 1e-4f) return 1;
        if (d < -1e-4f) return -1;
        return 0;
    }

    /// A player's authoritative input trace over the confirmed prefix (both players'
    /// inputs known and contiguous from frame 0).
    std::vector<GameInput> confirmedInputs(int player) const {
        std::vector<GameInput> out;
        if (!m_valid || player < 0 || player > 1) return out;
        const int len = confirmedPrefixLen();
        out.assign(m_inputs[player].begin(), m_inputs[player].begin() + len);
        return out;
    }

    /// The confirmed trace as a RunTrace (leaderboard-compatible, #242).
    RunTrace trace(int player) const {
        RunTrace t;
        t.dt = static_cast<double>(m_dt);
        t.inputs = confirmedInputs(player);
        return t;
    }

    /// The verifiable result: re-simulate the confirmed prefix. Undecided until a player
    /// clears within the confirmed range.
    VersusResult result() const {
        if (!m_valid) return VersusResult{};
        return replayVersus(m_level, m_dt, confirmedInputs(0), confirmedInputs(1));
    }

    /// True once result() has a winner.
    bool decided() const { return result().decided; }

    /// Emit a verifiable record of the match so far (level, traces, and the re-simulated
    /// winner). verifyMatch(record()) is true by construction.
    MatchRecord record() const {
        MatchRecord m;
        m.levelJson = m_level;
        m.dt = static_cast<double>(m_dt);
        m.inputs0 = confirmedInputs(0);
        m.inputs1 = confirmedInputs(1);
        m.claimedWinner = result().winner;
        return m;
    }

private:
    /// Store an authoritative input for (player, frame), growing the per-player log.
    void record(int player, int frame, const GameInput& in) {
        if (player < 0 || player > 1) return;
        std::vector<GameInput>& v = m_inputs[player];
        std::vector<bool>& h = m_have[player];
        if (static_cast<int>(v.size()) <= frame) {
            v.resize(static_cast<std::size_t>(frame) + 1);
            h.resize(static_cast<std::size_t>(frame) + 1, false);
        }
        v[static_cast<std::size_t>(frame)] = in;
        h[static_cast<std::size_t>(frame)] = true;
    }

    /// Length of the contiguous prefix [0, L) for which both players' inputs are confirmed.
    int confirmedPrefixLen() const {
        for (int f = 0;; ++f) {
            for (int p = 0; p < 2; ++p) {
                if (f >= static_cast<int>(m_have[p].size()) || !m_have[p][static_cast<std::size_t>(f)]) {
                    return f;
                }
            }
        }
    }

    float m_dt;
    int m_local;
    std::string m_level;
    bool m_valid{false};
    std::vector<GameInput> m_inputs[2]; ///< authoritative input log per player.
    std::vector<bool> m_have[2];        ///< which frames are confirmed per player.
    std::unique_ptr<net::RollbackSession<DoodleNetState, GameInput>> m_session;
};

} // namespace game
} // namespace IKore
