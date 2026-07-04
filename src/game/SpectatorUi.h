#pragma once

#include "game/Replay.h"    // Replay, ReplayPlayer
#include "game/VersusFFA.h" // VersusFFA, VersusStanding
#include "ui/HudFramework.h" // Hud, hudList

#include <algorithm>
#include <string>
#include <vector>

/**
 * @file SpectatorUi.h
 * @brief FFA standings HUD and a spectator replay scrubber (issue #335).
 *
 * N-player FFA (#318) exposes per-tick standings and shareable replays (#317) expose a
 * ReplayPlayer, but neither was surfaced for the UI. This adds both on top of the
 * header-only HUD framework (#55), so the widget/scrubber state is decoupled from GL and
 * unit-testable headless:
 *   - ffaStandingsLines / buildFfaHud: a live standings list widget bound to
 *     VersusFFA::standings(), refreshed per tick.
 *   - ReplayScrubber: play/pause and seek-to-tick over a Replay. Because playback is
 *     deterministic, seeking re-simulates from the start to the target tick, exposing each
 *     player's game at that tick.
 *
 * Header-only, std only.
 */
namespace IKore {
namespace game {

// --- FFA standings HUD -------------------------------------------------------

/// One standings line, e.g. "1. P0  coins 2  WON".
inline std::string formatStanding(int rank, const VersusStanding& s) {
    std::string line = std::to_string(rank) + ". P" + std::to_string(s.player) + "  coins " +
                       std::to_string(s.coinsCollected);
    if (s.won) {
        line += "  WON";
    } else if (s.lost) {
        line += "  OUT";
    }
    return line;
}

/// The live standings as ranked display lines (best first).
inline std::vector<std::string> ffaStandingsLines(const VersusFFA& match) {
    std::vector<std::string> lines;
    const std::vector<VersusStanding> standings = match.standings();
    lines.reserve(standings.size());
    for (std::size_t i = 0; i < standings.size(); ++i) {
        lines.push_back(formatStanding(static_cast<int>(i) + 1, standings[i]));
    }
    return lines;
}

/// Build a HUD with a top-right standings list bound to the live match. The returned Hud
/// captures @p match by reference, so it must not outlive the match.
inline Hud buildFfaHud(const VersusFFA& match) {
    Hud hud;
    const float rows = static_cast<float>(match.players() > 0 ? match.players() : 1);
    hud.add(hudList("ffa_standings", HudAnchor::TopRight, HudVec2{8.0f, 8.0f},
                    HudVec2{220.0f, 18.0f * rows}, [&match]() { return ffaStandingsLines(match); }));
    return hud;
}

// --- Spectator replay scrubber ----------------------------------------------

/**
 * @brief Play/pause and seek over a Replay (#317), exposing each player's game at the
 *        current tick. Seeking re-simulates deterministically from the start to the target
 *        tick, so any tick is reproducible.
 */
class ReplayScrubber {
public:
    explicit ReplayScrubber(Replay replay) : m_replay(std::move(replay)), m_player(m_replay) {
        for (const std::vector<GameInput>& tr : m_replay.traces) {
            m_length = std::max(m_length, tr.size());
        }
        m_valid = m_player.valid();
    }

    bool valid() const { return m_valid; }
    int players() const { return m_player.players(); }
    std::size_t tick() const { return m_tick; }
    std::size_t length() const { return m_length; } ///< total number of ticks.
    bool playing() const { return m_playing; }
    bool atEnd() const { return m_tick >= m_length; }

    void play() { m_playing = true; }
    void pause() { m_playing = false; }
    void togglePlay() { m_playing = !m_playing; }

    /// Jump to @p tick (clamped to [0, length]), re-simulating from the start.
    void seek(std::size_t tick) {
        if (tick > m_length) tick = m_length;
        m_player = ReplayPlayer(m_replay); // fresh, deterministic re-sim
        for (std::size_t i = 0; i < tick; ++i) m_player.step();
        m_tick = tick;
    }

    /// Advance one tick if playing (and not at the end). Returns whether it advanced.
    bool advance() {
        if (!m_playing || m_tick >= m_length) return false;
        seek(m_tick + 1);
        return true;
    }

    /// The player's game state at the current tick.
    const DungeonGame& game(int player) const { return m_player.game(player); }

private:
    Replay m_replay;
    std::size_t m_length{0};
    std::size_t m_tick{0};
    bool m_playing{false};
    bool m_valid{false};
    ReplayPlayer m_player;
};

} // namespace game
} // namespace IKore
