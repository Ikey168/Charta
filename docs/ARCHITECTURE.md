# Architecture map

This is the mental model for the engine: how the pieces fit, why the simulation is
deterministic and replay-verifiable, and how CI enforces all of it. For extending gameplay see
[DEVELOPER_ADD_A_MECHANIC.md](DEVELOPER_ADD_A_MECHANIC.md); to play, see
[PLAYER_GUIDE.md](PLAYER_GUIDE.md).

## The headless-core + engine-wiring model

Almost every gameplay and pipeline system is split in two:

- A **headless core** - a header-only, `std`-only module under `src/` (e.g.
  [`src/game/DungeonGame.h`](../src/game/DungeonGame.h),
  [`src/game/Solver.h`](../src/game/Solver.h),
  [`src/world/GeoJsonImporter.h`](../src/world/GeoJsonImporter.h)). It carries the rules and
  the data, has no dependency on OpenGL / GLFW / ImGui, and is unit-tested in isolation.
- **Engine wiring** - the renderer, Bullet physics, the audio subsystem, and the ImGui editor
  that drive those cores in the live build.

The payoff: the risky logic (movement, collision, solvability, rollback, CV) is exercised by
fast headless tests, while the platform-specific glue stays thin. A new mechanic is written and
tested entirely in a core header before any rendering exists.

## Determinism and replay-verifiability

The simulation is a pure function of its inputs. `DungeonGame::update(GameInput, dt)` mutates
plain data with no clocks, no RNG-by-default, and no floating-point nondeterminism beyond what
is quantized into the state digest. That gives three properties the whole game relies on:

- **Reproducible** - the same seed / input stream yields a byte-identical trajectory. The
  determinism fuzzer ([`src/game/DeterminismFuzz.h`](../src/game/DeterminismFuzz.h)) proves this
  at scale and shrinks any divergence to a minimal repro.
- **Rewindable** - the rollback core ([`src/game/DoodleRollback.h`](../src/game/DoodleRollback.h))
  hashes each state (`stateDigest`) so a client can predict, mispredict, and re-simulate.
- **Verifiable** - multiplayer results (Versus / FFA / co-op) and leaderboard runs are replays
  re-simulated and checked against a recorded digest, so a tampered run fails verification
  rather than being trusted.

The capture pipeline feeds this: a photo or map extract becomes a `SceneDescription` (walls +
`EntitySpawn`s), `loadGame` bakes it into a `DungeonGame`, and from there it is just the
deterministic loop - so a captured level is immediately solvable, playable, shareable, and
replay-verifiable.

## Data flow, end to end

```
capture (photo / OSM / SVG)                     authoring (in-engine editor)
        |                                                   |
   CV / import  --> SceneDescription (walls + EntitySpawns) <--
                                |
                          loadGame()
                                |
                          DungeonGame  --update(GameInput, dt)-->  trajectory
                          /     |      \                              |
                    Solver   Rollback   RenderData                 digest
                  (fairness) (rewind)  (draw colors)          (verify replays)
```

## CI gate topology

Correctness is enforced by a fan of workflows under `.github/workflows/`:

| Workflow | Gate |
| --- | --- |
| `ci.yml` | Full-engine build on Linux, Windows, and macOS (all required) + code-quality checks, rolled up into **Build Status**. |
| `tests.yml` | The headless unit-test suite (the header-only cores). |
| `sanitizers.yml` | The same suite under AddressSanitizer / UBSan (and the soak, #375). |
| `gl-tests.yml` | GL/audio tests under Xvfb (offscreen FBO render + readback). |
| `render-golden.yml` | Golden-image regression via EGL surfaceless render vs a committed PPM. |
| `perf.yml` | Performance-regression gate on representative scenes. |
| `analysis.yml` | CodeQL (c-cpp / python / actions). |
| `docs.yml` | Documentation structure and link checks (this file). |
| `release.yml` | Tag-driven per-platform packaging (CPack) + signed GitHub Release, with a PR dry-run. |

A change lands only when Build Status and the headless/sanitizer/GL/golden/perf gates are
green on all three platforms. macOS became a required gate again in #366 once the clang-21
toolchain was restored.
