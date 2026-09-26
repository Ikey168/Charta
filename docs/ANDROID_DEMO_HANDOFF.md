# Doodlebound Android demo: handoff guide

This is the guide that ships with a signed demo package. It covers installing and playing
the demo for testers, then rebuilding and supporting it for developers. The
[acceptance plan](ANDROID_DEMO_ACCEPTANCE.md) is the release gate, and the
[evidence index](evidence/ANDROID_EVIDENCE_INDEX.md) says what has actually been verified.
Nothing on this page claims a physical-phone result.

## What is in a demo package

`scripts/android-demo-package.sh`, run locally or by the `signed-demo` job in the Android demo
workflow, writes one directory per signed build:

| File | Purpose |
| --- | --- |
| `doodlebound-demo.apk` | The signed APK to install. |
| `SHA256SUMS` | Checksums for the APK and native symbols. Check before installing. |
| `BUILD.txt` | Source commit, version code and build time. |
| `apk-verification.txt` | Signature, 16 KB alignment and ABI inventory from the static checker. |
| `symbols/<abi>/libdoodlebound.so` | Unstripped native libraries for symbolizing crash traces. |
| `docs/ANDROID_DEMO_HANDOFF.md` | This guide. |
| `docs/NOTICE.txt` | Licenses for everything inside the APK. The app shows the same text under **Credits and notices**. |
| `printables/doodlebound-three-room.{svg,png}` | The drawing sheet used in the demo. |

## For testers

### Supported phones

- Android 10 (API 29) or newer, on an arm64 phone. The package also runs on x86_64 emulators.
- OpenGL ES 3.0. Install fails on devices without it.
- The demo works fully offline and needs no account. Camera permission is optional. You can
  import a photo or draw with your finger instead.

### Install, update and uninstall

1. Check the download: `sha256sum -c SHA256SUMS` should report `doodlebound-demo.apk: OK`.
2. Install with USB debugging enabled: `adb install doodlebound-demo.apk`. You can also open
   the APK on the phone after allowing installs from your file manager.
3. To update, install the newer package over the old one with `adb install -r`. Saved levels,
   drafts and settings are kept because every demo build uses the same signing certificate.
4. Uninstalling deletes saved levels, drafts and settings. Share any level you want to keep
   first, then run `adb uninstall com.ikore.doodlebound`.

### Playing

- **Move** by dragging on the movement side of the screen. **Look around** by dragging on the
  other side in Tour. Settings can swap the sides.
- **Win** by collecting every coin, then walking into the blue exit. Red enemies send you back.
- **Pause** with the HUD button to resume, retry or go home. **Tour** inspects the dungeon.
- **Settings** holds music volume, effects volume, ambient music and haptics. **Guided lessons** teach the basics in three short levels.

### Drawing and capturing a level

1. Print the three-room sheet in color at 100% scale, or draw your own square room map.
2. Mark the level with color: **green** start, **blue** exit, **yellow** coins, **red** enemies.
   Walls are dark lines.
3. Capture with **Photograph paper**, or use **Choose a photo** or **Draw a dungeon**. **Try sample drawing** runs the bundled sheet without a camera. Fill the frame with the
   whole square in bright, even light, and keep desk objects and other writing out of shot.
4. On the review screen, correct any misread wall or mark before pressing Play. The review
   screen only lets you play once a start and an exit exist and unclear marks are resolved.

### Sharing a level offline

Open **My dungeons**, choose a level, then **Share level**, and pick any local transport such
as nearby transfer or a file app. Small levels travel as `DDL1:` text and larger ones as a `.ddl`
file. On the other phone choose **Paste a level** or **Import level file**. The shared level
never includes your photo. **Compare paper and game** asks first before making an image that
does include it.

### Known limits

- The converter reads color classes. Pencil-only or shape-only marks may be misread, so
  review and correction are part of the normal flow.
- Glare, strong shadows, skew and blur reduce accuracy. Retry in even light, or use the
  sample PNG to separate a capture problem from a device problem.
- Review checks for missing elements and unclear symbols. It does not prove that every
  level using advanced mechanics can be solved.
- Real-phone camera quality, touch feel, thermals and frame pacing are still being qualified.
  See the evidence index for current status.

### When something goes wrong

| Symptom | What to do |
| --- | --- |
| Install fails | Record the `adb install` output, Android version and ABI. Check the phone has GLES 3.0. |
| Capture gives a poor level | Retry with even light and the whole square in frame, or import the sample PNG. |
| Black screen after switching apps | Capture `adb logcat -d -v time` around the switch and report it. |
| Crash or freeze | Capture logcat plus the APK hash from `SHA256SUMS`, and attach both to a GitHub issue. |

Collect device identity with `scripts/android-demo-device-report.sh path/to/apk`. Remove
personal file paths and photos from logs before attaching them.

## For developers

- **Build and QA procedure:** the [build and operator guide](ANDROID_DEMO_RUNBOOK.md) and the
  [Android host README](../android/README.md) cover the pinned toolchain, debug builds and the
  emulator suite.
- **Reproducible signed build:** commit your changes, then run the Android demo workflow with
  a `version_code` input or push a tag named `doodlebound-v<versionName>+<versionCode>`, for
  example `doodlebound-v0.1.3-demo+4`. The `signed-demo` job runs the packaging script from a
  clean checkout and uploads the package directory as a 90-day artifact. The version code must
  be higher than every build already installed on test phones.
- **Keystore ownership:** the release keystore and its passwords live only in the repository
  secrets `DOODLEBOUND_KEYSTORE_BASE64`, `DOODLEBOUND_KEY_ALIAS`, `DOODLEBOUND_STORE_PASSWORD`
  and `DOODLEBOUND_KEY_PASSWORD`, plus one offline backup held by the repository owner.
  Losing the keystore means future builds cannot upgrade installed demos. A release build
  without all four values fails instead of producing an unsigned APK.
- **Performance:** `scripts/android-demo-profile.sh --apk <apk>` measures cold launch, frame
  times, PSS and APK size against the acceptance budgets on a connected phone.
- **Crash symbolization:** use the matching `symbols/<abi>/libdoodlebound.so` from the package
  whose `BUILD.txt` commit equals the crashing build, for example with `ndk-stack`.

## License inventory

Everything packaged in the APK, with its license. The full texts are in
[NOTICE.txt](../android/app/src/main/assets/NOTICE.txt), which the app bundles and displays.

| Component | Source | License |
| --- | --- | --- |
| Charta / Doodlebound engine headers, native bridge and Kotlin app | This repository | MIT |
| Three-room sample drawing (in the APK and the printables) | Original project asset | MIT |
| Ambient music and sound cues | Synthesized at runtime by the app | MIT |
| AndroidX GameActivity 4.4.2 (native static library and Java) | Android Open Source Project | Apache-2.0 |
| AndroidX AppCompat 1.7.1, Core 1.17.0 and their AndroidX dependencies | Android Open Source Project | Apache-2.0 |
| Kotlin standard library | JetBrains | Apache-2.0 |
| GLESv3, EGL, libandroid, liblog | Provided by the phone's system image, not bundled | n/a |

No external artwork, fonts, textures or recorded audio are bundled. Before distributing a
build that adds any, add it to NOTICE.txt and this table. After a dependency change,
`./gradlew :app:dependencies --configuration releaseRuntimeClasspath` lists what the APK
contains, so this table can be checked against it.
