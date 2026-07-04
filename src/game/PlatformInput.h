#pragma once

#include "core/InputMap.h"      // InputMap, InputMapBinding, InputDevice
#include "core/Settings.h"      // Settings (persistence path, #61)
#include "game/DungeonGame.h"   // game::GameInput
#include "ui/HudFramework.h"    // HudAnchor, HudVec2, resolveAnchor

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

/**
 * @file PlatformInput.h
 * @brief Rebindable controls, gamepad support, and DPI-aware UI scaling (issue #368).
 *
 * Ship-on-many-devices plumbing, modelled as headless-testable logic separated from the
 * device glue (GLFW/ImGui): raw input -> action -> normalized game::GameInput, and
 * resolution/DPI -> HUD layout. It builds on the rebindable InputMap (#60) and persists
 * through the Settings surface (#61), and lays HUD elements out with the HUD framework's
 * anchor math so they scale and respect a safe area across resolutions and aspect ratios.
 *
 *   - Movement actions (WASD by default) resolve held keys to a GameInput; rebinding an
 *     action changes the produced input immediately.
 *   - A gamepad left stick resolves to a GameInput through a radial deadzone; a
 *     GamepadManager reports connect/disconnect (hot-plug) transitions.
 *   - computeUiMetrics()/layoutElement() give DPI-scaled, safe-area-aware pixel layout.
 *
 * Header-only, std only (plus the header-only InputMap / Settings / HUD / game types).
 */
namespace IKore {
namespace platform {

// --- Movement actions --------------------------------------------------------
// Direction convention matches the game camera: up = forward = -Z, right = +X.

constexpr const char* kMoveUp = "move_up";
constexpr const char* kMoveDown = "move_down";
constexpr const char* kMoveLeft = "move_left";
constexpr const char* kMoveRight = "move_right";

/// Register the default keyboard movement bindings (WASD) into @p map, if absent.
inline void addDefaultMovementActions(InputMap& map) {
    map.addAction(kMoveUp, InputMapBinding{InputDevice::Keyboard, 'W'});
    map.addAction(kMoveLeft, InputMapBinding{InputDevice::Keyboard, 'A'});
    map.addAction(kMoveDown, InputMapBinding{InputDevice::Keyboard, 'S'});
    map.addAction(kMoveRight, InputMapBinding{InputDevice::Keyboard, 'D'});
}

/// The set of inputs currently held down this frame (device + code pairs).
struct HeldInputs {
    std::vector<InputMapBinding> held;

    void press(InputMapBinding b) {
        if (!isHeld(b)) held.push_back(b);
    }
    void release(InputMapBinding b) {
        held.erase(std::remove(held.begin(), held.end(), b), held.end());
    }
    void clear() { held.clear(); }
    bool isHeld(const InputMapBinding& b) const {
        if (!b.bound()) return false;
        for (const InputMapBinding& h : held)
            if (h == b) return true;
        return false;
    }
};

/// Resolve the movement actions in @p map against the held inputs into a GameInput.
/// Opposite directions cancel; a diagonal is the (un-normalized) sum, matching how the
/// game normalizes its own input.
inline game::GameInput resolveKeyboardMovement(const InputMap& map, const HeldInputs& held) {
    game::GameInput in;
    if (held.isHeld(map.binding(kMoveRight))) in.moveX += 1.0f;
    if (held.isHeld(map.binding(kMoveLeft))) in.moveX -= 1.0f;
    if (held.isHeld(map.binding(kMoveDown))) in.moveZ += 1.0f;
    if (held.isHeld(map.binding(kMoveUp))) in.moveZ -= 1.0f;
    return in;
}

// --- Gamepad -----------------------------------------------------------------

/// A polled gamepad snapshot: connection, left-stick axes in [-1, 1] (stick up = -Y),
/// and per-button pressed state indexed by button code.
struct GamepadState {
    bool connected{false};
    float axisLX{0.0f};
    float axisLY{0.0f};
    std::vector<bool> buttons;

