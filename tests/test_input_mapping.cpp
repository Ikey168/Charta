// Platform input: rebinding, gamepad, and UI scaling - issue #368.
//
// Verifies the acceptance criteria as headless logic (raw input -> action, resolution ->
// layout): rebinding an action changes the produced GameInput, a synthetic gamepad axis
// maps to movement (with a deadzone and hot-plug detection), HUD layout metrics scale
// correctly across resolutions/DPIs and respect a safe area, and bindings persist through
// the settings path. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_input_mapping.cpp -o test_input_mapping

#include "game/PlatformInput.h"

#include <cmath>
#include <cstdio>

using namespace IKore;
using namespace IKore::platform;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    // 1. Rebinding an action changes the produced input immediately.
    {
        InputMap map;
        addDefaultMovementActions(map); // WASD

        HeldInputs held;
        held.press(InputMapBinding{InputDevice::Keyboard, 'D'}); // default = move_right
        CHECK(near(resolveKeyboardMovement(map, held).moveX, 1.0f));

        // Rebind move_right to 'L'. Holding 'D' now does nothing; holding 'L' moves right.
        map.rebind(kMoveRight, InputMapBinding{InputDevice::Keyboard, 'L'});
        CHECK(near(resolveKeyboardMovement(map, held).moveX, 0.0f));
        held.release(InputMapBinding{InputDevice::Keyboard, 'D'});
        held.press(InputMapBinding{InputDevice::Keyboard, 'L'});
        CHECK(near(resolveKeyboardMovement(map, held).moveX, 1.0f));

        // Opposite keys cancel; up is forward (-Z).
        HeldInputs both;
        both.press(map.binding(kMoveUp));
        both.press(map.binding(kMoveDown));
        CHECK(near(resolveKeyboardMovement(map, both).moveZ, 0.0f));
        HeldInputs up;
        up.press(map.binding(kMoveUp));
        CHECK(near(resolveKeyboardMovement(map, up).moveZ, -1.0f));
    }

    // 2. A synthetic gamepad axis maps to movement, with a radial deadzone.
    {
        GamepadState pad;
        pad.connected = true;
        pad.axisLX = 1.0f; // full right
        pad.axisLY = 0.0f;
        game::GameInput g = resolveGamepadMovement(pad);
        CHECK(near(g.moveX, 1.0f) && near(g.moveZ, 0.0f));

        pad.axisLX = 0.0f;
        pad.axisLY = -1.0f; // stick up -> forward
        g = resolveGamepadMovement(pad);
        CHECK(near(g.moveZ, -1.0f));

        // Inside the deadzone -> no movement.
        pad.axisLX = 0.1f;
        pad.axisLY = 0.05f;
        g = resolveGamepadMovement(pad);
        CHECK(near(g.moveX, 0.0f) && near(g.moveZ, 0.0f));

        // Disconnected pad produces nothing.
        pad.connected = false;
        pad.axisLX = 1.0f;
        CHECK(near(resolveGamepadMovement(pad).moveX, 0.0f));
    }

    // 3. Combined resolve: an active stick overrides the keyboard; a centered stick defers.
    {
        InputMap map;
        addDefaultMovementActions(map);
        HeldInputs held;
        held.press(map.binding(kMoveRight)); // keyboard says +X

        GamepadState pad;
        pad.connected = true;
        pad.axisLX = -1.0f; // stick says -X
        CHECK(resolveMovement(map, held, pad).moveX < 0.0f); // gamepad wins

        pad.axisLX = 0.0f; // centered -> falls back to keyboard
        CHECK(near(resolveMovement(map, held, pad).moveX, 1.0f));
    }

    // 4. Hot-plug: connect/disconnect transitions are reported once.
    {
        GamepadManager mgr;
        CHECK(mgr.update(false) == GamepadManager::Event::None);
        CHECK(mgr.update(true) == GamepadManager::Event::Connected);
        CHECK(mgr.update(true) == GamepadManager::Event::None);
        CHECK(mgr.update(false) == GamepadManager::Event::Disconnected);
        CHECK(!mgr.connected());
    }

    // 5. HUD layout scales with DPI and respects resolution.
    {
        const UiMetrics m1 = computeUiMetrics(1920, 1080, 96.0f);
        CHECK(near(m1.scale, 1.0f));
        // TopRight of a 100x40 element with a 20px margin.
        HudVec2 tr = layoutElement(m1, HudAnchor::TopRight, {100, 40}, {20, 20});
        CHECK(near(tr.x, 1920 - 100 - 20) && near(tr.y, 20));
        HudVec2 bl = layoutElement(m1, HudAnchor::BottomLeft, {100, 40}, {20, 20});
        CHECK(near(bl.x, 20) && near(bl.y, 1080 - 40 - 20));

        const UiMetrics m2 = computeUiMetrics(1920, 1080, 192.0f); // 2x DPI
        CHECK(near(m2.scale, 2.0f));
        CHECK(near(m2.scaled(100.0f), 200.0f));
        // Element and margin both double; the anchor still hugs the corner.
        HudVec2 tr2 = layoutElement(m2, HudAnchor::TopRight, {100, 40}, {20, 20});
        CHECK(near(tr2.x, 1920 - 200 - 40) && near(tr2.y, 40));
        HudVec2 tl2 = layoutElement(m2, HudAnchor::TopLeft, {100, 40}, {20, 20});
        CHECK(near(tl2.x, 40) && near(tl2.y, 40));
    }

    // 6. Safe-area insets keep the HUD off notches/system bars.
    {
        SafeAreaInsets safe;
        safe.left = 40;
        safe.top = 120;
        safe.right = 40;
        safe.bottom = 80;
        const UiMetrics m = computeUiMetrics(1080, 2400, 96.0f, safe);
        HudVec2 tl = layoutElement(m, HudAnchor::TopLeft, {100, 40}, {0, 0});
        CHECK(near(tl.x, 40) && near(tl.y, 120)); // pushed in by the insets
        HudVec2 br = layoutElement(m, HudAnchor::BottomRight, {100, 40}, {0, 0});
        // Bottom-right element sits flush against the safe rect's bottom-right.
        CHECK(near(br.x + 100, 1080 - 40));       // right edge == screen - right inset
        CHECK(near(br.y + 40, 2400 - 80));        // bottom edge == screen - bottom inset
    }

    // 7. Bindings persist through the settings path and survive a settings round-trip.
    {
        InputMap map;
        addDefaultMovementActions(map);
        map.rebind(kMoveRight, InputMapBinding{InputDevice::Keyboard, 'L'});
        map.rebind(kMoveUp, InputMapBinding{InputDevice::Gamepad, 11});

        Settings settings;
        saveBindings(map, settings);

        // Reload into a fresh default map through a serialized/reloaded Settings.
        Settings reloaded;
        reloaded.load(settings.serialize());
        InputMap map2;
        addDefaultMovementActions(map2);
        loadBindings(map2, reloaded);
        CHECK(map2.binding(kMoveRight) == (InputMapBinding{InputDevice::Keyboard, 'L'}));
        CHECK(map2.binding(kMoveUp) == (InputMapBinding{InputDevice::Gamepad, 11}));
        CHECK(map2.binding(kMoveLeft) == (InputMapBinding{InputDevice::Keyboard, 'A'})); // untouched
    }

    if (g_failures == 0) {
        std::printf("test_input_mapping: all checks passed\n");
        return 0;
    }
    std::printf("test_input_mapping: %d check(s) failed\n", g_failures);
    return 1;
}
