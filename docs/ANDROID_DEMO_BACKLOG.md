# Android demo delivery backlog

Umbrella: [#421](https://github.com/Ikey168/Charta/issues/421). Parent context: [#419](https://github.com/Ikey168/Charta/issues/419).

[Product and technical plan](ANDROID_DEMO_PLAN.md) · [Acceptance and handoff](ANDROID_DEMO_ACCEPTANCE.md)

All work is initially open. GitHub is authoritative for current status. Dependencies are prerequisites, not suggested reading. Estimates and owners are assigned after the foundation spike; no arbitrary dates are imposed.

## [M25: Doodlebound Android foundation](https://github.com/Ikey168/Charta/milestone/27)

Exit gate: Pinned clean build installs and boots on an arm64 phone; lifecycle smoke and CI artifact exist.

| Work item | Prerequisites |
| --- | --- |
| [A01: Pin Android support contract and prove the host architecture](https://github.com/Ikey168/Charta/issues/422) | None |
| [A02: Add isolated Gradle and NDK build for the portable game](https://github.com/Ikey168/Charta/issues/423) | [A01 / #422](https://github.com/Ikey168/Charta/issues/422) |
| [A03: Implement Android lifecycle and native session ownership](https://github.com/Ikey168/Charta/issues/424) | [A02 / #423](https://github.com/Ikey168/Charta/issues/423) |
| [A04: Build and smoke-test Android artifacts in CI](https://github.com/Ikey168/Charta/issues/425) | [A02 / #423](https://github.com/Ikey168/Charta/issues/423), [A03 / #424](https://github.com/Ikey168/Charta/issues/424) |

### A01: Pin Android support contract and prove the host architecture

Spike Kotlin GameActivity + JNI + portable doodle core. Record toolchain pins, API/ABI/GLES support, thread ownership, reference devices, image/level limits and vocabulary decision; inspect AppShell seams and root CMake desktop coupling.

Acceptance: Install an arm64 spike on a named physical phone and attach build/launch evidence; commit the version matrix and host ADR; confirm triangle/start, square/exit, circle/coin and X/enemy fixtures; document limits and decision rationale.

### A02: Add isolated Gradle and NDK build for the portable game

Create android/ wrapper, app manifest, Android CMake entry point, packaged assets and arm64-v8a/x86_64 variants without configuring desktop GLFW/GLAD/OpenAL dependencies.

Acceptance: A clean checkout builds and installs a debug APK using documented commands; both ABIs compile; dependency and native page-alignment inventory is recorded; existing desktop configuration remains valid.

### A03: Implement Android lifecycle and native session ownership

Bridge GameActivity to one AppShell/game session with explicit UI/render/worker ownership, fixed timestep, surface recreation, focus changes, back navigation and cancellation.

Acceptance: Ten background/foreground and surface-recreation cycles keep the session usable; pause stops simulation/audio and clears touch; native objects release once; lifecycle instrumentation reports no crash or black surface.

### A04: Build and smoke-test Android artifacts in CI

Add pinned Android CI build, emulator install/launch smoke, portable regression dependencies, APK and native-log retention; document local reproduction.

Acceptance: A clean CI runner produces both ABI builds, installs the test APK and opens the host; failures fail the job; artifacts include commit/version and logs; existing required workflows continue to pass.

## [M26: Doodlebound playable 3D phone slice](https://github.com/Ikey168/Charta/milestone/28)

Exit gate: A bundled 3D level can be won, lost and retried entirely with touch on a physical phone.

| Work item | Prerequisites |
| --- | --- |
| [A05: Render a real Doodlebound scene with GLES3](https://github.com/Ikey168/Charta/issues/426) | [A03 / #424](https://github.com/Ikey168/Charta/issues/424) |
| [A06: Connect multitouch movement and phone camera controls](https://github.com/Ikey168/Charta/issues/427) | [A05 / #426](https://github.com/Ikey168/Charta/issues/426) |
| [A07: Wire the complete touch gameplay and results loop](https://github.com/Ikey168/Charta/issues/428) | [A05 / #426](https://github.com/Ikey168/Charta/issues/426), [A06 / #427](https://github.com/Ikey168/Charta/issues/427) |
| [A08: Create the Android home and navigation flow](https://github.com/Ikey168/Charta/issues/429) | [A07 / #428](https://github.com/Ikey168/Charta/issues/428) |

### A05: Render a real Doodlebound scene with GLES3

Implement GLES3 shaders, camera, floor/wall meshes and visible actors from SceneDescription and DungeonRenderData; manage GPU resources independently of desktop GL and rebuild after context loss.

Acceptance: Physical-phone screenshots show correctly positioned walls, player, coin, enemy and exit with depth; GL errors are absent; resizing/context recreation restores identical content; unsupported GLES has a readable failure screen.

### A06: Connect multitouch movement and phone camera controls

Adapt GameActivity pointer events to TouchControls and GameInput, including cancel/pointer-up handling, deadzones, left/right layouts, safe areas and camera gestures.

Acceptance: Two simultaneous fingers can move and operate controls without interference; releasing/cancelling/backgrounding clears movement; controls remain usable on both phone layouts; attach a touch-play video.

### A07: Wire the complete touch gameplay and results loop

Drive DungeonGame on the fixed tick and render its authoritative state; add HUD, collect-all-coins exit rule, enemy loss, pause, restart, win results and return home.

Acceptance: A bundled level can be won and lost then retried without desktop input; HUD matches simulation; pause does not advance timers; repeated retries reset actors and score; portable gameplay regressions pass.

### A08: Create the Android home and navigation flow

Add branded home, sample selection, creation entry points, loading/error screens, pause/results navigation and unsaved-work confirmation; hide unavailable features until integrated.

Acceptance: A tester can launch, choose, play, retry and return home without a dead end; system back works in every available screen; camera permissions are not requested at startup; sample play works offline.

## [M27: Doodlebound draw, capture and repair](https://github.com/Ikey168/Charta/milestone/29)

Exit gate: Paper capture, image import and finger drawing produce editable, validated and playable levels on-device.

| Work item | Prerequisites |
| --- | --- |
| [A09: Acquire camera and picker images safely on Android](https://github.com/Ikey168/Charta/issues/430) | [A03 / #424](https://github.com/Ikey168/Charta/issues/424), [A08 / #429](https://github.com/Ikey168/Charta/issues/429) |
| [A10: Ship the touch drawing canvas and tested symbol legend](https://github.com/Ikey168/Charta/issues/431) | [A06 / #427](https://github.com/Ikey168/Charta/issues/427), [A08 / #429](https://github.com/Ikey168/Charta/issues/429) |
| [A11: Run bounded photo conversion asynchronously](https://github.com/Ikey168/Charta/issues/432) | [A09 / #430](https://github.com/Ikey168/Charta/issues/430) |
| [A12: Build review, repair and honest solvability feedback](https://github.com/Ikey168/Charta/issues/433) | [A10 / #431](https://github.com/Ikey168/Charta/issues/431), [A11 / #432](https://github.com/Ikey168/Charta/issues/432), [A07 / #428](https://github.com/Ikey168/Charta/issues/428) |

### A09: Acquire camera and picker images safely on Android

Implement camera preview/capture and permission recovery, photo picker/document fallback, URI decoding, EXIF rotation, downsampling and byte/pixel bounds before native allocation.

Acceptance: Real camera and picker photos arrive upright in review input; deny, deny permanently, cancel, missing camera, bad URI and oversized image are recoverable; no broad storage permission is needed for selected imports.

### A10: Ship the touch drawing canvas and tested symbol legend

Connect DrawCanvas with grid, pan/zoom, wall/symbol tools, color/width selection, erase, undo/redo and clear confirmation; use the frozen implemented vocabulary.

Acceptance: A user draws a three-room level with all five core object classes, undoes edits and reaches review; strokes align with touches at both phone densities; gesture cancellation never creates stray geometry.

### A11: Run bounded photo conversion asynchronously

Add photo-ingestion seam around Doodle.h with crop/corner correction, worker conversion, progress, cancellation, bounded allocations and stale-result suppression; retain original preview.

Acceptance: Camera/picker images convert without blocking UI/render threads; cancel/back/new capture discards obsolete results; degenerate input produces actionable feedback; latency and memory are recorded on the reference phone.

### A12: Build review, repair and honest solvability feedback

Expose detected geometry/symbol overlay, move/add/delete, wall corrections and undo; adapt LevelReview/LevelEditor/LevelRepair/Solver with bounded validation and preview-before-apply repair.

Acceptance: A deliberately misread photo can be corrected and played; missing start/exit and unreachable goals show actionable feedback; timeout/unsupported differs from invalid; no unknown result is described as guaranteed solvable; original drawing survives cancellation.

## [M28: Doodlebound complete demo experience](https://github.com/Ikey168/Charta/milestone/30)

Exit gate: Tutorials, eight curated levels, tour, sound, settings and accessible navigation form a coherent phone game.

| Work item | Prerequisites |
| --- | --- |
| [A13: Create onboarding and a printable drawing kit](https://github.com/Ikey168/Charta/issues/434) | [A08 / #429](https://github.com/Ikey168/Charta/issues/429), [A12 / #433](https://github.com/Ikey168/Charta/issues/433) |
| [A14: Curate eight phone levels and expose advanced mechanics](https://github.com/Ikey168/Charta/issues/435) | [A07 / #428](https://github.com/Ikey168/Charta/issues/428), [A12 / #433](https://github.com/Ikey168/Charta/issues/433) |
| [A15: Polish the dungeon presentation and touch tour mode](https://github.com/Ikey168/Charta/issues/436) | [A05 / #426](https://github.com/Ikey168/Charta/issues/426), [A07 / #428](https://github.com/Ikey168/Charta/issues/428) |
| [A16: Integrate audio, accessible settings and help](https://github.com/Ikey168/Charta/issues/437) | [A08 / #429](https://github.com/Ikey168/Charta/issues/429), [A15 / #436](https://github.com/Ikey168/Charta/issues/436) |

### A13: Create onboarding and a printable drawing kit

Adapt Tutorial into three guided lessons for move/collect/exit, danger/retry and draw/review; ship a printable three-room sheet and matching sample photo with the tested symbol legend.

Acceptance: All lessons are touch-completable, skippable and replayable; sheet and app use identical symbols; first-run help supports the acceptance usability study and works offline without camera permission.

### A14: Curate eight phone levels and expose advanced mechanics

Adapt Campaign/LaunchContent/StarRating into three tutorial plus five challenge levels, with names, order, pars and results; integrate keys/doors, switches, hazards and blocks into mobile rendering and input.

Acceptance: Every level is manually completed on a phone with recorded expected route; advanced mechanics visibly work and affect play; supported solver checks pass and their limits are recorded; progression/unlocks and retry behave correctly.

### A15: Polish the dungeon presentation and touch tour mode

Provide cohesive licensed dungeon assets, readable player/enemy motion, collection/hit/win effects and camera behavior; adapt TourCamera with touch navigation and return to play.

Acceptance: Actors and hazards remain distinguishable on small displays; camera avoids obscuring the player; tour explores a captured 3D map and returns safely; reduced-motion settings can disable excessive effects; assets carry provenance.

### A16: Integrate audio, accessible settings and help

Implement Android audio playback/focus, master/music/SFX controls, haptics toggle, reduced motion, non-color cues, scalable text, control-side choice, help and credits via localization strings.

Acceptance: Audio pauses/ducks correctly on focus changes with no duplicate playback after resume; settings immediately apply; larger fonts and cutouts do not obscure controls; cues remain understandable muted and without relying on color alone.

## [M29: Doodlebound save and share](https://github.com/Ikey168/Charta/milestone/31)

Exit gate: Relaunch preserves work and a second phone imports and plays a shared level offline.

| Work item | Prerequisites |
| --- | --- |
| [A17: Persist levels, drafts, progress and settings atomically](https://github.com/Ikey168/Charta/issues/438) | [A03 / #424](https://github.com/Ikey168/Charta/issues/424), [A08 / #429](https://github.com/Ikey168/Charta/issues/429) |
| [A18: Build the saved-level library and resume experience](https://github.com/Ikey168/Charta/issues/439) | [A12 / #433](https://github.com/Ikey168/Charta/issues/433), [A14 / #435](https://github.com/Ikey168/Charta/issues/435), [A16 / #437](https://github.com/Ikey168/Charta/issues/437), [A17 / #438](https://github.com/Ikey168/Charta/issues/438) |
| [A19: Share and import versioned levels between phones](https://github.com/Ikey168/Charta/issues/440) | [A12 / #433](https://github.com/Ikey168/Charta/issues/433), [A17 / #438](https://github.com/Ikey168/Charta/issues/438) |
| [A20: Export a paper and 3D comparison image](https://github.com/Ikey168/Charta/issues/441) | [A15 / #436](https://github.com/Ikey168/Charta/issues/436), [A18 / #439](https://github.com/Ikey168/Charta/issues/439), [A19 / #440](https://github.com/Ikey168/Charta/issues/440) |

### A17: Persist levels, drafts, progress and settings atomically

Adapt SaveSync and campaign/settings data to versioned app-private files, atomic replace, last-known-good recovery and migrations; define checkpoint semantics for process death.

Acceptance: Kill/relaunch restores committed draft/progress/settings and a safe checkpoint; truncated saves, low disk and failed writes preserve previous data and show an error; schema migration fixtures pass; raw photos remain local.

### A18: Build the saved-level library and resume experience

Add thumbnails, names, replay/edit/duplicate/delete, saved best results and storage cleanup; integrate automatic save boundaries across creation, play and settings.

Acceptance: Created and imported levels survive relaunch and can be edited/replayed; destructive deletion confirms intent; draft recovery is discoverable; deleting a level removes its owned preview files without harming other saves.

### A19: Share and import versioned levels between phones

Adapt LevelShare/LevelFormat to self-contained codes and versioned files; Android share sheet, URI grants and paste/file import; enforce schema, size and geometry bounds with file fallback for large codes.

Acceptance: Two physical phones exchange a level and reproduce its canonical gameplay data offline; corrupt, oversized and future-version payloads fail safely; cancelled sharing keeps the level; no hosted service or source photo is required.

### A20: Export a paper and 3D comparison image

Add opt-in comparison image export with accurate source/render pairing, Android sharing, temporary-file cleanup and explicit inclusion of the original drawing.

Acceptance: Exported image matches the selected level and opens in an external viewer; level-only sharing excludes the photo; cancellation leaves no persistent orphan exports; relaunch/deletion cleanup is verified.

## [M30: Doodlebound Android device quality](https://github.com/Ikey168/Charta/milestone/32)

Exit gate: Automated regression and physical-device lifecycle, capture, usability and performance acceptance pass.

| Work item | Prerequisites |
| --- | --- |
| [A21: Automate complete Android user journeys and failures](https://github.com/Ikey168/Charta/issues/442) | [A04 / #425](https://github.com/Ikey168/Charta/issues/425), [A13 / #434](https://github.com/Ikey168/Charta/issues/434), [A14 / #435](https://github.com/Ikey168/Charta/issues/435), [A18 / #439](https://github.com/Ikey168/Charta/issues/439), [A19 / #440](https://github.com/Ikey168/Charta/issues/440) |
| [A22: Qualify real-photo capture and first-use usability](https://github.com/Ikey168/Charta/issues/443) | [A13 / #434](https://github.com/Ikey168/Charta/issues/434), [A14 / #435](https://github.com/Ikey168/Charta/issues/435), [A18 / #439](https://github.com/Ikey168/Charta/issues/439) |
| [A23: Profile and tune frame pacing, memory and startup](https://github.com/Ikey168/Charta/issues/444) | [A12 / #433](https://github.com/Ikey168/Charta/issues/433), [A14 / #435](https://github.com/Ikey168/Charta/issues/435), [A15 / #436](https://github.com/Ikey168/Charta/issues/436), [A16 / #437](https://github.com/Ikey168/Charta/issues/437), [A20 / #441](https://github.com/Ikey168/Charta/issues/441) |
| [A24: Complete the physical-device and compatibility matrix](https://github.com/Ikey168/Charta/issues/445) | [A21 / #442](https://github.com/Ikey168/Charta/issues/442), [A22 / #443](https://github.com/Ikey168/Charta/issues/443), [A23 / #444](https://github.com/Ikey168/Charta/issues/444) |

### A21: Automate complete Android user journeys and failures

Add instrumentation for home/tutorial/sample play, creation fixture/review, save/import and error paths; keep portable regression tests and archive screenshots/logs tied to each APK.

Acceptance: Minimum/current-target emulator jobs pass main journeys, camera denial, picker cancel, process death and invalid import; deterministic fixtures run without external services; tests fail on broken navigation or lost saves.

### A22: Qualify real-photo capture and first-use usability

Build at least 30 owned/consented phone-photo fixtures across clean paper, skew, shadow and blur; record recognition/correction outcomes; run five first-time testers through the acceptance script.

Acceptance: Meet documented clean/grid capture and 4-of-5 onboarding targets; report all failures and input classes honestly; fix core-loop failures and rerun affected cases; attach anonymized observations and reproducible fixtures.

### A23: Profile and tune frame pacing, memory and startup

Profile reference-phone cold launch, conversion, scene load, frame times, thermal behavior, PSS and APK size; optimize batching/assets/bounded buffers and quality settings against documented budgets.

Acceptance: Attach raw traces and p95 results for the acceptance budgets; 30-minute play and 20 capture/play cycles stay within limits; worst-supported inputs are measured; budget changes require an explicit documented decision.

### A24: Complete the physical-device and compatibility matrix

Run the full script on two GPU/OS-diverse arm64 phones plus floor/current API and 16 KB environments; exercise lifecycle, input, audio, cutouts, font size, offline exchange and low storage.

Acceptance: Every acceptance row has model/OS/GPU/page size/APK hash and evidence; no crash, ANR, data loss, stuck input or core-loop blocker remains; native libraries load on 16 KB; fixes receive targeted retesting.

## [M31: Doodlebound Android demo handoff](https://github.com/Ikey168/Charta/milestone/33)

Exit gate: Signed, tested APK and checksum, symbols, licenses, installation guide, drawing sheet and video are delivered.

| Work item | Prerequisites |
| --- | --- |
| [A25: Produce a reproducibly configured signed demo APK](https://github.com/Ikey168/Charta/issues/446) | [A04 / #425](https://github.com/Ikey168/Charta/issues/425), [A24 / #445](https://github.com/Ikey168/Charta/issues/445) |
| [A26: Package the player and developer handoff documentation](https://github.com/Ikey168/Charta/issues/447) | [A13 / #434](https://github.com/Ikey168/Charta/issues/434), [A19 / #440](https://github.com/Ikey168/Charta/issues/440), [A25 / #446](https://github.com/Ikey168/Charta/issues/446) |
| [A27: Record the phone demonstration and evidence index](https://github.com/Ikey168/Charta/issues/448) | [A20 / #441](https://github.com/Ikey168/Charta/issues/441), [A24 / #445](https://github.com/Ikey168/Charta/issues/445), [A25 / #446](https://github.com/Ikey168/Charta/issues/446) |
| [A28: Sign off and hand over the complete Android demo](https://github.com/Ikey168/Charta/issues/449) | [A25 / #446](https://github.com/Ikey168/Charta/issues/446), [A26 / #447](https://github.com/Ikey168/Charta/issues/447), [A27 / #448](https://github.com/Ikey168/Charta/issues/448) |

### A25: Produce a reproducibly configured signed demo APK

Add secure release signing/versioning, artifact checksum, symbol archive and install/upgrade verification; document keystore ownership and explicit failure when signing credentials are missing.

Acceptance: Signature verifies; clean install and upgrade retain saves; APK, SHA-256, commit, version and symbols are archived; no keystore/password enters source control; final signed bytes pass phone smoke.

### A26: Package the player and developer handoff documentation

Write install/update/uninstall steps, supported-device limits, controls, capture tips, offline share instructions, troubleshooting, build reproduction and asset/dependency notices; bundle printable sheet.

Acceptance: A fresh tester installs and completes the demo using the guide; all artifact/document links resolve; license inventory covers packaged assets/libraries; guide accurately states known capture/solver limitations.

### A27: Record the phone demonstration and evidence index

Record a 60–90 second real-phone draw/capture/review/play/share video; index device runs, logs, performance traces, CI and artifact hashes in a final evidence report.

Acceptance: Video shows the distributed build and real touch/camera flow; index identifies exact tested APK and physical devices; missing evidence remains pending; raw-photo inclusion is intentional.

### A28: Sign off and hand over the complete Android demo

Run the final checklist on the exact signed artifact, triage remaining defects, publish a demo prerelease/package with APK/checksum/docs/video and record accountable sign-off.

Acceptance: All seven milestone gates pass; no install, crash, data-loss or core-loop blocker remains; final reviewer/date and evidence are recorded; artifact links work and hashes match; close the umbrella only after complete handoff.

## Completion rule

Every issue supplies the evidence its acceptance criteria require. Final sign-off uses the exact signed APK and the complete device matrix; close the umbrella after A28. Existing desktop release claims do not satisfy these gates.
