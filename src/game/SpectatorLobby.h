#pragma once

#include "game/Replay.h"      // Replay, decodeReplay
#include "game/SpectatorUi.h" // ReplayScrubber, VersusFFA, ffaStandingsLines

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file SpectatorLobby.h
 * @brief The front door to watching: list live/finished sessions and route in (issue #359).
 *
 * The watch primitives exist in isolation - SpectatorUi (#335) renders live standings and a
 * ReplayScrubber, and Replay (#317) round-trips a match to a shareable code. This is the
 * lobby that ties them together: it lists joinable live sessions and finished-match replays
 * with metadata in a deterministic order (live first, then most recent, then id), lets a
 * spectator read a live session's standings (driving SpectatorUi), opens a finished entry's
 * replay into a scrubber, and decodes a pasted share code straight into a scrubber (an
 * invalid code yields an invalid scrubber, reported cleanly). Header-only, deterministic.
 */
namespace IKore {
namespace game {

struct LobbyEntry {
    std::string id;
    std::string title;
    bool live{false};
    int players{0};
    std::int64_t updatedAt{0}; ///< caller-supplied timestamp (no wall clock here).
    std::string replayCode;    ///< finished entries carry a Replay share code (#317).
};

class SpectatorLobby {
public:
    /// Add a joinable live session. @p match (optional) binds a live VersusFFA so a spectator
    /// can read its standings; it must outlive the lobby.
    void addLive(const std::string& id, const std::string& title, int players,
                 std::int64_t updatedAt, const VersusFFA* match = nullptr) {
        m_entries.push_back(LobbyEntry{id, title, true, players, updatedAt, std::string()});
        m_live.push_back(match);
    }
    /// Add a finished match watchable from its Replay share code.
    void addFinished(const std::string& id, const std::string& title, int players,
                     std::int64_t updatedAt, const std::string& replayCode) {
        m_entries.push_back(LobbyEntry{id, title, false, players, updatedAt, replayCode});
        m_live.push_back(nullptr);
    }

    std::size_t size() const { return m_entries.size(); }

    /// The lobby listing: live sessions first, then most-recently-updated, then id ascending.
    std::vector<LobbyEntry> list() const {
        std::vector<LobbyEntry> out = m_entries;
        std::sort(out.begin(), out.end(), [](const LobbyEntry& a, const LobbyEntry& b) {
            if (a.live != b.live) return a.live > b.live;          // live first
            if (a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt; // newest first
            return a.id < b.id;                                    // stable tie-break
        });
        return out;
    }

    /// Read a live session's standings (drives SpectatorUi). False if not a bound live entry.
    bool liveStandings(const std::string& id, std::vector<std::string>& out) const {
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].id != id) continue;
            if (!m_entries[i].live || m_live[i] == nullptr) return false;
            out = ffaStandingsLines(*m_live[i]);
            return true;
        }
        return false;
    }

    /// Open a finished entry's replay into a scrubber (invalid scrubber if unknown/undecodable).
    ReplayScrubber openReplay(const std::string& id) const {
        for (const LobbyEntry& e : m_entries) {
            if (e.id == id && !e.live) return openCode(e.replayCode);
        }
        return ReplayScrubber(Replay{});
    }

    /// Paste-a-code: decode a Replay share code straight into a scrubber. Check valid().
    static ReplayScrubber openCode(const std::string& code) {
        Replay r;
        if (decodeReplay(code, r)) return ReplayScrubber(std::move(r));
        return ReplayScrubber(Replay{});
    }

private:
    std::vector<LobbyEntry> m_entries;
    std::vector<const VersusFFA*> m_live; ///< parallel to m_entries (nullptr unless a bound live).
};

} // namespace game
} // namespace IKore
