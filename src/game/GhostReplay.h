#pragma once

#include "game/Leaderboard.h" // RunTrace, replayRun (and transitively DungeonGame, LevelShare, LevelFormat)

#include <cstdint>
#include <cstring>
#include <string>

/**
 * @file GhostReplay.h
 * @brief Shareable ghost replays for the doodle game (issue #272).
 *
 * #242 made a completed run a verifiable input trace (RunTrace + replayRun) and #241
 * established content-verified share strings (LevelShare.h). Sharing a trace lets a
 * friend race a ghost of your best run: the deterministic sim replays your inputs in
 * their own game alongside the live one, tick for tick.
 *
 *   - encodeGhost/decodeGhost: a compact, content-verified share string for a RunTrace,
 *     bound to the level's share code so a trace cannot be replayed against the wrong
 *     level. Format "DDG1:<levelCode>:<traceCode>:<base64url(payload)>": the payload is
 *     the trace's exact bytes (little-endian IEEE-754, portable across devices),
 *     traceCode is a content hash of the payload (tamper check), and levelCode binds it
 *     to a specific level. decodeGhost rejects a bad prefix, corrupted base64, a
 *     payload that does not match traceCode, or a level whose code does not match.
 *   - GhostPlayback: steps a decoded trace through its own DungeonGame, exposing the
 *     ghost's position per tick. Because the sim is deterministic, the ghost's position
 *     stream reproduces the original run exactly.
 *
 * Reuses the base64/base32/hash helpers and shareCodeFor from LevelShare.h - no new
 * serialization scheme beyond the trace byte packing. Pure std, header-only, no wall
 * clock.
 */
