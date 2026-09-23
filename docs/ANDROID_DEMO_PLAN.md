# Doodlebound: complete Android phone demo

Status: planned, not implemented or device-verified. Baseline inspected on 2026-09-23:
`3c2318c66946797dc31cf97f5b354edf6c070ca4`. See the
[delivery backlog](ANDROID_DEMO_BACKLOG.md) and [acceptance plan](ANDROID_DEMO_ACCEPTANCE.md).

## Outcome and scope

Deliver an installable, signed Android APK that a new player can use without a developer,
computer, account, or network: try a tutorial, photograph a paper dungeon or draw on-screen,
review and fix its interpretation, explore it in visible 3D, collect coins, evade enemies,
win or retry, save, and share a level that a second phone can import and play.
“Full demo” means this complete product journey, including menus, errors, sound, persistence,
and installation instructions. It does not mean every feature in the long-term game design.

Required content: three guided tutorial levels plus five curated challenge levels, one
polished dungeon theme, a printable three-room drawing sheet, top-down 3D play and a touch
tour camera. Core creation vocabulary: walls, start, exit, coins, enemy. Curated levels also
show keys/doors, switches, hazards and blocks after their mobile integration is verified.
Use the implemented triangle=start, square=exit, circle=coin, X=enemy vocabulary from
[the player guide](PLAYER_GUIDE.md); the older concept's S/X vocabulary conflicts with it.
Freeze a single tested legend before producing tutorials, samples and capture fixtures.

Online accounts, cloud sync, public UGC discovery, leaderboards, live multiplayer, ads,
purchases, iOS, Play Store publication and unrestricted messy-drawing recognition are
outside this demo. Existing portable implementations remain available for subsequent work.
Share uses the Android share sheet and local import; no hosted short-code resolver is assumed.

## What exists and what must be connected

| Existing seam | Evidence and remaining Android work |
| --- | --- |
| `src/doodle/Doodle.h`, CMake `doodle` INTERFACE target | Portable photo interpretation, scene conversion and level serialization. Root CMake currently fetches desktop dependencies before this target; isolate an Android build entry point. |
| `src/mobile/AppShell.h` | Headless Draw/Review/Play/Share state machine; no Android Activity, lifecycle, camera or UI. Add image ingestion, cancellation and restore seams rather than duplicating game rules. |
| `TouchControls.h`, `PlatformInput.h`, `GlesProfile.h` | Input math and profile selection; not a working GLES renderer or Android event adapter. |
| `DungeonGame.h`, `DungeonRenderData.h`, `GameCamera.h`, `TourCamera.h` | Reuse simulation and presentation data; implement actual GLES meshes, shaders, resources and camera controls. |
| `LevelReview.h`, `LevelEditor.h`, `LevelRepair.h`, `Solver.h` | Reuse editing and bounded validation. Readiness checks are not proof of full mechanic-aware solvability. Report unsupported/unknown outcomes honestly. |
| `Campaign.h`, `LaunchContent.h`, `Tutorial.h`, `StarRating.h` | Reuse data and rules; curate and play-test the phone campaign. Existing solver-approved content deliberately routes around some advanced mechanics. |
| `SaveSync.h`, `LevelShare.h`, `AudioAccessibility.h`, `Localization.h` | Portable policies and formats need Android storage, sharing, playback, settings and UI adapters. |

