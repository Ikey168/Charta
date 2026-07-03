#pragma once

#include "game/GhostReplay.h"  // GhostPlayback, decodeGhost, RunTrace
#include "game/DungeonGame.h"  // DungeonGame, GameInput, loadGame, GameStatus
#include "game/DoodleScene.h"  // LevelSpec, convert
#include "game/LevelFormat.h"  // fromLevelJson

#include <cmath>
#include <string>

/**
 * @file GhostRace.h
 * @brief Race a live run against a shared ghost, tick for tick (issue #292).
 *
 * #272 made a completed run a shareable, content-verified ghost (GhostReplay). This
 * steps a live DungeonGame and a decoded GhostPlayback in lockstep at the fixed dt, so
 * a player races a friend's best asynchronously: each tick exposes who is ahead, by how
 * much, and the finish order. Because the sim is deterministic, the ghost reproduces the
 * original run exactly, so the race is reproducible and verifiable.
 *
 * The race is bound to the level: constructing from a share string rejects a wrong-level
 * or tampered ghost (via decodeGhost). Header-only, std only, no wall clock.
 */
namespace IKore {
namespace game {

/// A racer's standing at the current tick.
struct RaceStanding {
    float progress{0.0f}; ///< higher is further along (coins collected, then nearer the exit).
    bool finished{false}; ///< the racer's game ended (won or lost).
    bool won{false};
    int finishTick{-1};   ///< tick at which the racer won (-1 if not yet).
};

/// A live game racing a ghost of a previous run over the same level.
class GhostRace {
public:
    /// Race @p ghostTrace on @p levelJson. valid() is false if the level fails to load.
    GhostRace(const std::string& levelJson, const RunTrace& ghostTrace, float dt)
        : m_ghost(levelJson, ghostTrace), m_dt(dt) {
        LevelSpec spec;
        if (fromLevelJson(levelJson, spec) && m_ghost.valid()) {
            m_live = loadGame(convert(spec));
            m_valid = true;
        }
    }

    /**
     * @brief Build a race from a ghost share string, verified against @p levelJson.
     *        valid() is false if the string is a wrong prefix, tampered, or a wrong
     *        level (decodeGhost rejects it).
     */
    static GhostRace fromShare(const std::string& levelJson, const std::string& share, float dt) {
        const GhostImport imp = decodeGhost(share, levelJson);
        if (!imp.ok) return GhostRace(); // invalid
        return GhostRace(levelJson, imp.trace, dt);
    }

    bool valid() const { return m_valid; }
    int tick() const { return m_tick; }

    /// True once the race is over: either racer won, or both games ended, or the ghost ran out.
    bool finished() const {
        if (!m_valid) return true;
        if (m_live.won() || m_ghost.game().won()) return true;
        const bool liveDone = m_live.status != GameStatus::Playing;
        const bool ghostDone = m_ghost.finished();
        return liveDone && ghostDone;
    }

    /// Advance the live game by @p liveInput and the ghost by one tick. False if finished.
    bool step(const GameInput& liveInput) {
        if (finished()) return false;
        if (m_live.status == GameStatus::Playing) {
            m_live.update(liveInput, m_dt);
            if (m_live.won() && m_liveFinishTick < 0) m_liveFinishTick = m_tick + 1;
        }
        m_ghost.step();
        if (m_ghost.game().won() && m_ghostFinishTick < 0) m_ghostFinishTick = m_tick + 1;
        ++m_tick;
        return true;
    }

    ecs::Vec3 livePosition() const { return m_live.playerPosition; }
    ecs::Vec3 ghostPosition() const { return m_ghost.position(); }

    RaceStanding liveStanding() const { return standing(m_live, m_liveFinishTick); }
    RaceStanding ghostStanding() const { return standing(m_ghost.game(), m_ghostFinishTick); }

    /// Live progress minus ghost progress: > 0 means the live run is ahead this tick.
    float lead() const { return liveStanding().progress - ghostStanding().progress; }

    /// Who is ahead this tick: 1 live, -1 ghost, 0 tie.
    int leader() const {
        const float d = lead();
        if (d > 1e-4f) return 1;
        if (d < -1e-4f) return -1;
        return 0;
    }

    /// Race winner once finished(): 1 live, -1 ghost, 0 tie/undecided. The earlier
    /// finish tick wins; if neither finished, the higher progress wins.
    int winner() const {
        const bool lw = m_liveFinishTick >= 0, gw = m_ghostFinishTick >= 0;
        if (lw && gw) return m_liveFinishTick < m_ghostFinishTick ? 1
                            : (m_ghostFinishTick < m_liveFinishTick ? -1 : 0);
        if (lw) return 1;
        if (gw) return -1;
        return leader();
    }

private:
    GhostRace() : m_ghost("", RunTrace{}), m_dt(1.0f / 60.0f) {}

    static RaceStanding standing(const DungeonGame& g, int finishTick) {
        RaceStanding s;
        s.finished = g.status != GameStatus::Playing;
        s.won = g.won();
        s.finishTick = finishTick;
        // Progress: each collected coin dominates, then nearness to the exit.
        float exitDist = 0.0f;
        if (g.hasExit) {
            const float dx = g.exitPosition.x - g.playerPosition.x;
            const float dz = g.exitPosition.z - g.playerPosition.z;
            exitDist = std::sqrt(dx * dx + dz * dz);
        }
        s.progress = static_cast<float>(g.coinsCollected) * 1000.0f - exitDist + (g.won() ? 1e6f : 0.0f);
        return s;
    }

    DungeonGame m_live;
    GhostPlayback m_ghost;
    float m_dt;
    bool m_valid{false};
    int m_tick{0};
    int m_liveFinishTick{-1};
    int m_ghostFinishTick{-1};
};

} // namespace game
} // namespace IKore
