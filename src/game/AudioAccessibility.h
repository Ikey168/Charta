#pragma once

#include "core/Settings.h"             // Settings (persistence, #61)
#include "game/DungeonRenderData.h"    // RenderColor, renderColorForType
#include "game/GameEvents.h"           // GameEventType (#353)

#include <cmath>
#include <map>
#include <string>
#include <vector>

/**
 * @file AudioAccessibility.h
 * @brief Event-driven audio cues, a volume mix, and colorblind-safe palettes (issue #372).
 *
 * Cohesive sound and the accessibility options players expect at launch, kept as
 * headless-testable logic (the audio subsystem #258 plays the chosen cues; the renderer
 * applies the chosen palette):
 *   - cueForEvent maps each game event (coin/key/door/switch/win/lose) to a sound cue.
 *   - AudioMix gives independent master/music/SFX volumes and a mute, with an effective-gain
 *     computation, persisted through Settings.
 *   - Colorblind palettes recolor the feature types so they stay pairwise distinguishable
 *     under deuteranopia / protanopia / tritanopia by a contrast metric (luminance plus the
 *     chroma axis that deficiency preserves), not by hue alone.
 *   - AccessibilityOptions carries reduced-motion and cue-subtitle toggles.
 *
 * Header-only, std only (plus the header-only Settings / render / event types).
 */
namespace IKore {
namespace game {

// --- Event -> sound cue ------------------------------------------------------

enum class SoundCue { None, Coin, Key, Door, Switch, Hazard, Win, Lose, MenuMove, MenuSelect };

/// The sound cue a game event triggers. A hazard death arrives as Lose (there is no separate
/// hazard event); SoundCue::Hazard is available for a caller that detects the hazard touch.
inline SoundCue cueForEvent(GameEventType type) {
    switch (type) {
        case GameEventType::CoinPickup: return SoundCue::Coin;
        case GameEventType::KeyPickup: return SoundCue::Key;
        case GameEventType::DoorOpen: return SoundCue::Door;
        case GameEventType::SwitchToggle: return SoundCue::Switch;
        case GameEventType::Win: return SoundCue::Win;
        case GameEventType::Lose: return SoundCue::Lose;
    }
    return SoundCue::None;
}

inline const char* soundCueName(SoundCue cue) {
    switch (cue) {
        case SoundCue::Coin: return "coin";
        case SoundCue::Key: return "key";
        case SoundCue::Door: return "door";
        case SoundCue::Switch: return "switch";
        case SoundCue::Hazard: return "hazard";
        case SoundCue::Win: return "win";
        case SoundCue::Lose: return "lose";
        case SoundCue::MenuMove: return "menu_move";
        case SoundCue::MenuSelect: return "menu_select";
        case SoundCue::None: return "none";
    }
    return "none";
}

// --- Volume mix --------------------------------------------------------------

enum class AudioChannel { Master, Music, Sfx };

/// Independent master / music / SFX gains plus a mute. effectiveGain multiplies the channel by
/// the master (and is 0 when muted), so a mixer applies one number per channel.
struct AudioMix {
    float master{1.0f};
    float music{0.8f};
    float sfx{1.0f};
    bool muted{false};

    float channel(AudioChannel ch) const {
        switch (ch) {
            case AudioChannel::Master: return master;
            case AudioChannel::Music: return music;
            case AudioChannel::Sfx: return sfx;
        }
        return 1.0f;
    }
    float effectiveGain(AudioChannel ch) const {
        if (muted) return 0.0f;
        const float c = ch == AudioChannel::Master ? 1.0f : channel(ch);
        return clamp01(master) * clamp01(c);
    }

    void save(Settings& s, const std::string& prefix = "audio.") const {
        s.setFloat(prefix + "master", master);
        s.setFloat(prefix + "music", music);
        s.setFloat(prefix + "sfx", sfx);
        s.setBool(prefix + "muted", muted);
    }
    void load(const Settings& s, const std::string& prefix = "audio.") {
        master = s.getFloat(prefix + "master", master);
        music = s.getFloat(prefix + "music", music);
        sfx = s.getFloat(prefix + "sfx", sfx);
        muted = s.getBool(prefix + "muted", muted);
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
};

// --- Accessibility toggles ---------------------------------------------------

struct AccessibilityOptions {
    bool reducedMotion{false}; ///< suppress screen shake / large motion effects.
    bool cueSubtitles{false};  ///< show a text label when a sound cue fires.

