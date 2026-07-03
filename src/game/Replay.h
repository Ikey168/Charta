#pragma once

#include "game/GhostReplay.h" // detail codec helpers (putLE/getLE, base64, traceCodeFor), shareCodeFor
#include "game/Versus.h"      // MatchRecord, replayVersus
#include "game/Leaderboard.h" // RunTrace, replayRun

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file Replay.h
 * @brief Shareable spectator replays with deterministic playback (issue #317).
 *
 * Leaderboards (#242) and ghosts (#272) already share a single run's input trace, and
 * versus (#293) makes a verifiable MatchRecord - but there was no compact, shareable
 * codec for a *match*, and no player that re-simulates a run or a match exposing every
 * player's per-tick state. This adds both, so a run or a versus match can be packaged
 * as one self-contained string that anyone can play back deterministically.
 *
 *   - A Replay bundles the level, the fixed dt, one input trace per player (1 = single
 *     run, 2 = versus), and the claimed result.
 *   - encodeReplay/decodeReplay: a content-verified share string "DDR1:<levelCode>:
 *     <payloadCode>:<base64url(payload)>", reusing the ghost/leaderboard share-code
 *     helpers. The level is embedded so the replay is self-contained; decode rejects a
 *     bad prefix, corrupt base64, a payload that does not match its hash (tamper), or a
 *     level whose recomputed code disagrees with the embedded one.
 *   - verifyReplay: re-simulates with replayRun / replayVersus and rejects a replay
 *     whose traces do not reproduce its claimed result.
 *   - ReplayPlayer: re-simulates at the fixed dt, exposing each player's DungeonGame
 *     per tick (single run and versus).
 *
 * Pure std, header-only, no wall clock, deterministic.
 */
namespace IKore {
namespace game {

/// A self-contained, replayable record of a run (1 trace) or a match (2 traces).
struct Replay {
    std::string levelJson;
    double dt{1.0 / 60.0};
    std::vector<std::vector<GameInput>> traces; ///< one input trace per player.
    int claimedWinner{-1};                       ///< versus: winner index; single run: 0 = claimed clear, -1 = none.
};

/// Package a single run (a leaderboard/ghost trace) as a replay; the claim is set from
/// a deterministic re-simulation so a legit run round-trips through verifyReplay.
inline Replay makeRunReplay(const std::string& levelJson, const RunTrace& trace) {
    Replay r;
    r.levelJson = levelJson;
    r.dt = trace.dt;
    r.traces.push_back(trace.inputs);
    r.claimedWinner = replayRun(levelJson, trace).won ? 0 : -1;
    return r;
}

/// Package a finished versus match (its MatchRecord) as a replay.
inline Replay makeVersusReplay(const MatchRecord& rec) {
    Replay r;
    r.levelJson = rec.levelJson;
    r.dt = rec.dt;
    r.traces.push_back(rec.inputs0);
    r.traces.push_back(rec.inputs1);
    r.claimedWinner = rec.claimedWinner;
    return r;
}

/// Re-simulate a replay and confirm it reproduces its claimed result.
inline bool verifyReplay(const Replay& r) {
    if (r.traces.size() >= 2) {
        const VersusResult vr =
            replayVersus(r.levelJson, static_cast<float>(r.dt), r.traces[0], r.traces[1]);
        return vr.decided && vr.winner == r.claimedWinner;
    }
    if (r.traces.size() == 1) {
        RunTrace t;
        t.dt = r.dt;
        t.inputs = r.traces[0];
        return replayRun(r.levelJson, t).won == (r.claimedWinner == 0);
    }
    return false;
}

namespace detail {

inline std::string serializeReplay(const Replay& r) {
    std::string p;
    putLE64(p, doubleBits(r.dt));
    putLE32(p, static_cast<std::uint32_t>(r.claimedWinner)); // two's-complement, round-trips
    putLE32(p, static_cast<std::uint32_t>(r.traces.size()));
    putLE32(p, static_cast<std::uint32_t>(r.levelJson.size()));
    p += r.levelJson;
    for (const std::vector<GameInput>& tr : r.traces) {
        putLE32(p, static_cast<std::uint32_t>(tr.size()));
        for (const GameInput& in : tr) {
            putLE32(p, floatBits(in.moveX));
            putLE32(p, floatBits(in.moveZ));
        }
    }
    return p;
}

/// Bounds-checked inverse of serializeReplay. Rejects any payload whose declared sizes
/// run past the buffer, so a truncated/tampered payload cannot over-read or over-alloc.
inline bool deserializeReplay(const std::string& p, Replay& out) {
    std::size_t i = 0;
    auto need = [&](std::size_t n) { return i + n <= p.size(); };
    if (!need(8)) return false;
    out.dt = bitsDouble(getLE64(p, i));
    if (!need(4)) return false;
    out.claimedWinner = static_cast<int>(getLE32(p, i));
    if (!need(4)) return false;
    const std::uint32_t nTraces = getLE32(p, i);
    if (!need(4)) return false;
    const std::uint32_t levelLen = getLE32(p, i);
    if (!need(levelLen)) return false;
    out.levelJson.assign(p, i, levelLen);
    i += levelLen;
    if (nTraces > 8) return false; // sanity cap (max supported players)
    out.traces.clear();
    for (std::uint32_t t = 0; t < nTraces; ++t) {
        if (!need(4)) return false;
        const std::uint32_t count = getLE32(p, i);
        if (!need(static_cast<std::size_t>(count) * 8)) return false;
        std::vector<GameInput> tr;
        tr.reserve(count);
        for (std::uint32_t k = 0; k < count; ++k) {
            GameInput in;
            in.moveX = bitsFloat(getLE32(p, i));
            in.moveZ = bitsFloat(getLE32(p, i));
            tr.push_back(in);
        }
        out.traces.push_back(std::move(tr));
    }
    return i == p.size(); // no trailing garbage
}

/// Content code over a replay payload (distinct namespace tag from ghost traces).
inline std::string replayCodeFor(const std::string& payload) {
    return "DR-" + base32Crockford(fnv1a64(payload), 13);
}

} // namespace detail

/// The frozen wire prefix for a replay share string.
inline const std::string& replaySharePrefix() {
    static const std::string p = "DDR1:";
    return p;
}

/// Encode a replay as a content-verified, self-contained share string.
inline std::string encodeReplay(const Replay& r) {
    const std::string payload = detail::serializeReplay(r);
    return replaySharePrefix() + shareCodeFor(r.levelJson) + ":" + detail::replayCodeFor(payload) + ":" +
           detail::base64Encode(payload);
}

/// Decode a replay share string. Returns false on a bad prefix/structure, invalid
/// base64, a payload that does not match its content code, or an embedded level whose
/// recomputed share code disagrees (tamper). On success @p out is byte-faithful.
inline bool decodeReplay(const std::string& share, Replay& out) {
    const std::string& prefix = replaySharePrefix();
    if (share.size() <= prefix.size() || share.compare(0, prefix.size(), prefix) != 0) return false;
    const std::size_t p1 = share.find(':', prefix.size());
    if (p1 == std::string::npos) return false;
    const std::size_t p2 = share.find(':', p1 + 1);
    if (p2 == std::string::npos) return false;

    const std::string levelCode = share.substr(prefix.size(), p1 - prefix.size());
    const std::string payloadCode = share.substr(p1 + 1, p2 - (p1 + 1));
    const std::string payloadB64 = share.substr(p2 + 1);

    std::string payload;
    if (!detail::base64Decode(payloadB64, payload)) return false;
    if (detail::replayCodeFor(payload) != payloadCode) return false; // integrity
    Replay r;
    if (!detail::deserializeReplay(payload, r)) return false;
    if (shareCodeFor(r.levelJson) != levelCode) return false; // level binding integrity
    out = std::move(r);
    return true;
}

/**
 * @brief Re-simulate a replay at its fixed dt, exposing each player's game per tick.
 *
 * One DungeonGame per trace, loaded from the embedded level. step() advances every
 * still-playing game by its recorded input for the current tick; a game that runs out
 * of inputs or ends just stops advancing. Deterministic, so the per-tick state stream
 * reproduces the original run/match exactly on any device.
 */
class ReplayPlayer {
public:
    explicit ReplayPlayer(const Replay& replay) : m_replay(replay) {
        LevelSpec spec;
        if (!fromLevelJson(replay.levelJson, spec) || replay.traces.empty()) return;
        const DungeonGame base = loadGame(convert(spec));
        m_games.assign(replay.traces.size(), base);
        for (const std::vector<GameInput>& tr : replay.traces)
            m_maxTicks = tr.size() > m_maxTicks ? tr.size() : m_maxTicks;
        m_valid = true;
    }

    bool valid() const { return m_valid; }
    int players() const { return static_cast<int>(m_games.size()); }
    std::size_t tick() const { return m_tick; }
    const DungeonGame& game(int player) const { return m_games[static_cast<std::size_t>(player)]; }

    /// True once every tick has been played (or the replay was invalid).
    bool finished() const { return !m_valid || m_tick >= m_maxTicks; }

    /// Advance one tick: apply each player's input for this tick. Returns false at end.
    bool step() {
        if (finished()) return false;
        for (std::size_t p = 0; p < m_games.size(); ++p) {
            const std::vector<GameInput>& tr = m_replay.traces[p];
            if (m_games[p].status == GameStatus::Playing && m_tick < tr.size())
                m_games[p].update(tr[m_tick], static_cast<float>(m_replay.dt));
        }
        ++m_tick;
        return true;
    }

    /// Run to the end.
    void playToEnd() { while (step()) {} }

private:
    Replay m_replay;
    std::vector<DungeonGame> m_games;
    std::size_t m_tick{0};
    std::size_t m_maxTicks{0};
    bool m_valid{false};
};

} // namespace game
} // namespace IKore
