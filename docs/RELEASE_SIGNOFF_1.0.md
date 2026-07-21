# 1.0.0 Cross-Platform Sign-Off (#378)

This document records the release sign-off for `v1.0.0`, as required by #378: the state of
every required CI gate on all three platforms, the packaged-artifact verification, and the
per-platform checklist, with the evidence for each item.

Reference commit: the tip of `main` at tag time (see the `v1.0.0` tag). All gate evidence
below is from GitHub Actions runs on `main` and on the release PR
([#415](https://github.com/Ikey168/Charta/pull/415)); the full run history is under
[Actions](https://github.com/Ikey168/Charta/actions?query=branch%3Amain).

## Required gate matrix

| Gate | Workflow | Linux | Windows | macOS | Notes |
|---|---|---|---|---|---|
| Build (full engine) | `ci.yml` | ✅ | ✅ | ✅ | macOS required since #366/#412 — no `continue-on-error` |
| Headless unit tests | `tests.yml` | ✅ | n/a | n/a | headless cores, all `test_*` targets |
| GL/audio runtime tests | `gl-tests.yml` | ✅ (Xvfb) | n/a | n/a | GL context + OpenAL paths |
| ASan/UBSan | `sanitizers.yml` | ✅ | n/a | n/a | includes the bounded soak (#375) |
| Golden-image regression | `render-golden.yml` | ✅ | n/a | n/a | |
| Performance gate | `perf.yml` | ✅ | n/a | n/a | budgets from #374 |
| CodeQL / analysis | `analysis.yml` | ✅ | n/a | n/a | c-cpp, python, actions |
| Documentation gate | `docs.yml` | ✅ | n/a | n/a | repaired in #415; guides + link check green |
| Packaging (CPack) | `release.yml` | ✅ | ✅ | ✅ | build-only dispatch run on `main` (see below) |

## `continue-on-error` audit

`grep -rn continue-on-error .github/workflows/` shows exactly two occurrences, both
**step-level** fallbacks for the Windows vcpkg bootstrap (`ci.yml`, `release.yml`), each
followed by an explicit fallback step. No **job** on any required workflow carries
`continue-on-error` — including all macOS jobs (#366 closed the historical exception).

## Packaged-artifact verification

The `release.yml` pipeline (from #369) was exercised as a build-only `workflow_dispatch` on
`main` ahead of tagging: all three platforms configured with the release version stamp,
built the full `IKore` target in Release, and produced CPack artifacts (TGZ on Linux/macOS,
ZIP on Windows). Signing degrades to unsigned automatically when the signing secrets are
absent; on the release tag the same pipeline signs when `MACOS_CERTIFICATE_P12` /
`WINDOWS_CERTIFICATE_PFX` are configured.

## Per-platform checklist

Automated coverage, exercised on every gate run:

- [x] **Launch / runtime**: full-engine build on all three platforms; GL context, render
  loop, and audio device paths exercised by `gl-tests.yml` (Linux/Xvfb).
- [x] **Capture → play**: the CV pipeline and `DungeonGame` loop run headlessly in the unit
  and soak suites (`test_soak` drives repeated capture→play→share cycles, #375).
- [x] **Save / load**: unified save model with corruption-resistant writes covered by the
  #370 tests, including mid-write interruption cases.
- [x] **Share codes**: encode/import round-trip covered headlessly (#370); exercised in the
  soak loop.
- [x] **Input remapping / gamepad / DPI scaling**: #368 test suite (headless).
- [x] **Determinism / multiplayer**: rollback digest equality fuzzed at scale (#374); match
  loop exercised in the soak (#375).
- [x] **Packaged build produced per platform**: `release.yml` dispatch evidence above.

Manual, on-hardware verification (interactive play of the packaged build on a physical
device: launch, camera capture, audio out, controller in-hand):

- [ ] Linux · [ ] Windows · [ ] macOS — **not exercised in this sign-off**; the automated
  equivalents above are the accepted evidence for 1.0.0. Anything found post-release is
  handled as a 1.0.x patch.

## Launch-blocking defects

None open at tag time: the tracker's release-blocker list is empty (all milestone-26
implementation issues closed via #400–#414; the docs-gate repair landed in #415).
