#pragma once

#include "core/Settings.h" // Settings key/value store (#58)

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file Campaign.h
 * @brief Ordered campaign with unlocks and persistent progress (issue #347).
 *
 * Turns one-off levels into a structured single-player campaign: ordered worlds, each an
 * ordered set of levels, with unlock rules and a per-level best (stars + time). Levels
 * unlock sequentially within a world (clear one to reveal the next); a world unlocks once
 * enough levels of the previous world are cleared (its @c requiredToAdvance, or all levels
 * when 0). Progress persists through the Settings store (#58): save() writes each level's
 * best into a Settings object and load() restores it, so serialize()/loadFromFile() round
 * trips it across sessions.
 *
 * Star values are supplied by the caller (e.g. the star rating in #346), so this core has
 * no dependency on how stars are computed. Header-only, std only, deterministic.
 */
namespace IKore {
namespace game {

struct CampaignLevel {
    std::string id;        ///< stable key for persistence (auto-filled "w{w}l{l}" if empty).
    std::string name;      ///< display name.
    std::string levelJson; ///< optional level content (may be empty for structure-only).
};

struct CampaignWorld {
    std::string name;
    std::vector<CampaignLevel> levels;
    int requiredToAdvance{0}; ///< clears in THIS world needed to unlock the next (0 => all).
};

/// A level's map state for the world-map UI.
enum class LevelState { Locked, Unlocked, Completed };

/// Best result recorded for one level.
struct LevelProgress {
    bool completed{false};
    int bestStars{0};      ///< 0 = none earned yet.
    double bestTime{-1.0}; ///< < 0 = no cleared time yet.
};

class Campaign {
public:
    Campaign() = default;
    explicit Campaign(std::vector<CampaignWorld> worlds) : m_worlds(std::move(worlds)) {
        // Give every level a stable id and a progress slot in a deterministic order.
        for (std::size_t w = 0; w < m_worlds.size(); ++w) {
            for (std::size_t l = 0; l < m_worlds[w].levels.size(); ++l) {
                CampaignLevel& lv = m_worlds[w].levels[l];
                if (lv.id.empty())
                    lv.id = "w" + std::to_string(w) + "l" + std::to_string(l);
                m_progress.push_back(LevelProgress{});
                m_index.push_back(lv.id);
            }
        }
    }

    // --- Structure -----------------------------------------------------------------
    std::size_t worldCount() const { return m_worlds.size(); }
    std::size_t levelCount(std::size_t world) const {
        return world < m_worlds.size() ? m_worlds[world].levels.size() : 0;
    }
    const CampaignWorld& world(std::size_t w) const { return m_worlds.at(w); }
    const CampaignLevel& level(std::size_t w, std::size_t l) const { return m_worlds.at(w).levels.at(l); }

    // --- Recording -----------------------------------------------------------------
    /// Record a clear: marks the level completed and keeps the best stars (max) and time
    /// (min). Out-of-range indices are ignored.
    void recordClear(std::size_t world, std::size_t level, int stars, double time) {
        const int idx = flat(world, level);
        if (idx < 0) return;
        LevelProgress& p = m_progress[static_cast<std::size_t>(idx)];
        p.completed = true;
        if (stars > p.bestStars) p.bestStars = stars;
        if (time >= 0.0 && (p.bestTime < 0.0 || time < p.bestTime)) p.bestTime = time;
    }

    // --- Queries -------------------------------------------------------------------
    LevelProgress progressOf(std::size_t world, std::size_t level) const {
        const int idx = flat(world, level);
        return idx < 0 ? LevelProgress{} : m_progress[static_cast<std::size_t>(idx)];
    }
    bool levelCompleted(std::size_t world, std::size_t level) const {
        return progressOf(world, level).completed;
    }
    /// Number of completed levels in a world.
    int worldClears(std::size_t world) const {
        int n = 0;
        for (std::size_t l = 0; l < levelCount(world); ++l)
            if (levelCompleted(world, l)) ++n;
        return n;
    }
    /// A world is unlocked if it is the first, or the previous world has enough clears.
    bool worldUnlocked(std::size_t world) const {
        if (world >= m_worlds.size()) return false;
        if (world == 0) return true;
        if (!worldUnlocked(world - 1)) return false;
        const CampaignWorld& prev = m_worlds[world - 1];
        const int need = prev.requiredToAdvance > 0 ? prev.requiredToAdvance
                                                     : static_cast<int>(prev.levels.size());
        return worldClears(world - 1) >= need;
    }
    /// A level is unlocked if its world is unlocked and it is the first level or the
    /// previous level in the world is completed.
    bool levelUnlocked(std::size_t world, std::size_t level) const {
        if (level >= levelCount(world) || !worldUnlocked(world)) return false;
        if (level == 0) return true;
        return levelCompleted(world, level - 1);
    }
    LevelState stateOf(std::size_t world, std::size_t level) const {
        if (levelCompleted(world, level)) return LevelState::Completed;
        if (levelUnlocked(world, level)) return LevelState::Unlocked;
        return LevelState::Locked;
    }
    /// Total stars earned across the campaign.
    int totalStars() const {
        int n = 0;
        for (const LevelProgress& p : m_progress) n += p.bestStars;
        return n;
    }

    // --- Persistence via the Settings store (#58) ----------------------------------
    /// Write every level's best into @p s under "campaign.<id>.*" keys.
    void save(Settings& s) const {
        for (std::size_t i = 0; i < m_index.size(); ++i) {
            const std::string& id = m_index[i];
            const LevelProgress& p = m_progress[i];
            s.setBool(key(id, "done"), p.completed);
            s.setInt(key(id, "stars"), p.bestStars);
            s.setFloat(key(id, "time"), static_cast<float>(p.bestTime));
        }
    }
    /// Restore every level's best from @p s (missing keys leave a fresh, empty progress).
    void load(const Settings& s) {
        for (std::size_t i = 0; i < m_index.size(); ++i) {
            const std::string& id = m_index[i];
            LevelProgress p;
            p.completed = s.getBool(key(id, "done"), false);
            p.bestStars = s.getInt(key(id, "stars"), 0);
            p.bestTime = static_cast<double>(s.getFloat(key(id, "time"), -1.0f));
            m_progress[i] = p;
        }
    }
    /// Convenience: progress as the Settings text form (for a full save-file round trip).
    std::string serializeProgress() const {
        Settings s;
        save(s);
        return s.serialize();
    }
    void loadProgress(const std::string& text) {
        Settings s;
        s.load(text);
        load(s);
    }

private:
    int flat(std::size_t world, std::size_t level) const {
        if (world >= m_worlds.size() || level >= m_worlds[world].levels.size()) return -1;
        int idx = 0;
        for (std::size_t w = 0; w < world; ++w) idx += static_cast<int>(m_worlds[w].levels.size());
        return idx + static_cast<int>(level);
    }
    static std::string key(const std::string& id, const char* field) {
        return "campaign." + id + "." + field;
    }

    std::vector<CampaignWorld> m_worlds;
    std::vector<LevelProgress> m_progress; ///< flat, parallel to m_index (deterministic order).
    std::vector<std::string> m_index;      ///< flat level ids, world-major.
};

} // namespace game
} // namespace IKore
