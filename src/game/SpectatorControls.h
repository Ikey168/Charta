#pragma once

#include "game/SpectatorUi.h" // ReplayScrubber, buildFfaHud, ffaStandingsLines
#include "ui/HudFramework.h"  // Hud, HudElement, hudBar, hudText, HudWidget

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file SpectatorControls.h
 * @brief Wire the replay scrubber to input and resolve the spectator HUD for drawing (#352).
 *
 * SpectatorUi (#335) supplies the FFA standings HUD and a ReplayScrubber as header-only
 * state on the HUD framework (#55). This closes the live-engine gap: a small command layer
 * maps input (play/pause, step, seek) onto the scrubber, a scrubber HUD (progress bar +
 * label) is built on the same framework, and resolveHud() turns a bound Hud into a flat list
 * of positioned draw items so the GL pass can draw every element each frame from its live
 * source. All renderer-agnostic and headlessly testable; the gl-labelled tool does the draw.
 */
namespace IKore {
namespace game {

// --- Replay scrubber controls (input -> scrubber) ----------------------------

/// A control action for the replay scrubber, e.g. from a key or UI button.
enum class ScrubCommand { None, TogglePlay, Play, Pause, StepBack, StepForward, SeekFraction };

/// Apply a control command to @p sc. Stepping/seeking pauses playback (manual scrub), and
/// SeekFraction uses @p arg in [0,1] as a position along the timeline.
inline void applyScrub(ReplayScrubber& sc, ScrubCommand cmd, float arg = 0.0f) {
    switch (cmd) {
        case ScrubCommand::TogglePlay: sc.togglePlay(); break;
        case ScrubCommand::Play: sc.play(); break;
        case ScrubCommand::Pause: sc.pause(); break;
        case ScrubCommand::StepBack:
            sc.pause();
            if (sc.tick() > 0) sc.seek(sc.tick() - 1);
            break;
        case ScrubCommand::StepForward:
            sc.pause();
            sc.seek(sc.tick() + 1);
            break;
        case ScrubCommand::SeekFraction: {
            float f = arg < 0.0f ? 0.0f : (arg > 1.0f ? 1.0f : arg);
            sc.pause();
            sc.seek(static_cast<std::size_t>(f * static_cast<float>(sc.length()) + 0.5f));
            break;
        }
        case ScrubCommand::None: break;
    }
}

// --- Scrubber HUD ------------------------------------------------------------

/// Timeline progress in [0,1] (0 when empty).
inline float scrubProgress(const ReplayScrubber& sc) {
    return sc.length() ? static_cast<float>(sc.tick()) / static_cast<float>(sc.length()) : 0.0f;
}

/// A compact scrubber status label, e.g. "PLAY 42/120".
inline std::string scrubLabel(const ReplayScrubber& sc) {
    return (sc.playing() ? "PLAY " : "PAUSE ") + std::to_string(sc.tick()) + "/" +
           std::to_string(sc.length());
}

/// A bottom-center HUD: a progress bar bound to the scrubber plus a status label. Captures
/// @p sc by reference, so it must not outlive the scrubber.
inline Hud buildScrubberHud(const ReplayScrubber& sc) {
    Hud hud;
    hud.add(hudBar("scrub_bar", HudAnchor::BottomCenter, HudVec2{0.0f, 24.0f}, HudVec2{400.0f, 12.0f},
                   [&sc]() { return scrubProgress(sc); }));
    hud.add(hudText("scrub_label", HudAnchor::BottomCenter, HudVec2{0.0f, 44.0f}, HudVec2{400.0f, 16.0f},
                    [&sc]() { return scrubLabel(sc); }));
    return hud;
}

// --- Draw list resolution ----------------------------------------------------

/// A resolved, positioned HUD element ready to draw (renderer-agnostic).
struct HudDrawItem {
    std::string name;
    float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f}; ///< pixel rect (top-left origin).
    HudWidget widget{HudWidget::Text};
    float bar{0.0f};                ///< Bar fraction [0,1].
    std::string text;              ///< Text/Value readout.
    std::vector<std::string> lines; ///< List contents.
};

/// Resolve a bound Hud into a flat list of positioned draw items for the current screen
/// size/scale - each element read from its live source, so the draw reflects current state.
inline std::vector<HudDrawItem> resolveHud(const Hud& hud) {
    std::vector<HudDrawItem> out;
    for (const HudElement& e : hud.elements()) {
        if (!e.visible) continue;
        const HudVec2 p = hud.positionOf(e);
        HudDrawItem it;
        it.name = e.name;
        it.x = p.x;
        it.y = p.y;
        it.w = e.size.x * hud.scale();
        it.h = e.size.y * hud.scale();
        it.widget = e.widget;
        it.bar = e.bar();
        it.text = e.text();
        it.lines = e.list();
        out.push_back(std::move(it));
    }
    return out;
}

} // namespace game
} // namespace IKore
