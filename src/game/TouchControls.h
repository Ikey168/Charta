#pragma once

#include "game/DungeonGame.h" // game::GameInput

#include <cmath>
#include <cstddef>
#include <vector>

/**
 * @file TouchControls.h
 * @brief Touch movement + action controls for the mobile shell (issue #367).
 *
 * The portable doodle core is renderer- and platform-agnostic; a phone drives it through
 * touch. This is the headless-testable half of that: a virtual movement joystick (fixed or
 * floating) that turns a finger drag into the same normalized game::GameInput the keyboard
 * and gamepad produce, plus rectangular touch buttons with edge detection for actions such
 * as the capture entry point. The Android/NDK surface + lifecycle glue that feeds real touch
 * events in is the platform-specific shell; everything here is pure math and unit-testable.
 *
 * Screen space is pixels with +Y down; up on screen (smaller Y) is forward (-Z), matching
 * the keyboard/gamepad convention in PlatformInput. Header-only, std only.
 */
namespace IKore {
namespace platform {

/// A single touch sample in screen pixels.
struct TouchPoint {
    float x{0.0f};
    float y{0.0f};
    bool down{false};
    int id{0}; ///< finger id for multi-touch.
};

/// Pure joystick math: the movement from a stick @p origin to the current touch point,
/// with a radial deadzone and clamped to full deflection at @p radius. Magnitude is 0 at
/// the deadzone edge and 1 at (or beyond) the radius.
inline game::GameInput joystickInput(float originX, float originY, float touchX, float touchY,
                                     float radius, float deadzone = 0.15f) {
    game::GameInput in;
    const float dx = touchX - originX;
    const float dy = touchY - originY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float deadPx = deadzone * radius;
    if (radius <= 0.0f || dist <= deadPx || dist <= 0.0f) return in;
    const float clamped = std::min(dist, radius);
    const float mag = (clamped - deadPx) / (radius - deadPx); // 0 at deadzone edge -> 1 at radius
    const float nx = dx / dist, ny = dy / dist;
    in.moveX = nx * mag;
    in.moveZ = ny * mag; // screen up (dy < 0) -> forward (-Z)
    return in;
}

/// A floating movement stick: its origin snaps to wherever the finger first lands, then the
/// drag from there drives movement until the finger lifts. (A fixed stick is just this with
/// a preset origin and no re-centering.)
class FloatingJoystick {
public:
    explicit FloatingJoystick(float radius = 120.0f, float deadzone = 0.15f)
        : m_radius(radius), m_deadzone(deadzone) {}

    void setFixedOrigin(float x, float y) {
        m_originX = x;
        m_originY = y;
        m_active = true;
        m_fixed = true;
    }

    void onTouchDown(float x, float y) {
        if (!m_fixed) {
            m_originX = x;
            m_originY = y;
        }
        m_active = true;
    }
    game::GameInput onTouchMove(float x, float y) const {
        return m_active ? joystickInput(m_originX, m_originY, x, y, m_radius, m_deadzone)
                        : game::GameInput{};
    }
    void onTouchUp() {
        if (!m_fixed) m_active = false;
    }

    bool active() const { return m_active; }
    float originX() const { return m_originX; }
    float originY() const { return m_originY; }

private:
    float m_radius;
    float m_deadzone;
    float m_originX{0.0f};
    float m_originY{0.0f};
    bool m_active{false};
    bool m_fixed{false};
};

/// A rectangular touch target (top-left origin, size), in screen pixels.
struct TouchButton {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    bool pressedBy(const std::vector<TouchPoint>& touches) const {
        for (const TouchPoint& t : touches)
            if (t.down && contains(t.x, t.y)) return true;
        return false;
    }
};

/// Fires exactly once on the frame a boolean condition goes from false to true - so a held
/// finger on the capture button triggers capture a single time, not every frame.
class ButtonLatch {
public:
    bool update(bool pressedNow) {
        const bool edge = pressedNow && !m_prev;
        m_prev = pressedNow;
        return edge;
    }
    bool held() const { return m_prev; }

private:
    bool m_prev{false};
};

/// The on-screen touch layout: a movement stick plus a capture button (the entry into the
/// photograph/import capture pipeline). resolve() maps this frame's touches to a GameInput
/// and reports whether capture was just tapped.
struct TouchControls {
    FloatingJoystick move{120.0f, 0.15f};
    TouchButton capture; ///< the capture entry-point button.

    struct Frame {
        game::GameInput input;
        bool captureTapped{false};
    };

    /// Feed this frame's active touches. The first touch not on a button drives the stick.
    Frame resolve(const std::vector<TouchPoint>& touches) {
        Frame f;
        bool stickDown = false;
        float sx = 0.0f, sy = 0.0f;
        for (const TouchPoint& t : touches) {
            if (!t.down) continue;
            if (capture.contains(t.x, t.y)) continue; // reserved for the button
            stickDown = true;
            sx = t.x;
            sy = t.y;
            break;
        }
        if (stickDown) {
            if (!move.active()) move.onTouchDown(sx, sy);
            f.input = move.onTouchMove(sx, sy);
        } else {
            move.onTouchUp();
        }
        f.captureTapped = m_captureLatch.update(capture.pressedBy(touches));
        return f;
    }

private:
    ButtonLatch m_captureLatch;
};

} // namespace platform
} // namespace IKore