[Mobile status](MOBILE_SHELL.md) explicitly defers the APK/device shell. Historical closed
issues #171, #172, #367–#374 represent reusable foundations, not Android acceptance evidence.
[Post-1.0 backlog #419](https://github.com/Ikey168/Charta/issues/419) is the parent context;
this plan decomposes its mobile productization item without reopening completed core work.

## Proposed architecture and decisions

Add an `android/` Gradle project with a thin Kotlin GameActivity host, a C++ JNI bridge,
and an Android-only CMake entry point linking the portable core. Kotlin owns navigation,
permission prompts, image acquisition, app-private files and sharing. C++ owns the fixed-step
simulation, converter and GLES3 presentation. Send explicit commands and immutable results
across the bridge; document thread ownership and object lifetime. Keep desktop builds intact.

GameActivity supplies its own input interfaces; do not mix its glue with NativeActivity's
event queue. Confirm this architecture in the first milestone with a running phone spike.
See [Android GameActivity documentation](https://developer.android.com/games/agdk/game-activity).
This supersedes the older mobile note's tentative NativeActivity/AMotionEvent approach.

Proposed support floor: Android 10/API 29, GLES 3.0, arm64-v8a phones; x86_64 for emulator
testing. Unsupported graphics devices receive a useful error. Pin exact JDK/Gradle/AGP/NDK,
CMake, compileSdk and targetSdk versions in M25 after a compatibility build; these are
implementation choices, not assertions about current store requirements. Verify packaged
native libraries on both 4 KB and 16 KB environments using the
[Android page-size guide](https://developer.android.com/guide/practices/page-sizes).

Landscape-first gameplay and creation; Android system camera/picker screens may rotate.
Respect cutouts, gesture areas, display density and system font scaling. Keep control layouts
independent of render resolution. Support back navigation with unsaved-work confirmation.
Pause simulation and audio on focus loss; clear active fingers on cancel; restore graphics
resources after surface loss. Resume after process death to a safe saved checkpoint.

Capture uses an in-app camera preview with permission requested only on entry. Gallery import
uses the [Android photo picker](https://developer.android.com/training/data-storage/shared/photo-picker)
with a document-picker fallback where unavailable. Decode content URIs with EXIF orientation,
explicit byte/pixel limits and downsampling before native allocation. Process bounded images
off the UI/render threads with progress, cancellation and stale-result suppression. Camera
denial always leaves drawing, sample levels and image import usable.

Review shows the original image and detected walls/symbols; users can correct crop corners,
move/add/remove symbols, repair walls, undo, and rerun validation. Auto-repair must preview
changes and require an explicit apply action. Limit solver time/state count; distinguish
solvable, invalid, unsupported and timed-out results. Do not promise universal fairness.

Save versioned levels, draft, progress and settings atomically in app-private storage; retain
a recoverable last-known-good file. Validate imported schema, dimensions and entity counts.
Share versioned level files or self-contained codes through temporary URI grants. Export a
paper/3D comparison image only on request. Keep photos local and exclude source photos from
level exports by default; allow deletion and clean temporary exports.

## Delivery sequence

| Milestone | Exit gate |
| --- | --- |
| M25: Android foundation | Pinned clean build installs and boots on an arm64 phone; lifecycle smoke and CI artifact exist. |
| M26: Playable 3D phone slice | A bundled level renders correctly and can be won/lost/retried entirely with touch. |
| M27: Draw, capture and repair | Paper, picker and finger-drawing paths produce an editable and playable level on-device. |
| M28: Complete demo experience | Tutorials, eight levels, tour, sound, settings and accessible navigation form a coherent game. |
| M29: Save and share | Relaunch preserves work; a second phone imports and plays the shared level offline. |
| M30: Device quality | Regression, lifecycle, capture robustness, performance and physical-device gates pass. |
| M31: Demo handoff | Signed APK, checksum, symbols, license inventory, guide, video and signed-off evidence are delivered. |

Critical path: foundation → touch 3D slice → capture/review → complete experience → device
qualification → handoff. Storage can start after the foundation; sharing needs capture and
review schemas. Content work can start after the touch slice. Individual issue dependencies
in the backlog are authoritative. No due dates or assignees are invented; each milestone is
gated by evidence, and effort estimates follow the M25 spike.

## Risks and controls

| Risk | Control / decision gate |
| --- | --- |
| Desktop renderer dependencies leak into Android | Independent CMake path and dependency audit in M25; GLES3 slice before content expansion. |
| Core helpers do not expose a full app lifecycle | Explicit host contract and process/surface-loss scenarios; preserve one simulation authority. |
| Camera data differs from synthetic fixtures | Real photos with lighting/skew/blur variation; constrained paper guide and editable preview. |
| Solver cannot validate advanced mechanics | Declare support limits, curate human-verified routes, never label unknown as guaranteed solvable. |
| Thermal/memory pressure ruins live demo | Bound image and level sizes, profile on the reference phone, tune quality before signing. |
| Shared payload is oversized or malformed | Size limits, schema checks, file-sharing fallback, visible errors and cross-device round trips. |

Close implementation issues only with the validation listed in their acceptance criteria.
Close the umbrella only after all release gates pass on the exact distributed APK. Headless
tests, emulator screenshots and compilation alone cannot substitute for the phone demo.
