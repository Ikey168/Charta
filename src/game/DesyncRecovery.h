#pragma once

#include "game/DoodleRollback.h" // DoodleNetState, doodleStep, stateDigest
#include "ui/HudFramework.h"     // Hud, hudBar, hudValue, hudText

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file DesyncRecovery.h
 * @brief Desync recovery + a connection-quality HUD (issue #360).
 *
 * Desync detection already exists (stateDigest, #337); this acts on it and surfaces link
 * health. resyncFrom() recovers a diverged peer by rolling its state back to the last agreed
 * snapshot and re-simulating the confirmed inputs since - deterministic, so both peers
 * reconverge to an identical digest. ConnectionQuality samples the signals a session sees
 * (input delay, dropped/late frames, last rollback depth, resync count) into a 0..1 score
 * and a good/fair/poor level; shouldPause() is the graceful-degradation policy when quality
 * craters. buildQualityHud() lays those readouts (a quality bar, the input delay, and a
 * resync indicator) onto the HUD framework (#55). Header-only, deterministic.
 */
namespace IKore {
namespace game {

struct ConnectionQuality {
    int inputDelayFrames{0};
    int droppedFrames{0};
    int lateFrames{0};
    int lastRollbackDepth{0};
    int resyncs{0};

    /// A 0..1 link-quality score (1 = perfect), penalizing each signal.
    float score() const {
        float s = 1.0f;
        s -= 0.03f * static_cast<float>(inputDelayFrames);
        s -= 0.05f * static_cast<float>(droppedFrames);
        s -= 0.02f * static_cast<float>(lateFrames);
        s -= 0.02f * static_cast<float>(lastRollbackDepth);
        if (s < 0.0f) s = 0.0f;
        if (s > 1.0f) s = 1.0f;
        return s;
    }
    const char* level() const {
        const float s = score();
        return s >= 0.75f ? "good" : (s >= 0.4f ? "fair" : "poor");
    }
};

/// Graceful-degradation policy: pause the session when quality falls below @p threshold.
inline bool shouldPause(const ConnectionQuality& q, float threshold = 0.3f) {
    return q.score() < threshold;
}

/// True if two states have diverged (digest mismatch).
inline bool diverged(const DoodleNetState& a, const DoodleNetState& b) {
    return stateDigest(a) != stateDigest(b);
}

struct ResyncResult {
    bool recovered{false};
    int rollbackDepth{0};    ///< confirmed frames re-simulated from the snapshot.
    std::uint64_t digest{0}; ///< the recovered state's digest.
};

/**
 * @brief Recover a diverged @p local: adopt the authoritative snapshot (roll back to the
 *        last agreed state) and re-simulate the confirmed inputs since it (one per-player
 *        input vector per frame). Deterministic, so a peer reconverges to the authoritative
 *        timeline. Records the rollback depth / resync count into @p q if given.
 */
inline ResyncResult resyncFrom(DoodleNetState& local, const DoodleNetState& authoritative,
                               const std::vector<std::vector<GameInput>>& pendingInputs, float dt,
                               ConnectionQuality* q = nullptr) {
    local = authoritative; // roll back to the agreed snapshot
    int frame = 0;
    for (const std::vector<GameInput>& in : pendingInputs) doodleStep(local, in, frame++, dt);
    ResyncResult r;
    r.recovered = true;
    r.rollbackDepth = static_cast<int>(pendingInputs.size());
    r.digest = stateDigest(local);
    if (q) {
        q->lastRollbackDepth = r.rollbackDepth;
        ++q->resyncs;
    }
    return r;
}

/// A connection-quality HUD: a quality bar, the input delay, and a status/resync indicator.
inline Hud buildQualityHud(const ConnectionQuality& q, bool resyncing = false) {
    Hud hud;
    hud.add(hudBar("net_quality", HudAnchor::TopRight, HudVec2{8.0f, 8.0f}, HudVec2{120.0f, 10.0f},
                   [&q]() { return q.score(); }));
    hud.add(hudValue("net_delay", HudAnchor::TopRight, HudVec2{8.0f, 24.0f}, HudVec2{120.0f, 14.0f},
                     [&q]() { return q.inputDelayFrames; }));
    hud.add(hudText("net_status", HudAnchor::TopRight, HudVec2{8.0f, 42.0f}, HudVec2{120.0f, 14.0f},
                    [&q, resyncing]() { return std::string(resyncing ? "RESYNC" : q.level()); }));
    return hud;
}

} // namespace game
} // namespace IKore
