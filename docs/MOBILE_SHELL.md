# Mobile shell (Android/GLES) - status and plan

Issue #367 delivers the mobile game on the portable doodle library (#171) behind the thin
`AppShell` host (#172). This document records what is in-tree now (the portable, headless
core) and what remains platform-specific (the device shell), so the boundary stays clean.

## In-tree now (portable, headless-tested)

- `src/game/TouchControls.h` - a virtual movement joystick (fixed or floating) that maps a
  finger drag to the same normalized `game::GameInput` the keyboard and gamepad produce, plus
  rectangular touch buttons with single-fire edge detection for the capture entry point.
- `src/render/GlesProfile.h` - selects the desktop-GL vs GLES3/GLES2 render path and the GLSL
  dialect (`#version 330 core` / `300 es` / `100`) from the runtime target, so the shared
  render code branches on capabilities, not platform.
- `tests/test_touch_controls.cpp` - covers the joystick math, deadzone/clamp, button edge
  detection, the capture tap, and render-path selection. Runs in the headless suite.

Everything above is pure logic: no GLES context, no NDK, no device. It is unit-tested in CI.

## Remaining (platform-specific shell - built outside this CI)

These require an Android toolchain (NDK + Gradle) and a device/emulator, which the headless CI
here cannot exercise. They are intentionally out of the portable core:

- A GLES2/3 context + surface via EGL, created by the Android `AppShell` host.
- `android_native_app_glue` lifecycle (init / focus / pause / resume / terminate) wired to the
  library's frame loop.
- Touch event delivery (`AMotionEvent`) feeding `TouchControls`, and the camera / image-picker
  entry point feeding the existing capture pipeline.
- A Gradle module producing a runnable APK that boots into the doodle library and plays a
  captured level.

## Capture -> play -> share loop on device

The capture pipeline (photo/import -> vectorize -> playable `DungeonGame`) and the share-code
path (#317, exercised by `SaveSync`/`LevelShare`) are already portable and headless-tested; the
shell only has to feed pixels in and surface the share string out. The shared game core keeps
passing its headless tests unchanged, satisfying the "core stays headless-testable" requirement
of #367; the on-device APK build and emulator verification are tracked as the remaining shell
work.
