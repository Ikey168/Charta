#pragma once

#include "game/Fairness.h"       // checkLevelFairness, FairnessResult (#334)
#include "game/Leaderboard.h"    // Leaderboard, RunTrace, ScoreEntry, SubmitResult, replay verification
#include "game/LevelCatalog.h"   // LevelCatalog, LevelEntry, weeklyPrompt
#include "game/LevelShare.h"     // shareCodeFor (content code that keys a board)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file WeeklyChallenge.h
 * @brief Weekly challenge rotation with its own leaderboard (issue #273).
 *
 * Closes the weekly retention loop the concept doc describes by combining three
 * existing cores: the UGC catalog (#174), content share codes (#241), and the
 * replay-verified leaderboards (#242). Each week has a rotating drawing prompt and a
 * featured level chosen deterministically from the catalog, plus its own competition
 * board; runs are verified exactly as in #242 (this reuses Leaderboard unchanged).
 *
 *   - weekIndexAt(): the week number for a caller-supplied timestamp (no wall clock),
 *     with week bounds and time-remaining helpers.
 *   - selectFeatured(): the week's level, picked from the catalog by a total order
 *     (most stars, then newest, then code) and rotated by week index, so every
 *     instance with the same catalog agrees on the week's level.
 *   - current(): the query API - prompt + featured level + time remaining.
 *   - submit()/standings(): a per-week board keyed by (week index, level code). A new
 *     week starts a fresh board; prior weeks stay queryable.
 *
 * Deterministic: same catalog + timestamps + traces yield the same selection and
 * standings on any instance. Header-only, std only, no wall clock.
 */
namespace IKore {
namespace game {

/// Seconds in a week (7 * 24 * 3600); the default rotation period.
inline constexpr std::int64_t kSecondsPerWeek = 604800;

/// The current challenge: which week, its prompt, the featured level, and timing.
struct WeeklyChallengeInfo {
    bool valid{false};       ///< false when the catalog has no level to feature.
    int weekIndex{0};
    std::string prompt;
    LevelEntry level;        ///< the featured level for the week.
    std::int64_t weekStart{0};   ///< inclusive start timestamp.
    std::int64_t weekEnd{0};     ///< exclusive end timestamp.
    std::int64_t timeRemaining{0}; ///< weekEnd - now (seconds left in the week).
};

class WeeklyChallenge {
public:
    /**
     * @param secondsPerWeek Rotation period.
     * @param epoch          Timestamp of week 0's start (the rotation anchor).
     * @param fixedDt        Fixed timestep the per-week boards verify against (#242).
     */
    explicit WeeklyChallenge(std::int64_t secondsPerWeek = kSecondsPerWeek, std::int64_t epoch = 0,
                             double fixedDt = 1.0 / 60.0)
        : m_secondsPerWeek(secondsPerWeek > 0 ? secondsPerWeek : kSecondsPerWeek),
          m_epoch(epoch), m_fixedDt(fixedDt) {}

    /// Week index for @p now (floored; can be negative before the epoch).
    int weekIndexAt(std::int64_t now) const {
        const std::int64_t rel = now - m_epoch;
        // Floor division toward negative infinity so pre-epoch times index correctly.
        std::int64_t q = rel / m_secondsPerWeek;
        if (rel % m_secondsPerWeek != 0 && ((rel < 0) != (m_secondsPerWeek < 0))) --q;
        return static_cast<int>(q);
    }

    std::int64_t weekStart(int weekIndex) const {
        return m_epoch + static_cast<std::int64_t>(weekIndex) * m_secondsPerWeek;
    }
    std::int64_t weekEnd(int weekIndex) const { return weekStart(weekIndex) + m_secondsPerWeek; }
    std::int64_t timeRemaining(std::int64_t now) const { return weekEnd(weekIndexAt(now)) - now; }
    double fixedDt() const { return m_fixedDt; }

    /**
     * @brief Pick the featured level for @p weekIndex from @p catalog deterministically.
     *
     * Orders all catalog entries by a total order - most stars, then newest, then code
     * ascending (so ties never depend on sort stability) - and rotates the pick by the
     * week index. False (out untouched) if the catalog is empty.
     */
    static bool selectFeatured(const LevelCatalog& catalog, int weekIndex, LevelEntry& out) {
        const std::size_t n = catalog.size();
        if (n == 0) return false;
        std::vector<LevelEntry> all = catalog.browseNew(n); // all entries, any order
        std::sort(all.begin(), all.end(), featuredLess);
        int i = weekIndex % static_cast<int>(all.size());
        if (i < 0) i += static_cast<int>(all.size());
        out = all[static_cast<std::size_t>(i)];
        return true;
    }

    /// The current challenge for @p now against @p catalog (query API).
    WeeklyChallengeInfo current(const LevelCatalog& catalog, std::int64_t now) const {
        WeeklyChallengeInfo info;
        info.weekIndex = weekIndexAt(now);
        info.prompt = LevelCatalog::weeklyPrompt(info.weekIndex);
        info.weekStart = weekStart(info.weekIndex);
        info.weekEnd = weekEnd(info.weekIndex);
        info.timeRemaining = info.weekEnd - now;
        info.valid = selectFeatured(catalog, info.weekIndex, info.level);
        return info;
    }

    /// The content code that keys a week's board (the featured level's share code).
    static std::string challengeLevelCode(const LevelEntry& level) {
        return shareCodeFor(level.levelJson);
    }

    /// Solver-backed fairness of the week-of-@p now's featured level (#334), so a caller
    /// can gate selection on a clearable level. "no featured level" when the catalog is
    /// empty.
    FairnessResult featuredFairness(const LevelCatalog& catalog, std::int64_t now) const {
        const WeeklyChallengeInfo info = current(catalog, now);
        if (!info.valid) {
            FairnessResult r;
            r.reason = "no featured level";
            return r;
        }
        return checkLevelFairness(info.level.levelJson);
    }

    /**
     * @brief Submit a run for the week-of-@p now's featured level.
     *
     * Registers the featured level in that week's board (idempotent) and delegates to
     * Leaderboard::submit, so the run is replay-verified exactly as in #242: a run that
     * does not clear, uses the wrong timestep, or desyncs is rejected and never ranked.
     * Rejected (or no featured level) yields an unaccepted SubmitResult.
     */
    SubmitResult submit(const LevelCatalog& catalog, std::int64_t now, const std::string& player,
                        const RunTrace& trace, std::int64_t submittedAt, double claimedTime = -1.0) {
        SubmitResult res;
        const WeeklyChallengeInfo info = current(catalog, now);
        if (!info.valid) {
            res.reason = "no featured level";
            return res;
        }
        Leaderboard& board = m_boards.emplace(info.weekIndex, Leaderboard(m_fixedDt)).first->second;
        const std::string code = board.registerLevel(info.level.levelJson); // idempotent (content code)
        m_weekCode[info.weekIndex] = code;
        return board.submit(code, player, trace, submittedAt, claimedTime);
    }

    /// Standings for a week (current or past). Empty for a week with no submissions.
    std::vector<ScoreEntry> standings(int weekIndex, std::size_t limit = 50) const {
        auto bit = m_boards.find(weekIndex);
        auto cit = m_weekCode.find(weekIndex);
        if (bit == m_boards.end() || cit == m_weekCode.end()) return {};
        return bit->second.top(cit->second, limit);
    }

    /// Standings for the week containing @p now.
    std::vector<ScoreEntry> currentStandings(std::int64_t now, std::size_t limit = 50) const {
        return standings(weekIndexAt(now), limit);
    }

    /// Whether a board exists for @p weekIndex (i.e. it has had at least one submission).
    bool hasBoard(int weekIndex) const { return m_boards.count(weekIndex) > 0; }

private:
    // "Better" (featured-first) total order: most stars, then newest, then code asc.
    static bool featuredLess(const LevelEntry& a, const LevelEntry& b) {
        if (a.stars != b.stars) return a.stars > b.stars;
        if (a.publishedAt != b.publishedAt) return a.publishedAt > b.publishedAt;
        return a.code < b.code;
    }

    std::int64_t m_secondsPerWeek;
    std::int64_t m_epoch;
    double m_fixedDt;
    std::unordered_map<int, Leaderboard> m_boards;   ///< per-week competition boards.
    std::unordered_map<int, std::string> m_weekCode; ///< week index -> featured level code.
};

} // namespace game
} // namespace IKore