    bool button(int code) const {
        return code >= 0 && code < static_cast<int>(buttons.size()) && buttons[static_cast<std::size_t>(code)];
    }
};

struct GamepadConfig {
    float deadzone{0.2f}; ///< radial deadzone; magnitudes below this produce no movement.
    bool invertY{false};
};

/// Left stick -> movement with a radial deadzone (rescaled so the deadzone edge is 0 and
/// full deflection is 1). Stick up (Y = -1) drives forward (moveZ = -1).
inline game::GameInput resolveGamepadMovement(const GamepadState& pad, const GamepadConfig& cfg = {}) {
    game::GameInput in;
    if (!pad.connected) return in;
    const float x = pad.axisLX;
    const float y = cfg.invertY ? -pad.axisLY : pad.axisLY;
    const float mag = std::sqrt(x * x + y * y);
    if (mag <= cfg.deadzone || mag <= 0.0f) return in;
    const float rescaled = (mag - cfg.deadzone) / (1.0f - cfg.deadzone);
    const float k = rescaled / mag; // normalize direction, apply rescaled magnitude
    in.moveX = x * k;
    in.moveZ = y * k;
    return in;
}

/// Combined resolve: an active gamepad stick takes over, else the keyboard.
inline game::GameInput resolveMovement(const InputMap& map, const HeldInputs& held,
                                       const GamepadState& pad, const GamepadConfig& cfg = {}) {
    const game::GameInput g = resolveGamepadMovement(pad, cfg);
    if (g.moveX != 0.0f || g.moveZ != 0.0f) return g;
    return resolveKeyboardMovement(map, held);
}

/// Tracks gamepad connection across polls to surface hot-plug transitions.
class GamepadManager {
public:
    enum class Event { None, Connected, Disconnected };

    /// Feed this frame's connection state; returns the transition since last call.
    Event update(bool connectedNow) {
        Event e = Event::None;
        if (connectedNow && !m_connected)
            e = Event::Connected;
        else if (!connectedNow && m_connected)
            e = Event::Disconnected;
        m_connected = connectedNow;
        return e;
    }
    bool connected() const { return m_connected; }

private:
    bool m_connected{false};
};

// --- DPI-aware UI / HUD scaling ---------------------------------------------

/// Safe-area insets in pixels (notches, rounded corners, system bars).
struct SafeAreaInsets {
    float left{0.0f};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
};

struct UiScaleConfig {
    float referenceDpi{96.0f}; ///< DPI at which scale == 1.0.
    float minScale{0.5f};
    float maxScale{4.0f};
};

/// Resolved UI layout metrics for a framebuffer: a DPI-derived scale, the full screen
/// size, and the safe-area rect the HUD should stay inside.
struct UiMetrics {
    float scale{1.0f};
    HudVec2 screen{};
    SafeAreaInsets safe{};

    HudVec2 safeOrigin() const { return {safe.left, safe.top}; }
    HudVec2 safeSize() const {
        return {std::max(0.0f, screen.x - safe.left - safe.right),
                std::max(0.0f, screen.y - safe.top - safe.bottom)};
    }
    /// Scale a length authored in reference pixels to this display.
    float scaled(float dip) const { return dip * scale; }
};

/// Compute UI metrics from a framebuffer size, DPI, and optional safe-area insets. The
/// scale is DPI/referenceDpi clamped to [minScale, maxScale], so the HUD keeps a constant
/// physical size across displays while layout uses the actual pixel dimensions.
inline UiMetrics computeUiMetrics(float widthPx, float heightPx, float dpi, SafeAreaInsets safe = {},
                                  const UiScaleConfig& cfg = {}) {
    UiMetrics m;
    m.screen = {widthPx, heightPx};
    m.safe = safe;
    const float ref = cfg.referenceDpi > 0.0f ? cfg.referenceDpi : 96.0f;
    m.scale = std::max(cfg.minScale, std::min(cfg.maxScale, dpi / ref));
    return m;
}

/// Top-left pixel position of a HUD element authored in reference pixels: its size and
/// margin are DPI-scaled, then anchored within the safe rect (so it never lands under a
/// notch), independent of resolution and aspect ratio.
inline HudVec2 layoutElement(const UiMetrics& m, HudAnchor anchor, HudVec2 sizeDip, HudVec2 offsetDip) {
    const HudVec2 size{sizeDip.x * m.scale, sizeDip.y * m.scale};
    const HudVec2 offset{offsetDip.x * m.scale, offsetDip.y * m.scale};
    HudVec2 p = resolveAnchor(anchor, m.safeSize(), size, offset);
    p.x += m.safe.left;
    p.y += m.safe.top;
    return p;
}

// --- Settings persistence (#61) ---------------------------------------------

/// Persist each action's binding as its own Settings key ("input.<action>"), so the blob
/// stays single-line and round-trips through the settings file.
inline void saveBindings(const InputMap& map, Settings& settings, const std::string& prefix = "input.") {
    for (std::size_t i = 0; i < map.actionCount(); ++i)
        settings.setString(prefix + map.actionName(i), toString(map.bindingAt(i)));
}

/// Load bindings previously written by saveBindings() back into @p map's registered
/// actions. Actions with no stored value keep their current binding.
inline void loadBindings(InputMap& map, const Settings& settings, const std::string& prefix = "input.") {
    for (std::size_t i = 0; i < map.actionCount(); ++i) {
        const std::string key = prefix + map.actionName(i);
        const std::string stored = settings.getString(key, "");
        if (!stored.empty()) map.rebind(map.actionName(i), bindingFromString(stored));
    }
}

} // namespace platform
} // namespace IKore
