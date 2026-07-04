#pragma once

#include "game/Versus.h" // MatchRecord, VersusResult, verifyMatch

#include <cstddef>
#include <vector>

/**
 * @file Tournament.h
 * @brief Single-elimination bracket over verified match records (issue #358).
 *
 * Ties the versus stack together into a tournament: seed N players, run rounds, advance
 * winners, crown a champion - with every advancement backed by a replay-verified MatchRecord
 * (#293), so a forged result cannot move a player forward. Each round pairs the surviving
 * players in bracket order; an odd survivor takes a bye to the next round (so non-power-of-two
 * fields are handled). reportMatch() resolves the next pending pairing from a record whose
 * inputs0/inputs1 are that pairing's A/B: verifyMatch() must reproduce the claimed winner or
 * the report is rejected. Deterministic (same records rebuild the same bracket), header-only.
 */
namespace IKore {
namespace game {

struct BracketMatch {
    int round{0};
    int playerA{-1};
    int playerB{-1};
    int winner{-1}; ///< resolved winner (a seed index), or -1 while pending.
};

class Tournament {
public:
    explicit Tournament(int numPlayers) {
        if (numPlayers < 1) numPlayers = 1;
        for (int i = 0; i < numPlayers; ++i) m_current.push_back(i);
        startRound();
    }

    bool complete() const { return m_current.size() <= 1; }
    /// The champion's seed index once complete, else -1.
    int champion() const { return complete() && !m_current.empty() ? m_current.front() : -1; }
    int round() const { return m_round; }

    /// Real (non-bye) matches still to play in the current round.
    int pendingMatches() const { return static_cast<int>(m_matches.size()) - m_nextIdx; }
    /// The next pairing to resolve (A vs B); playerA == -1 if none is pending.
    BracketMatch nextMatch() const {
        if (m_nextIdx >= static_cast<int>(m_matches.size())) return BracketMatch{};
        return m_matches[static_cast<std::size_t>(m_nextIdx)];
    }
    /// Every resolved match across all rounds (for standings / summary).
    const std::vector<BracketMatch>& history() const { return m_history; }

    /**
     * @brief Resolve the next pending pairing from a verified record. The record's
     *        inputs0/inputs1 are player A/B of that pairing; the replayed winner advances.
     *        Returns false (no change) if the tournament is done, no pairing is pending, or
     *        the record does not verify to its claimed winner.
     */
    bool reportMatch(const MatchRecord& rec) {
        if (complete()) return false;
        if (m_nextIdx >= static_cast<int>(m_matches.size())) return false;
        VersusResult vr;
        if (!verifyMatch(rec, vr)) return false; // anti-cheat: must reproduce its claim

        BracketMatch& m = m_matches[static_cast<std::size_t>(m_nextIdx)];
        m.winner = (vr.winner == 0) ? m.playerA : m.playerB;
        m_advancers.push_back(m.winner);
        m_history.push_back(m);
        ++m_nextIdx;

        if (m_nextIdx == static_cast<int>(m_matches.size())) {
            if (m_byeAdvancer >= 0) m_advancers.push_back(m_byeAdvancer);
            m_current = m_advancers;
            ++m_round;
            if (m_current.size() > 1) startRound();
        }
        return true;
    }

private:
    void startRound() {
        m_matches.clear();
        m_advancers.clear();
        m_nextIdx = 0;
        m_byeAdvancer = -1;
        for (std::size_t i = 0; i + 1 < m_current.size(); i += 2)
            m_matches.push_back(BracketMatch{m_round, m_current[i], m_current[i + 1], -1});
        if (m_current.size() % 2 == 1) m_byeAdvancer = m_current.back(); // odd survivor byes
    }

    int m_round{0};
    std::vector<int> m_current;    ///< players still in, this round (bracket order).
    std::vector<BracketMatch> m_matches; ///< this round's real pairings.
    std::vector<int> m_advancers;  ///< winners (+ bye) collected this round, in bracket order.
    std::vector<BracketMatch> m_history;
    int m_nextIdx{0};
    int m_byeAdvancer{-1};
};

} // namespace game
} // namespace IKore