    void save(Settings& s, const std::string& prefix = "a11y.") const {
        s.setBool(prefix + "reduced_motion", reducedMotion);
        s.setBool(prefix + "cue_subtitles", cueSubtitles);
    }
    void load(const Settings& s, const std::string& prefix = "a11y.") {
        reducedMotion = s.getBool(prefix + "reduced_motion", reducedMotion);
        cueSubtitles = s.getBool(prefix + "cue_subtitles", cueSubtitles);
    }
};

// --- Colorblind-safe palettes -----------------------------------------------

enum class ColorVisionMode { Default, Deuteranopia, Protanopia, Tritanopia };

/// The feature types a level palette must keep distinguishable.
inline const std::vector<std::string>& paletteTypes() {
    static const std::vector<std::string> t = {"wall", "player", "enemy", "coin",
                                               "exit", "key", "switch", "hazard"};
    return t;
}

inline float luminance(const RenderColor& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

/// Perceived separation of two colors for @p mode. Luminance is preserved under every
/// deficiency, so it is always weighted; the second term is the chroma axis the deficiency
/// still resolves (blue-yellow for red-green deficiencies, red-green for blue deficiency).
/// Default uses the full RGB distance.
inline float colorContrast(const RenderColor& a, const RenderColor& b, ColorVisionMode mode) {
    const float dL = luminance(a) - luminance(b);
    if (mode == ColorVisionMode::Default) {
        const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
        return std::sqrt(dr * dr + dg * dg + db * db);
    }
    float dC = 0.0f;
    if (mode == ColorVisionMode::Tritanopia) {
        dC = (a.r - a.g) - (b.r - b.g); // red-green axis
    } else {
        // deuteranopia / protanopia: blue-yellow axis
        dC = (a.b - (a.r + a.g) * 0.5f) - (b.b - (b.r + b.g) * 0.5f);
    }
    return std::sqrt((2.0f * dL) * (2.0f * dL) + dC * dC);
}

/// A recoloring of the feature types for a given vision mode.
struct AccessibilityPalette {
    ColorVisionMode mode{ColorVisionMode::Default};
    std::map<std::string, RenderColor> overrides;

    RenderColor colorFor(const std::string& type) const {
        auto it = overrides.find(type);
        return it != overrides.end() ? it->second : renderColorForType(type);
    }
    /// Smallest pairwise contrast among the core feature types (higher = more distinguishable).
    float minPairwiseContrast() const {
        const std::vector<std::string>& types = paletteTypes();
        float m = 1e9f;
        for (std::size_t i = 0; i < types.size(); ++i)
            for (std::size_t j = i + 1; j < types.size(); ++j)
                m = std::min(m, colorContrast(colorFor(types[i]), colorFor(types[j]), mode));
        return m;
    }
};

/// A colorblind-safe palette: the feature types are spread across luminance (which survives
/// every deficiency) and given distinct hues, so they stay separable by hue for normal vision
/// and by brightness otherwise.
inline AccessibilityPalette accessibilityPalette(ColorVisionMode mode) {
    AccessibilityPalette p;
    p.mode = mode;
    // Luminance-ordered from dark to light, with distinct hues.
    p.overrides["enemy"]  = {0.45f, 0.05f, 0.05f}; // darkest - deep red
    p.overrides["hazard"] = {0.70f, 0.25f, 0.05f}; // dark orange
    p.overrides["wall"]   = {0.40f, 0.40f, 0.42f}; // mid gray
    p.overrides["exit"]   = {0.15f, 0.55f, 0.85f}; // blue
    p.overrides["switch"] = {0.20f, 0.75f, 0.70f}; // teal
    p.overrides["key"]    = {0.90f, 0.55f, 0.85f}; // light magenta
    p.overrides["player"] = {0.55f, 0.90f, 0.55f}; // light green
    p.overrides["coin"]   = {0.98f, 0.92f, 0.35f}; // brightest - yellow
    return p;
}

} // namespace game
} // namespace IKore
