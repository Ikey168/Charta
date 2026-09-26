# Android demo build and operator guide

This guide is for building and reviewing the work in [the Android plan](ANDROID_DEMO_PLAN.md).
The [acceptance matrix](ANDROID_DEMO_ACCEPTANCE.md) records the release gate; this page does
not imply that any phone test has passed.

## Build from source

Use JDK 17 or newer and the versions pinned by `android/app/build.gradle.kts` and
`android/gradle/wrapper/gradle-wrapper.properties`: Android SDK platform 36,
build tools 35.0.0, CMake 3.22.1 and NDK 28.2.13676358. Keep the SDK outside this
repository and set `ANDROID_HOME` to its directory. From the repository root:

```sh
cd android
./gradlew --no-daemon :app:assembleDebug
```

The debug APK is `android/app/build/outputs/apk/debug/app-debug.apk` relative to the
repository root. Install it on an arm64 phone with USB debugging or an x86_64 emulator:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.ikore.doodlebound/.MainActivity
```

Build from a clean checkout and retain the command output, commit ID, APK hash and
Android build versions with any QA result. The Gradle and native build must not require
the desktop engine's GLFW, GLAD, OpenAL or Bullet dependencies.
Run [APK static checks](../scripts/android-demo-verify-apk.sh) on the resulting APK to
verify its signature, 16 KB zip alignment and both demo ABIs; that check does not prove
runtime compatibility on a 16 KB device.

For a distributable demo build, set `DOODLEBOUND_KEYSTORE`, `DOODLEBOUND_KEY_ALIAS`,
`DOODLEBOUND_STORE_PASSWORD` and `DOODLEBOUND_KEY_PASSWORD` in a private shell environment,
then run `./gradlew --no-daemon :app:assembleRelease`. The same certificate must be retained
for upgrades. Set `DOODLEBOUND_VERSION_CODE` to a strictly higher integer when upgrading;
`DOODLEBOUND_VERSION_NAME` is the human-readable label. Keep the keystore and credentials
outside the repository. Verify the exact release APK with the static checker and an install
or upgrade on a device before sharing it. A build that lacks signing variables fails rather
than silently producing an unsigned release.
After committing source, [the packaging script](../scripts/android-demo-package.sh) can
build the release APK, verify it, archive unstripped native symbols and write SHA-256
checksums under `android/build/demo-artifacts/`. It refuses to overwrite an existing
artifact directory. Archive the keystore separately under an accountable owner; the
artifact package intentionally excludes it. The Android demo workflow's `signed-demo` job
runs the same script in CI from repository secrets; the
[handoff guide](ANDROID_DEMO_HANDOFF.md) describes how to trigger it and what it archives.

To update an installed demo, keep the same signing certificate, increment
`DOODLEBOUND_VERSION_CODE`, and run `adb install -r` with the new signed APK.
An uninstall removes the app's private saved levels, drafts and settings; export any
levels you want to keep first, then run `adb uninstall com.ikore.doodlebound`.

## Quick operator path

1. Launch Doodlebound with the phone offline. Start a bundled level and use touch controls
   to collect its coins and reach the exit. Lose/retry and return home.
2. Print [the three-room sheet](../assets/printables/doodlebound-three-room.svg) in color at
   100% or open the [sample PNG](../assets/printables/doodlebound-three-room.png). Its square
   playing field contains only walls and marks; crop out desk objects and other writing.
3. Capture or import the sheet. Green means start, blue exit, yellow coin, and red enemy.
   Inspect the review overlay and correct any misread marks before playing. Bright, even
   light and a frame that includes the whole square make the result easier to review.
4. Play the created level, save it, and share/import it on a second phone using a local
   transport. The share payload is self-contained; do not assume a backend resolves a short
   code. Treat source photos as private unless explicitly exporting a comparison image.

During play, drag on the movement side of the screen to walk; the other side is used to
look around in Tour. The movement side can be switched in Settings. Use the HUD's Pause
button to resume, retry or return home, and Tour/Return to inspect the dungeon. Collect
every coin before entering the blue exit. The library's level menu offers Play, Edit,
Duplicate, Share level, Compare paper and game, and Delete.

For offline sharing, open **My dungeons → a level → Share level** and choose a local
transport such as nearby transfer or a file app. Small levels are sent as `DDL1:` text;
larger ones as a `.ddl` file. On the receiving phone choose **Paste a level** for the text
or **Import level file** for the file, then review and play. The level payload omits the
source photo. **Compare paper and game** asks separately before creating an image that
includes the photo.

For the full demo, use the numbered [demonstration script](ANDROID_DEMO_ACCEPTANCE.md).
Capture the actual behavior and any deviation; a route that is missing in the current build
remains an open issue.

## Troubleshooting and evidence

If the app does not install, record the Android version, ABI and `adb install` output;
check that the APK includes the ABI and device supports GLES 3.0. If capture fails, retry
with the sample PNG and record whether the camera, picker, decoder, converter or review step
failed. If graphics disappear after switching apps, capture a logcat trace around surface
recreation. To collect logs and artifact identity:

```sh
adb logcat -d -v time > doodlebound-logcat.txt
sha256sum app/build/outputs/apk/debug/app-debug.apk
adb shell getprop ro.build.version.release
adb shell getprop ro.product.model
```

Store logs outside the repository if they include personal file paths or photos. Attach
sanitized logs and steps to the relevant GitHub issue. Record whether testing used an
emulator or physical hardware; they prove different parts of the acceptance matrix.
The [device report script](../scripts/android-demo-device-report.sh) prints a Markdown
identity block for one authorized connected device and an optional APK. The
[profile script](../scripts/android-demo-profile.sh) measures cold launch, play frame times,
PSS and APK size against the acceptance budgets on the same device.

## Boundaries

The current Tier-1 converter recognizes color classes. Shape-only pencil marks can be
misclassified; review and correction are part of the workflow. `readyToPlay()` checks
missing elements and unresolved low-confidence symbols; it does not by itself prove that
every advanced-mechanic level is solvable. Android photos, lighting and device performance
must be measured against real hardware before signing off the demo.

The app packages [NOTICE.txt](../android/app/src/main/assets/NOTICE.txt), which includes
the Charta MIT notice and full Apache-2.0 text for the AndroidX/Kotlin dependencies.
The sample sheet is an original project asset. Any later external artwork, music or
typefaces must be added to that inventory before a release artifact is distributed.
