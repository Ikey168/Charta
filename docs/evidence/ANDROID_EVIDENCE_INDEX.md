# Android demo evidence index

This index lists every piece of evidence for the Doodlebound Android demo (A27 / #448), and
the final sign-off record (A28 / #449). A row counts as evidence only when it names the exact
APK hash and the device it ran on. Anything not yet produced stays **Pending**. Emulator
results support the matrix but do not replace physical-phone rows.

Gate definitions: [acceptance plan](../ANDROID_DEMO_ACCEPTANCE.md). Tester and developer
guide: [handoff guide](../ANDROID_DEMO_HANDOFF.md).

## Artifacts

| Build | Version | Source commit | APK SHA-256 | Where it is kept | Role |
| --- | --- | --- | --- | --- | --- |
| Signed v1 | 1 / `0.1.0-demo` | `b66ac4c` | `49827e070177aa03ea397ff1787a1dab7c0e1e69307053adbeb171e6797463f9` | Operator machine only | Clean-install test |
| Signed v2 | 2 / `0.1.1-demo` | `b66ac4c` | `59cd1294630c45b95c139bd9f316e3a88dcc7412cc2d5993176c4d377aa009bd` | Operator machine only | Upgrade source |
| Signed v3 | 3 / `0.1.2-demo` | `f9b7377` | `d83786db9fc8a9d6752f7953137ed565336a3c813fa538f25dac593947ff2fb4` | Operator machine only | Latest signed emulator QA |
| Handoff candidate | Pending | Pending | Pending | `signed-demo` CI artifact | Final device matrix and sign-off |

The handoff candidate must come from the `signed-demo` workflow job, so its bytes, checksum
and symbols are archived by CI rather than on one machine. Its version code must be 4 or
higher so it upgrades over the builds above.

## Automated runs

| Evidence | Result | Record |
| --- | --- | --- |
| Portable headless suites, desktop builds, sanitizers, CodeQL | Green on the Android demo branch | Desktop CI checks on the pull request |
| Local API 36 emulator, connected suite | 10/10 on the signed-v3 code path, later 14 tests plus 4 direct recovery journeys | [API 36 QA](ANDROID_API36_QA.md), [JUnit XML](android-api36-tests.xml) |
| Hosted API 29 and API 36 emulator matrix | Pending. Earlier runs failed from software emulation: slow boot, package service loss, ANR. The workflow now enables KVM. | Android demo workflow runs |

## Physical-device matrix

Fill each row from `scripts/android-demo-device-report.sh <apk>` output plus the linked logs.

| Target | Model / OS / GPU / page size | APK SHA-256 | Script steps passed | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Reference midrange arm64 phone | | | | | Pending |
| Second arm64 phone, different GPU and OS | | | | | Pending |
| API 29 floor emulator | | | | | Pending (hosted run) |
| Current-target emulator | | | | | Pending (hosted run) |
| 16 KB arm64 device or emulator | | | | | Pending |

## Budgets and qualification

| Measure | Method | Result | Status |
| --- | --- | --- | --- |
| Cold launch, frame time, PSS, APK size | `scripts/android-demo-profile.sh` on the reference phone | | Pending |
| 30-minute play and 20 capture/play cycles | Profile script plus manual cycles | | Pending |
| Capture corpus, 30 or more real photos | A22 fixture set and outcome table | | Pending |
| First-use usability, 5 testers | Acceptance script, anonymized notes | | Pending |

## Demonstration video

| Item | Value |
| --- | --- |
| 60–90 second real-phone video | Pending |
| APK SHA-256 shown or stated | Pending |
| Phone model | Pending |
| Raw source photo shown on purpose | Pending decision |

## Sign-off record

Complete this only when every checklist item in the acceptance plan passes on the exact
handoff APK. Then close the umbrella issue #421.

| Field | Value |
| --- | --- |
| Handoff APK SHA-256 | |
| Version code / name | |
| Source commit | |
| Artifact link (CI) and prerelease link | |
| Open defects and their disposition | |
| Approver | |
| Date | |
