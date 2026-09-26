# Android demo acceptance and handoff

This is a future verification plan, not a completed test report. Scope and architecture:
[demo plan](ANDROID_DEMO_PLAN.md). Work and dependencies: [backlog](ANDROID_DEMO_BACKLOG.md).

## Demonstration script (10–15 minutes)

1. Install the signed APK on a clean arm64 phone. Launch in airplane mode; see branded home,
   a working tutorial entry point and sample content without granting camera access.
2. Finish the three short tutorials: move/collect/exit, enemy avoidance/retry, draw/review.
   Pause and resume, change volume/control side, and return home without losing progress.
3. Photograph the supplied three-room paper sheet. Review walls and symbols, deliberately
   fix one detection, validate, and play the resulting 3D dungeon. Collect coins and win.
4. Cancel a capture, deny permission, import an existing photo, then create a finger drawing.
   Each route remains recoverable; processing progress is visible and cancellable.
5. Play curated challenges demonstrating a key/door, switch, hazard and block. Lose and retry;
   see correct results and saved best performance. Enter the touch tour camera.
6. Background the game, lock/unlock, recreate the Activity, and kill/relaunch the process.
   Recover the documented checkpoint/draft and settings without stuck controls or black frames.
7. Save and share the created level as a code/file. Transfer it with an offline-capable local
   transport to a second phone; import it, compare canonical level data, and play it there.
   A malformed/oversized import produces an error without replacing existing work.
8. Show the optional paper/3D comparison export, saved-level library, deletion, credits,
   version/build identifier and help. Complete a second run without a developer intervening.

## Required matrix

Record actual model, OS/API, SoC/GPU, RAM, resolution, page size, build SHA and APK hash.
Select devices in M25; do not mark rows passed until hardware evidence is attached.

| Target | Required coverage | Current evidence |
| --- | --- | --- |
| Reference midrange arm64 phone, 4 GB RAM or more | Full script, capture corpus, 30-minute thermal soak, profiling | Pending |
| Second arm64 phone, different GPU vendor and OS version | Full script, share round trip, layout/audio/lifecycle | Pending |
| Emulator at API 29 floor | Install, navigation, sample play, storage, denied/cancelled input paths | Pending |
| Emulator at chosen current target API | Automated UI smoke, process death, cutouts, font scale, screenshot evidence | Pending |
| 16 KB arm64 device/emulator | All packaged native libraries load, sample play, save/import | Pending |

Include gesture and button navigation, left/right controls, concurrent touch pointers,
picker/camera return, focus/audio interruption, low-storage save failure and invalid imports.
Headless regression tests cover the shared core; Android instrumentation covers adapters and
UI; physical phones establish touch feel, camera behavior, GPU correctness and thermal limits.

## Proposed measurable budgets

Budgets are release targets to validate in M25 and M30, not existing measurements. Record
the measurement method, sample count and device; changes require a documented scope decision.

| Measure | Target and protocol |
| --- | --- |
| Cold launch | Home interactive within 3 seconds, p95 over 20 launches on reference phone. |
| Capture to review | Within 5 seconds p95 over the accepted photo corpus; no UI-thread processing stalls. |
| Review to playable | Within 2 seconds p95 over 20 bounded levels. |
| Frame pacing | Target 60 fps; p95 frame time at most 33.3 ms on reference phone during 30-minute representative play. |
| Memory | Peak app PSS at most 350 MB through 20 capture/play cycles; retained PSS growth below 20 MB after warm-up and cleanup. |
| Distribution | arm64 demo APK at most 150 MB; all tutorial/content assets bundled and usable offline. |
| Stability | Zero crashes, ANRs, blocked navigation or lost committed saves during the script and 30-minute soak on both phones. |
| Capture quality | At least 30 real consented/test-owned photos covering clean paper, skew, shadows and blur; at least 90% of in-scope clean/grid drawings yield a playable result with at most two symbol corrections. Report failures by category; unsupported photos get actionable feedback. |
| Usability | At least 4 of 5 first-time testers reach bundled gameplay within 90 seconds and complete capture → review → play within 3 minutes using only in-app help and supplied sheet. |

M25 records maximum decoded pixels/input bytes and maximum level geometry/entity counts;
M27 enforces them before conversion and M30 profiles those upper bounds. Measure both ordinary
and worst-supported scenes; avoid reporting average fps as proof of frame pacing.

## Release checklist and evidence template

- [ ] All implementation issues complete and all milestone exits demonstrated.
- [ ] Required portable and Android CI green on the release commit.
- [ ] Both physical phones pass the full script on the signed artifact; 16 KB check passes.
- [ ] No open crash, data-loss, broken-core-loop or install blockers; lesser defects documented.
- [ ] Signed APK verifies, installs fresh and upgrades a prior demo without losing saves.
- [ ] SHA-256 checksum, version code/name, commit SHA and native symbols archived.
- [ ] Keystore remains outside source control; secure signing and repeat-install ownership documented.
- [ ] Asset/dependency licenses and notices included; no unlicensed placeholder media.
- [ ] Installation/uninstallation guide, printable sheet, known limits and 60–90 second demo video included.
- [ ] Final approver records device results, artifact links and date; umbrella closes only then.

For each run record: issue/scenario, operator/date, device details, commit and artifact hash,
steps, expected/actual result, screenshot/video/log/profile links, pass/fail and defect links.
Attach an evidence index to the release issue; the working index is
[Android evidence index](evidence/ANDROID_EVIDENCE_INDEX.md). Blank entries are pending, never passes.
The release artifact must match the tested hash; any binary change requires relevant retesting.