namespace IKore {
namespace game {
namespace detail {

inline void putLE32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline void putLE64(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline std::uint32_t getLE32(const std::string& s, std::size_t& i) {
    std::uint32_t v = 0;
    for (int b = 0; b < 4; ++b) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[i++])) << (8 * b);
    return v;
}
inline std::uint64_t getLE64(const std::string& s, std::size_t& i) {
    std::uint64_t v = 0;
    for (int b = 0; b < 8; ++b) v |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i++])) << (8 * b);
    return v;
}
inline std::uint32_t floatBits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
inline float bitsFloat(std::uint32_t u) {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
inline std::uint64_t doubleBits(double d) {
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}
inline double bitsDouble(std::uint64_t u) {
    double d = 0.0;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

/// Pack a trace to exact bytes: dt (double LE) + count (u32 LE) + per input (2 floats LE).
inline std::string serializeTrace(const RunTrace& t) {
    std::string p;
    p.reserve(12 + t.inputs.size() * 8);
    putLE64(p, doubleBits(t.dt));
    putLE32(p, static_cast<std::uint32_t>(t.inputs.size()));
    for (const GameInput& in : t.inputs) {
        putLE32(p, floatBits(in.moveX));
        putLE32(p, floatBits(in.moveZ));
    }
    return p;
}

/// Unpack a trace. Rejects a payload whose length does not exactly match its count, so
/// a truncated or tampered payload cannot drive an oversized allocation or a bad read.
inline bool deserializeTrace(const std::string& p, RunTrace& out) {
    if (p.size() < 12) return false;
    std::size_t i = 0;
    const double dt = bitsDouble(getLE64(p, i));
    const std::uint32_t n = getLE32(p, i);
    if (p.size() != static_cast<std::size_t>(12) + static_cast<std::size_t>(n) * 8) return false;
    out.dt = dt;
    out.inputs.clear();
    out.inputs.reserve(n);
    for (std::uint32_t k = 0; k < n; ++k) {
        GameInput g;
        g.moveX = bitsFloat(getLE32(p, i));
        g.moveZ = bitsFloat(getLE32(p, i));
        out.inputs.push_back(g);
    }
    return true;
}

/// Content code for a trace payload. "DG-" distinguishes it from a level's "DD-" code.
inline std::string traceCodeFor(const std::string& payload) {
    return "DG-" + base32Crockford(fnv1a64(payload), 13);
}

} // namespace detail

/// The wire prefix for a ghost share string (frozen, like LevelShare's "DDL1:").
inline const std::string& ghostSharePrefix() {
    static const std::string p = "DDG1:";
    return p;
}

/**
 * @brief Build a shareable ghost string for @p trace on the level @p levelJson.
 *
 * Embeds the level's content share code (so it can only be replayed against that level)
 * and a content hash of the trace payload (so tampering is detected on decode).
 */
inline std::string encodeGhost(const std::string& levelJson, const RunTrace& trace) {
    const std::string payload = detail::serializeTrace(trace);
    return ghostSharePrefix() + shareCodeFor(levelJson) + ":" + detail::traceCodeFor(payload) + ":" +
           detail::base64Encode(payload);
}

/// Result of decoding a ghost share string.
struct GhostImport {
    bool ok{false};
    std::string levelCode; ///< the level share code the ghost is bound to.
    std::string traceCode; ///< the trace's content code.
    RunTrace trace;        ///< the recovered trace (byte-identical when ok).
};

/// Peek the level code a ghost string is bound to, without verifying it (for routing to
/// the right level). Returns "" on a malformed prefix/format.
inline std::string peekGhostLevelCode(const std::string& share) {
    const std::string& prefix = ghostSharePrefix();
    if (share.size() <= prefix.size() || share.compare(0, prefix.size(), prefix) != 0) return "";
    const std::size_t sep = share.find(':', prefix.size());
    if (sep == std::string::npos) return "";
    return share.substr(prefix.size(), sep - prefix.size());
}

/**
 * @brief Decode a ghost string and verify it against @p expectedLevelJson.
 *
 * Fails (ok == false) on a wrong prefix, malformed structure, invalid base64, a payload
 * that does not match its embedded trace code (tamper), or a level whose share code does
 * not match the embedded level code (wrong level). On success the trace is byte-identical
 * to the encoded one.
 */
inline GhostImport decodeGhost(const std::string& share, const std::string& expectedLevelJson) {
    GhostImport r;
    const std::string& prefix = ghostSharePrefix();
    if (share.size() <= prefix.size() || share.compare(0, prefix.size(), prefix) != 0) return r;

    const std::size_t p1 = share.find(':', prefix.size());
    if (p1 == std::string::npos) return r;
    const std::size_t p2 = share.find(':', p1 + 1);
    if (p2 == std::string::npos) return r;

    const std::string levelCode = share.substr(prefix.size(), p1 - prefix.size());
    const std::string traceCode = share.substr(p1 + 1, p2 - (p1 + 1));
    const std::string payloadB64 = share.substr(p2 + 1);

    std::string payload;
    if (!detail::base64Decode(payloadB64, payload)) return r;
    if (detail::traceCodeFor(payload) != traceCode) return r;  // integrity: payload must match its code
    if (shareCodeFor(expectedLevelJson) != levelCode) return r; // bound to this specific level

    RunTrace trace;
    if (!detail::deserializeTrace(payload, trace)) return r;

    r.ok = true;
    r.levelCode = levelCode;
    r.traceCode = traceCode;
    r.trace = std::move(trace);
    return r;
}

/**
 * @brief Steps a recorded trace through its own DungeonGame so a ghost can be drawn
 *        alongside a live run, exposing the ghost's position per tick.
 *
 * The ghost loads the same level and applies the same inputs at the same dt as the
 * original run, so its position stream reproduces that run exactly (the sim is
 * deterministic). Advance it once per live tick with step().
 */
class GhostPlayback {
public:
    GhostPlayback(const std::string& levelJson, const RunTrace& trace) : m_trace(trace) {
        LevelSpec spec;
        if (fromLevelJson(levelJson, spec)) {
            m_game = loadGame(convert(spec));
            m_valid = true;
        }
    }

    bool valid() const { return m_valid; }
    std::size_t tick() const { return m_tick; }
    ecs::Vec3 position() const { return m_game.playerPosition; }
    const DungeonGame& game() const { return m_game; }

    /// No more inputs to play, the run ended, or the level failed to load.
    bool finished() const {
        return !m_valid || m_tick >= m_trace.inputs.size() || m_game.status != GameStatus::Playing;
    }

    /// Apply the next recorded input. Returns false (no-op) once finished().
    bool step() {
        if (finished()) return false;
        m_game.update(m_trace.inputs[m_tick], static_cast<float>(m_trace.dt));
        ++m_tick;
        return true;
    }

private:
    DungeonGame m_game;
    RunTrace m_trace;
    std::size_t m_tick{0};
    bool m_valid{false};
};

} // namespace game
} // namespace IKore
