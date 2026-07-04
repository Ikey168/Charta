// Mobile touch controls + GLES render-path selection - issue #367 (portable core).
//
// The device shell (Android/NDK surface, lifecycle, camera) is platform glue; this exercises
// the headless-testable half: a virtual movement stick turning a finger drag into the same
// normalized game::GameInput as keyboard/gamepad, touch buttons with single-fire edge
// detection for the capture entry point, and the desktop-GL vs GLES render-path selection.
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_touch_controls.cpp -o test_touch_controls

#include "game/TouchControls.h"
#include "render/GlesProfile.h"

#include <cmath>
#include <cstdio>
#include <vector>

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
    // 1. Joystick math: direction + deadzone + clamp, screen-up = forward (-Z).
    {
        game::GameInput right = joystickInput(100, 100, 300, 100, 100.0f); // far right
        CHECK(near(right.moveX, 1.0f) && near(right.moveZ, 0.0f));

        game::GameInput up = joystickInput(100, 100, 100, 0, 100.0f); // finger up the screen
        CHECK(near(up.moveZ, -1.0f) && near(up.moveX, 0.0f));

        game::GameInput dead = joystickInput(100, 100, 110, 100, 100.0f); // inside deadzone
        CHECK(near(dead.moveX, 0.0f) && near(dead.moveZ, 0.0f));

        // Partial deflection is between 0 and 1.
        game::GameInput part = joystickInput(0, 0, 50, 0, 100.0f);
        CHECK(part.moveX > 0.0f && part.moveX < 1.0f);
    }

    // 2. Floating joystick recentres on touch-down and clears on lift.
    {
        FloatingJoystick js(120.0f, 0.15f);
        CHECK(!js.active());
        js.onTouchDown(200, 200);
        CHECK(js.active() && near(js.originX(), 200) && near(js.originY(), 200));
        CHECK(js.onTouchMove(320, 200).moveX > 0.0f); // drag right
        js.onTouchUp();
        CHECK(!js.active());
        CHECK(near(js.onTouchMove(320, 200).moveX, 0.0f)); // inactive -> no input

        // A fixed stick keeps its origin and stays active across lifts.
        FloatingJoystick fixed(100.0f);
        fixed.setFixedOrigin(50, 50);
        fixed.onTouchUp();
        CHECK(fixed.active());
    }

    // 3. Touch buttons: hit test and multi-touch press.
    {
        TouchButton b{10, 10, 80, 40};
        CHECK(b.contains(20, 20));
        CHECK(!b.contains(200, 200));
        std::vector<TouchPoint> touches = {{5, 5, true, 0}, {50, 25, true, 1}};
        CHECK(b.pressedBy(touches)); // second finger lands on it
    }

    // 4. Button latch fires exactly once per press.
    {
        ButtonLatch latch;
        CHECK(!latch.update(false));
        CHECK(latch.update(true));  // rising edge
        CHECK(!latch.update(true)); // held -> no repeat
        CHECK(!latch.update(false));
        CHECK(latch.update(true));  // fires again on a new press
    }

    // 5. TouchControls: a capture-button tap fires once and does not drive the stick; a
    //    finger elsewhere drives movement.
    {
        TouchControls tc;
        tc.capture = TouchButton{0, 0, 80, 80};

        // Tap the capture button.
        TouchControls::Frame f1 = tc.resolve({{40, 40, true, 0}});
        CHECK(f1.captureTapped);
        CHECK(near(f1.input.moveX, 0.0f) && near(f1.input.moveZ, 0.0f)); // button != movement
        TouchControls::Frame f2 = tc.resolve({{40, 40, true, 0}});       // still held
        CHECK(!f2.captureTapped);

        // Movement: first frame sets the origin, second frame drags right.
        tc.resolve({{400, 300, true, 0}});
        TouchControls::Frame m = tc.resolve({{500, 300, true, 0}});
        CHECK(m.input.moveX > 0.0f && near(m.input.moveZ, 0.0f));
        CHECK(!m.captureTapped);
    }

    // 6. Render-path selection: desktop GL vs GLES3 vs GLES2, with the right shader dialect.
    {
        const render::RenderCaps desktop = render::selectRenderCaps(false);
        CHECK(desktop.profile == render::GlProfile::DesktopGL && !desktop.es);
        CHECK(desktop.instancing && desktop.floatTextures);
        CHECK(render::shaderVersionDirective(desktop) == "#version 330 core");

        const render::RenderCaps es3 = render::selectRenderCaps(true, 3);
        CHECK(es3.profile == render::GlProfile::GLES3 && es3.es);
        CHECK(es3.instancing);
        CHECK(render::shaderVersionDirective(es3) == "#version 300 es");

        const render::RenderCaps es2 = render::selectRenderCaps(true, 2);
        CHECK(es2.profile == render::GlProfile::GLES2 && es2.es);
        CHECK(!es2.instancing && !es2.floatTextures); // GLES2 renderer falls back
        CHECK(render::shaderVersionDirective(es2) == "#version 100");
    }

    if (g_failures == 0) {
        std::printf("test_touch_controls: all checks passed\n");
        return 0;
    }
    std::printf("test_touch_controls: %d check(s) failed\n", g_failures);
    return 1;
}
