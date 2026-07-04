#pragma once

#include "game/DoodleScene.h"  // convert, SceneDescription
#include "game/DungeonGame.h"  // DungeonGame, GameInput, loadGame
#include "game/Fairness.h"     // checkLevelFairness (opt-in solvability gate, #334)
#include "game/LevelFormat.h"  // fromLevelJson, LevelSpec
#include "game/LevelShare.h"   // shareCodeFor

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file Leaderboard.h
 * @brief Fastest-clear leaderboards with deterministic-replay anti-cheat (issue #242).
 *
 * Milestone 17, "Doodlebound" Phase 5. Ranks players by clear time per level,
 * keyed by the content share code from LevelShare.h (#241). Because DungeonGame is
 * fully deterministic (no RNG; enemies chase deterministically), a fixed-timestep
 * input trace replays to the identical result on any device - so a submitted time
 * is verifiable: the server re-simulates the trace and trusts only what it computes.
 *
 *   - replayRun(levelJson, trace): the anti-cheat core. Re-simulates a fixed-step
 *     input trace against a level and reports whether it cleared and in what time.
 *   - Leaderboard: registerLevel() (returns the level's share code), submit() (which
 *     re-simulates and rejects runs that do not clear, use the wrong timestep, or
 *     whose claimed time desyncs from the replay), and top()/rankOf() for the
 *     per-level ranked list (fastest first, best-per-player).
 *
 * Timestamps are caller-supplied, so ordering is deterministic and there is no wall
 * clock. Header-only, reusing the existing level JSON - no bespoke serialization.
 */
namespace IKore {
namespace game {

/// A recorded run: a fixed simulation timestep and the per-step player input.
struct RunTrace {
    double dt{1.0 / 60.0};
    std::vector<GameInput> inputs;
};

/// Outcome of re-simulating a trace.
struct RunResult {
    bool won{false};
    int steps{0};         ///< steps taken to clear (0 if not cleared).
    double clearTime{0.0}; ///< steps * dt.
};

/**
 * @brief Deterministically replay an input trace against a level (the anti-cheat
 *        core). Loads a fresh game from the level JSON, applies each input at the
 *        trace's fixed dt, and stops at the first win (or loss). Returns whether the
 *        run cleared and the resulting time. The same trace always yields the same
 *        result, so a client and the server agree.
 */
inline RunResult replayRun(const std::string& levelJson, const RunTrace& trace) {
    RunResult r;
    LevelSpec spec;
    if (!fromLevelJson(levelJson, spec)) return r;
    DungeonGame game = loadGame(convert(spec));
    for (std::size_t i = 0; i < trace.inputs.size(); ++i) {
        game.update(trace.inputs[i], static_cast<float>(trace.dt));
        if (game.won()) {
            r.won = true;
            r.steps = static_cast<int>(i) + 1;
            r.clearTime = r.steps * trace.dt;
            return r;
        }
        if (game.lost()) return r; // a loss is not a clear
    }
    return r;
}

/// One player's best clear on a level.
struct ScoreEntry {
    std::string player;
    double clearTime{0.0};
    int steps{0};
    std::int64_t submittedAt{0};
};

/// Result of a submission attempt.
struct SubmitResult {
    bool accepted{false};  ///< the run cleared and verified.
    bool improved{false};  ///< it was the player's first or a new personal best.
    double clearTime{0.0};
    int rank{0};           ///< 1-based rank after this submission (0 if not accepted).
    std::string reason;    ///< why an unaccepted run was rejected.
};

/// Per-level fastest-clear leaderboards with re-simulation verification.
class Leaderboard {
public:
    /// @param enforceFairness when true, registerLevel refuses an unsolvable level and
    ///        records its solver par (#334). Off by default, so existing behavior is
    ///        unchanged.
    explicit Leaderboard(double fixedDt = 1.0 / 60.0, bool enforceFairness = false)
        : m_fixedDt(fixedDt), m_enforceFairness(enforceFairness) {}

    /// Register a level for competition; returns its content share code. When fairness is
    /// enforced, an unsolvable level is refused (returns "", with lastRegisterReason set)
    /// and a fair level's par is recorded (parFor()).
    std::string registerLevel(const std::string& levelJson) {
        const std::string code = shareCodeFor(levelJson);
        if (m_enforceFairness) {
            const FairnessResult f = checkLevelFairness(levelJson);
            if (!f.fair) {
                m_lastRegisterReason = f.reason;
                return "";
            }
            m_par[code] = f.par;
        }
        m_levels[code] = levelJson;
        return code;
    }

    bool fairnessEnforced() const { return m_enforceFairness; }
    /// Reason the last registerLevel rejected a level ("" if it succeeded).
    const std::string& lastRegisterReason() const { return m_lastRegisterReason; }
    /// The solver par recorded for a fair, registered level (-1 if unknown).
    int parFor(const std::string& code) const {
        auto it = m_par.find(code);
        return it == m_par.end() ? -1 : it->second;
    }
    bool hasLevel(const std::string& code) const { return m_levels.count(code) > 0; }
    double fixedDt() const { return m_fixedDt; }

    /**
     * @brief Submit a run for @p player on level @p code.
     *
     * Re-simulates the trace and accepts it only if it cleared. Rejections: unknown
     * level, a timestep other than the leaderboard's fixed dt, a trace that does not
     * clear, or (when @p claimedTime >= 0) a claimed time that disagrees with the
     * re-simulated time (a desync / tamper). Accepted runs keep the player's best.
     */
    SubmitResult submit(const std::string& code, const std::string& player, const RunTrace& trace,
                        std::int64_t submittedAt, double claimedTime = -1.0) {
        SubmitResult res;
        auto it = m_levels.find(code);
        if (it == m_levels.end()) {
            res.reason = "unknown level";
            return res;
        }
        if (std::fabs(trace.dt - m_fixedDt) > 1e-9) {
            res.reason = "bad timestep";
            return res;
        }
        if (trace.inputs.size() > kMaxInputs) {
            res.reason = "trace too long";
            return res;
        }
        const RunResult rr = replayRun(it->second, trace);
        if (!rr.won) {
            res.reason = "did not clear";
            return res;
        }
        if (claimedTime >= 0.0 && std::fabs(claimedTime - rr.clearTime) > 1e-6) {
            res.reason = "desync";
            return res;
        }

        res.accepted = true;
        res.clearTime = rr.clearTime;
        std::vector<ScoreEntry>& board = m_scores[code];
        ScoreEntry* existing = nullptr;
        for (ScoreEntry& e : board) {
            if (e.player == player) { existing = &e; break; }
        }
        if (existing == nullptr) {
            board.push_back(ScoreEntry{player, rr.clearTime, rr.steps, submittedAt});
            res.improved = true;
        } else if (rr.clearTime < existing->clearTime) {
            existing->clearTime = rr.clearTime;
            existing->steps = rr.steps;
            existing->submittedAt = submittedAt;
            res.improved = true;
        }
        // Keep the winning trace so a rank's ghost can be fetched later (issue #272).
        // Only on an improvement, so the stored trace always matches the ranked time.
        if (res.improved) {
            m_bestTraces[code][player] = trace;
        }
        res.rank = rankOf(code, player);
        return res;
    }

    /// The ranked list for a level (fastest first), one row per player.
    std::vector<ScoreEntry> top(const std::string& code, std::size_t limit = 50) const {
        auto it = m_scores.find(code);
        if (it == m_scores.end()) return {};
        std::vector<ScoreEntry> out = it->second;
        std::sort(out.begin(), out.end(), rankLess);
        if (out.size() > limit) out.resize(limit);
        return out;
    }

    /// 1-based rank of a player on a level, or 0 if they have no entry.
    int rankOf(const std::string& code, const std::string& player) const {
        const std::vector<ScoreEntry> board = top(code, kMaxInputs);
        for (std::size_t i = 0; i < board.size(); ++i) {
            if (board[i].player == player) return static_cast<int>(i) + 1;
        }
        return 0;
    }

    /// Number of ranked players on a level.
    std::size_t count(const std::string& code) const {
        auto it = m_scores.find(code);
        return it == m_scores.end() ? 0 : it->second.size();
    }

    /// The stored level JSON for a code (needed to bind/verify a ghost share, #272).
    bool levelJson(const std::string& code, std::string& out) const {
        auto it = m_levels.find(code);
        if (it == m_levels.end()) return false;
        out = it->second;
        return true;
    }

    /// The best (ranked) trace a player submitted on a level, for ghost playback (#272).
    bool bestTrace(const std::string& code, const std::string& player, RunTrace& out) const {
        auto lit = m_bestTraces.find(code);
        if (lit == m_bestTraces.end()) return false;
        auto pit = lit->second.find(player);
        if (pit == lit->second.end()) return false;
        out = pit->second;
        return true;
    }

    /// The best trace of the @p rank-th (1-based) player on a level, plus their name.
    bool ghostForRank(const std::string& code, int rank, RunTrace& out, std::string& player) const {
        if (rank < 1) return false;
        const std::vector<ScoreEntry> board = top(code, kMaxInputs);
        if (static_cast<std::size_t>(rank) > board.size()) return false;
        player = board[static_cast<std::size_t>(rank - 1)].player;
        return bestTrace(code, player, out);
    }

private:
    static bool rankLess(const ScoreEntry& a, const ScoreEntry& b) {
        if (a.clearTime != b.clearTime) return a.clearTime < b.clearTime;
        if (a.submittedAt != b.submittedAt) return a.submittedAt < b.submittedAt; // earlier ranks higher
        return a.player < b.player;
    }

    static const std::size_t kMaxInputs = 1000000;
    double m_fixedDt;
    bool m_enforceFairness{false};
    std::string m_lastRegisterReason;
    std::unordered_map<std::string, int> m_par; ///< solver par per fair level (when enforced).
    std::unordered_map<std::string, std::string> m_levels;
    std::unordered_map<std::string, std::vector<ScoreEntry>> m_scores;
    // Best trace per (level, player) for ghost replays (issue #272). Kept in step with
    // m_scores: written only when a submission improves the player's ranked time.
    std::unordered_map<std::string, std::unordered_map<std::string, RunTrace>> m_bestTraces;
};

} // namespace game
} // namespace IKore
