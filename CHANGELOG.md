# Changelog

All notable changes to Doodlebound (the IKore engine) are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The single source of truth for the
version is `project(IKoreEngine VERSION ...)` / `IKORE_RELEASE_VERSION` in `CMakeLists.txt`,
surfaced to code through the generated `ikore/Version.h`.

## [Unreleased]

Development on the 1.0.x line (`1.0.1-dev`). Nothing user-facing yet.

## [1.0.0] - 2026-07-21

The 1.0 release: the capture-to-play engine, multiplayer, the content pipeline, and the
platform/ship work are feature-complete. Everything below stabilized through the
`1.0.0-rc.1` bug-fix-only freeze and ships unchanged in 1.0.0, plus the release-window
fixes recorded under "Fixed".

### Added

- **Capture & CV**: labeled glyph corpus with a recognition-accuracy gate (#364); richer
  OSM/GeoJSON import into CityGame - region kinds, POIs, and barriers become hazards, coins,
  and walls (#365); digit-glyph recognition for `@id` feature links (#361); solver-guided
  auto-repair of unsolvable captured levels (#362).
- **Authoring**: an in-engine level editor with live solver fairness (#363); a solver-oracle
  procedural level generator (#348) and a launch campaign of fairness-validated levels with a
  deterministic weekly rotation (#371).
- **Platform & ship**: rebindable input, gamepad support, and DPI/safe-area UI scaling (#368);
  a unified save model with corruption-resistant writes, cloud-sync (last-writer-wins), and
  share/import (#370); per-platform CPack packaging with a signed-release pipeline and a PR
  dry-run (#369); the portable mobile core - touch controls and a GLES render path (#367).
- **Polish & balance**: event-driven audio cues, a volume/mute mix, and colorblind-safe
  palettes with a contrast gate (#372); a localization string table with fallback, formatting,
  and a completeness check (#373); seeded determinism fuzzing with minimal-repro shrinking
  (#374); a bounded soak harness for long-session stability under sanitizers (#375).
- **Release**: a single version source surfaced through `ikore/Version.h`, consumed by the
  build stamp, packaging, and the about screen (#376); this changelog.

### Changed

- **macOS is a required build gate again** (#366): the clang-21 toolchain blocker (#338) is
  resolved by patching sol2 v3.3.0's bundled `optional<T&>` at configure time, so all three
  platforms compile the full engine and are required.

### Fixed

- macOS builds of the scripting layer under Xcode 26 / clang 21 (sol2 `optional<T&>::emplace`).
- The `docs.yml` documentation gate: the workflow file was invalid YAML (an unindented
  heredoc) and had failed at startup on every run since it landed, so the #377 doc checks
  never executed. The link checker also now skips fenced code blocks so C++ lambdas in the
  implementation notes are not misread as markdown links (#415).

[Unreleased]: https://github.com/Ikey168/Charta/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Ikey168/Charta/releases/tag/v1.0.0
