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
[Preserved JUnit XML](android-api36-tests.xml) records 9 tests, 0 failures,
0 errors, and 0 skipped. A full build and connected run succeeded in 6m 5s.

After the system-bar color adjustment (`f891ee3`), `:app:assembleDebug`
succeeded and a fresh launch/native play smoke passed on debug APK SHA-256
`9e1c2a0b45ab91b48f874c8f3c3b5ae78fa0ce880108dfb7c391d2194df53e7d`.
The recaptured play screenshot shows readable status and navigation icons and a
HUD clear of the status bar. The app crash buffer remained empty.

Manual checks on the emulator confirmed fresh install and launch, sample
conversion to review and native play, a visibly rendered curated First steps
scene, camera permission denial with Draw/Choose Photo recovery, virtual
Camera2 preview, and Cancel returning Home. The screenshots here show
[native play](android-api36-first-steps.png),
[camera denial](android-api36-camera-denied.png), and
[virtual camera preview](android-api36-camera-preview.png). The camera images
were captured against the first APK hash above; the native play image was
recaptured against the final hash. The virtual preview depicts the emulator's
synthetic scene and does not validate physical camera focus or photo quality.

No physical phone was connected. Thus touch feel, real camera capture, thermal and frame-pacing
budgets, second-device sharing, 16 KB runtime loading, and signed APK upgrade
remain pending physical or later emulator evidence. A brief System UI
"isn't responding" prompt appeared during the emulator's first boot; the app
test suite still passed after the emulator settled.
