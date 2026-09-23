# Doodlebound Android host

The Android project is independent of the root desktop CMake configuration. It
compiles the portable `src/doodle`, `src/game`, `src/cv`, and `src/mobile` headers
through `app/src/main/cpp/CMakeLists.txt`. The only native Android dependencies
are GLESv3, EGL, `libandroid`, `liblog`, and the GameActivity static library.

## Support and toolchain

| Item | Pinned contract |
| --- | --- |
| Android | API 29 minimum, API 36 compile/target |
| Graphics | OpenGL ES 3.0; Android manifest requires it |
| CPU | `arm64-v8a` phones and `x86_64` emulator |
| Java | JDK 17 or newer |
| Gradle / AGP / Kotlin | 8.13 / 8.13.2 / 2.2.20 |
| NDK / CMake | 28.2.13676358 / 3.22.1 |
| GameActivity | `androidx.games:games-activity:4.4.2` |

NDK r28 and AGP 8.13 package 16 KB aligned native libraries by default. Verify
the final APK's native segments and ZIP alignment on both 4 KB and 16 KB test
environments before release. This configuration does not claim a physical phone
result. The camera and level import paths bound images to at most 1024 × 1024
pixels before JNI, and JNI rejects level JSON over 1 MiB.

Install Android SDK platform 36, Build Tools 35.0.0, Platform Tools, NDK
28.2.13676358, and CMake 3.22.1. Then run:

```sh
cd android
./gradlew --no-daemon :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -W -n com.ikore.doodlebound/.MainActivity
./gradlew --no-daemon :app:connectedDebugAndroidTest
```

To sign a release build, set `DOODLEBOUND_KEYSTORE` to an external keystore
path, `DOODLEBOUND_KEY_ALIAS`, `DOODLEBOUND_STORE_PASSWORD`, and
`DOODLEBOUND_KEY_PASSWORD`. Set `DOODLEBOUND_VERSION_CODE` and
`DOODLEBOUND_VERSION_NAME` for the release identifier. Then run
`./gradlew --no-daemon :app:assembleRelease`. Release tasks fail if signing
credentials are absent. Never put the keystore or passwords in this repository.

The debug APK packages both ABIs. `unzip -l app/build/outputs/apk/debug/app-debug.apk`
shows the native inventory. `adb logcat -d` captures launch diagnostics. The
`Android demo` GitHub workflow builds both ABIs, runs an x86_64 emulator
launch smoke and ten pause/recreate cycles, retaining the APK, inventory,
instrumentation results and native log.

## Ownership and lifecycle

`MainActivity` is the Kotlin GameActivity host. It owns permissions, Android UI,
navigation and one native handle. `GLSurfaceView` alone owns the EGL context;
its renderer invokes all GLES functions on the GL thread. Touch and UI commands
enter the mutex guarded `MobileRenderer` session through `NativeBridge`. Native
image conversion is called on a worker thread by the product UI.

Focus loss pauses simulation and clears active touch. Activity pause queues
native GPU release on the GL thread before suspending the view. A new EGL
surface recreates native resources; the session's gameplay state remains in
memory. Activity destruction releases the native handle once. Process death
requires reloading saved level and progress data through the product UI.

The drawing vocabulary used by capture and tutorials is color first: green
start, blue exit, yellow coin and red enemy. Dark strokes form walls. Shapes
remain helpful visual hints but should not override these colors.
