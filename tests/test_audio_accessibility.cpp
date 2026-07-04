// Audio cues, volume mix, and accessibility palettes - issue #372.
//
// Verifies each game event maps to the expected sound cue, the volume/mute mix computes and
// persists, accessibility toggles persist, and every colorblind palette keeps all feature
// types pairwise-distinguishable by a contrast metric (where the raw engine palette does not).
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_audio_accessibility.cpp -o test_audio_accessibility

#include "game/AudioAccessibility.h"

#include <cstdio>

using namespace IKore;
using namespace IKore::game;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// The accessibility contrast floor: below the accessible palettes' observed min (~0.24) and
// well above the raw engine palette's (~0.04-0.07), so it asserts colorblind-safety.
static constexpr float kContrastFloor = 0.15f;

int main() {
    // 1. Each game event maps to its sound cue.
    {
        CHECK(cueForEvent(GameEventType::CoinPickup) == SoundCue::Coin);
        CHECK(cueForEvent(GameEventType::KeyPickup) == SoundCue::Key);
        CHECK(cueForEvent(GameEventType::DoorOpen) == SoundCue::Door);
        CHECK(cueForEvent(GameEventType::SwitchToggle) == SoundCue::Switch);
        CHECK(cueForEvent(GameEventType::Win) == SoundCue::Win);
        CHECK(cueForEvent(GameEventType::Lose) == SoundCue::Lose);
        CHECK(std::string(soundCueName(SoundCue::Coin)) == "coin");
    }

    // 2. Volume mix: effective gain folds in master and mute; it persists via settings.
    {
        AudioMix mix;
        mix.master = 0.5f;
        mix.music = 0.8f;
        mix.sfx = 1.0f;
        CHECK(near(mix.effectiveGain(AudioChannel::Sfx), 0.5f));    // master * sfx
        CHECK(near(mix.effectiveGain(AudioChannel::Music), 0.4f));  // master * music
        CHECK(near(mix.effectiveGain(AudioChannel::Master), 0.5f));
        mix.muted = true;
        CHECK(near(mix.effectiveGain(AudioChannel::Sfx), 0.0f));    // mute overrides
        mix.muted = false;

        Settings s;
        mix.save(s);
        Settings reloaded;
        reloaded.load(s.serialize());
        AudioMix mix2;
        mix2.load(reloaded);
        CHECK(near(mix2.master, 0.5f) && near(mix2.music, 0.8f) && near(mix2.sfx, 1.0f));
        CHECK(mix2.muted == false);
    }

    // 3. Accessibility toggles persist.
    {
        AccessibilityOptions opt;
        opt.reducedMotion = true;
        opt.cueSubtitles = true;
        Settings s;
        opt.save(s);
        Settings reloaded;
        reloaded.load(s.serialize());
        AccessibilityOptions opt2;
        opt2.load(reloaded);
        CHECK(opt2.reducedMotion && opt2.cueSubtitles);
    }

    // 4. Every colorblind palette keeps all feature types pairwise-distinguishable.
    {
        const ColorVisionMode modes[] = {ColorVisionMode::Default, ColorVisionMode::Deuteranopia,
                                         ColorVisionMode::Protanopia, ColorVisionMode::Tritanopia};
        for (ColorVisionMode mode : modes) {
            const AccessibilityPalette p = accessibilityPalette(mode);
            const float m = p.minPairwiseContrast();
            std::printf("[info] palette mode %d: min pairwise contrast %.3f\n",
                        static_cast<int>(mode), m);
            CHECK(m >= kContrastFloor);
        }
        // colorFor falls back to the engine palette for a type the palette does not override.
        const AccessibilityPalette p = accessibilityPalette(ColorVisionMode::Deuteranopia);
        const RenderColor coin = p.colorFor("coin");
        const RenderColor player = p.colorFor("player");
        CHECK(colorContrast(coin, player, ColorVisionMode::Deuteranopia) >= kContrastFloor);
    }

    // 5. The metric has teeth: the raw engine palette (no accessibility overrides) fails the
    //    colorblind floor - which is exactly why the accessible palette exists.
    {
        AccessibilityPalette raw; // empty overrides -> renderColorForType for every type
        raw.mode = ColorVisionMode::Deuteranopia;
        CHECK(raw.minPairwiseContrast() < kContrastFloor);
    }

    if (g_failures == 0) {
        std::printf("test_audio_accessibility: all checks passed\n");
        return 0;
    }
    std::printf("test_audio_accessibility: %d check(s) failed\n", g_failures);
    return 1;
}
