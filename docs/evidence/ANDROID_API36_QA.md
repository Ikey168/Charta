# Android API 36 emulator QA (2026-09-23)

Device: `charta_api36_qa`, Android 16 / API 36, x86_64, 1080 × 2280 display,
Pixel 4 profile, SwiftShader OpenGL ES 3.0. The AVD ran headless with KVM; its
data partition was held in `/dev/shm` because local disk space was limited.
These are emulator results, not physical-phone results.

The first connected suite passed **7/7** tests on a debug APK with SHA-256
`cbc8b81d7e55024c1d9fa213c6ba7131d8c58f719c0449c708f13be03eb86f32`.
It covered bundled sample conversion to review and play, ten Activity
pause/recreate cycles, draft recovery, share tampering, interrupted conversion,
home/draw/review navigation, and ambient audio sample validity. The test result
is in `android/app/build/outputs/androidTest-results/connected/debug/` from the
local run; later runs overwrite that generated directory.

After the native pause and Android inset fixes (`97dfc24`), the connected suite passed
**9/9** tests on a debug APK with SHA-256
`affe9e004ce2cd0e52907304429c46a5ab2956b0dd5157283b5369bd79f8da18`.
The added checks imported a valid level file through the app's result handler,
played and saved it, rejected a malformed file without losing the save, and
verified menus/backgrounding keep new or retried native levels paused. The
[Preserved JUnit XML](android-api36-tests.xml) now records the latest 10-test
run, with 0 failures, 0 errors, and 0 skipped. The earlier full build and
9-test connected run succeeded in 6m 5s.

After the system-bar color adjustment (`f891ee3`), `:app:assembleDebug`
succeeded and a fresh launch/native play smoke passed on debug APK SHA-256
`9e1c2a0b45ab91b48f874c8f3c3b5ae78fa0ce880108dfb7c391d2194df53e7d`.
The recaptured play screenshot shows readable status and navigation icons and a
HUD clear of the status bar. The app crash buffer remained empty.

The signed v1 package from source commit `b66ac4c` was clean installed after
uninstalling the debug app. Artifact:
`android/build/demo-artifacts/v1-b66ac4cb7aa2/doodlebound-demo.apk`, SHA-256
`49827e070177aa03ea397ff1787a1dab7c0e1e69307053adbeb171e6797463f9`.
Android reported version code 1, version name `0.1.0-demo`. Cold launch
completed in 4.10 seconds on this emulator. Home → First steps native play →
Pause → Home worked and the app crash buffer contained no fatal exception or
ANR. A five-second `pidstat` sample of the **whole emulator process** measured
about 970% host CPU in software-rendered play, then about 44% on Pause (one
spike; steady samples near 30%) and 21% on Home. These are diagnostic CPU
readings, not phone performance measurements.

Before a same-certificate upgrade, the v1 Settings screen showed **Haptics:
Off** after changing it from On. `adb install -r` of signed v2 artifact
`android/build/demo-artifacts/v2-b66ac4cb7aa2/doodlebound-demo.apk` (SHA-256
`59cd1294630c45b95c139bd9f316e3a88dcc7412cc2d5993176c4d377aa009bd`)
succeeded without uninstalling v1. Android reported version code 2, version
name `0.1.1-demo`; Settings still showed **Haptics: Off** after relaunch. The
First steps native level opened with Coins/Pause/Tour HUD and no fatal entry in
the app crash log buffer. Android system Back from Settings exited to the
launcher rather than returning Home, so a navigation fix and a fresh signed
build are pending. This v2 artifact is retained as the upgrade test input,
not the final handoff package.

The navigation fix registers a lifecycle-bound OnBackPressedCallback through
the GameActivity AppCompat back dispatcher and keeps the legacy activity
fallback for older Android versions. On the API 36 emulator, the focused
SystemBackRegressionTest sent GLOBAL_ACTION_BACK from Settings and confirmed
the Home screen; it passed, and the full connected instrumentation suite passed
10/10 in 114.884 seconds. A manual `adb shell input keyevent 4` from Settings
also returned Home. The emulator uses three-button navigation, so this did not
test an edge swipe. The [preserved JUnit XML](android-api36-tests.xml) is the
record for this run.

The signed v3 artifact built from source commit
`f9b737741261a01b410b530c56f8450944821b7b` is
`android/build/demo-artifacts/v3-f9b737741261/doodlebound-demo.apk`, SHA-256
`d83786db9fc8a9d6752f7953137ed565336a3c813fa538f25dac593947ff2fb4`.
Android reported version code 3, version name `0.1.2-demo`; the artifact
verification passed for APK Signature Scheme v2, arm64-v8a and x86_64, and
16 KB alignment. After setting Haptics Off in signed v2, `adb install -r`
upgraded to signed v3 without uninstalling; after a cold relaunch Settings
still showed **Haptics: Off**. The v3 launch completed in 3.12 seconds.
System Back from Settings returned to Home, with `MainActivity` still resumed.
The final app log check found no fatal exception or app ANR.

Manual checks on the emulator confirmed fresh install and launch, sample
conversion to review and native play, a visibly rendered curated First steps
scene, camera permission denial with Draw/Choose Photo recovery, virtual
Camera2 preview, and Cancel returning Home. The screenshots here show
[signed v3 native play](android-api36-first-steps.png),
[signed v3 Settings with Haptics Off](android-api36-settings-haptics-off.png),
[camera denial](android-api36-camera-denied.png), and
[virtual camera preview](android-api36-camera-preview.png). The camera images
were captured against the v1 APK hash above; the native play and Settings
images were captured against the signed v3 hash above. The virtual preview
depicts the emulator's synthetic scene and does not validate physical camera
focus or photo quality.

No physical phone was connected. Thus touch feel, real camera capture, thermal and frame-pacing
budgets, second-device sharing, and 16 KB runtime loading remain pending
physical or later emulator evidence. A brief System UI
"isn't responding" prompt appeared during the emulator's first boot; the app
test suite still passed after the emulator settled.
