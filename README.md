# Charta

[![CI/CD Pipeline](https://github.com/Ikey168/IKore-Engine/actions/workflows/ci.yml/badge.svg)](https://github.com/Ikey168/IKore-Engine/actions/workflows/ci.yml)
[![CodeQL](https://github.com/Ikey168/IKore-Engine/actions/workflows/analysis.yml/badge.svg)](https://github.com/Ikey168/IKore-Engine/actions/workflows/analysis.yml)
[![Documentation](https://github.com/Ikey168/IKore-Engine/actions/workflows/docs.yml/badge.svg)](https://github.com/Ikey168/IKore-Engine/actions/workflows/docs.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

> **From paper to playable: an engine that turns drawings, floor plans, and map
> data into living, rewindable 3D worlds.**

**Charta** (Latin for *paper* or *map*; formerly IKore Engine) is a C++17 / OpenGL
engine built around a data-oriented (archetype) Entity-Component System. On top of
a conventional renderer + physics + audio stack, it does something general-purpose
engines do not ship out of the box: it **turns 2D and real-world data into
navigable 3D scenes** - OpenStreetMap / GeoJSON, tilemaps, SVG floor plans, and
even a **photo of a hand-drawn map** (the computer-vision pipeline behind
Doodlebound) - then **simulates crowds of agents** with **deterministic,
rewindable, replay-verifiable time control**, driven from an **in-engine ImGui
editor**.

> The C++ namespace remains `IKore::` and the repository slug is unchanged for
> now; the rebrand is source-compatible.

The end-to-end proof of that identity is the vertical slice
([`src/game/VerticalSlice.h`](src/game/VerticalSlice.h)): import a neighborhood,
bake a nav grid, send a crowd to a goal, then rewind time and replay it
deterministically. Run it in one command (see [Samples](#samples)).

The full thesis and roadmap are in
[`EXPANSION_IDEAS.md`](EXPANSION_IDEAS.md); the consumer flagship concept,
**Doodlebound** (draw a dungeon on paper, photograph it, play it in 3D), is in
[`PHONE_GAME_CONCEPT.md`](PHONE_GAME_CONCEPT.md) and
[`PHONE_GAME_DESIGN.md`](PHONE_GAME_DESIGN.md).

---

## Feature matrix

"Tested" means the system has unit tests that run headless (built and exercised
under AddressSanitizer / UBSan); the GL/runtime systems are compiled in CI.

| System | Status | Notes |
|---|---|---|
| Data-oriented ECS | Implemented, tested | Archetype / SoA storage, generational handles, cache-friendly views (`src/core/ecs`) |
| In-engine editor & tooling (ImGui) | Implemented, tested | Perf overlay, debug console, HUD framework, entity inspector, viewport picking, menus + persisted settings, scene hierarchy, input remapping, DPI/UI scaling, benchmarking, log viewer (`src/ui`) |
| World-from-data importers | Implemented, tested | GeoJSON / OpenStreetMap, TMX tilemaps, SVG floor plans (`src/world`) |
| Navigation | Implemented, tested | Nav-grid bake, A* pathfinding, flow fields (`src/world`, `src/core/sim`) |
| Crowd simulation | Implemented, tested | Flow-field agents on the ECS hot path (`src/core/sim/Crowd.h`) |
| Deterministic time control | Implemented, tested | Fixed-step, snapshot rewind/replay, deterministic RNG (`src/core/sim`) |
| Rollback netcode | Implemented, tested | Rollback + desync detection (`src/net`) |
| AI | Implemented, tested | Behavior trees, steering/NPCs, LLM-driven brain, quest + scene authoring (`src/ai`) |
| Scripting | Implemented, tested | Lua (sol2) bound to the ECS + events, with hot reload (`src/scripting`) |
| Data export | Implemented, tested | Deterministic CSV / JSON exporter (`src/core/sim/DataExport.h`) |
| Vertical slice | Implemented, tested | Import -> nav -> crowd -> rewind, end to end (`src/game/VerticalSlice.h`) |
| Rendering | Implemented | Forward renderer, directional + point shadow maps, frustum culling, GPU particles, bloom / SSAO / FXAA, skybox (`src/shaders`) |
| Physics | Implemented | Rigid bodies via Bullet3 |
| Audio | Implemented | OpenAL 3D positional audio (with a no-OpenAL fallback) |
| Animation | Implemented | Skeletal animation via Assimp |

Per-subsystem implementation notes live in [`docs/`](docs/).

---

## Getting started

Charta uses CMake. GLFW, GLAD, GLM, stb_image, Assimp, Bullet, Dear ImGui, Lua, and
sol2 are downloaded automatically at configure time via `FetchContent`. OpenAL is
located via your system package manager (`pkg-config`).

```bash
# From the repository root
mkdir build && cd build
cmake ..
cmake --build .
```

> **Linux audio prerequisite:** install OpenAL development headers, e.g.
> `sudo apt-get install libopenal-dev` (Debian/Ubuntu).

On Windows use `-G "Visual Studio 17 2022"`; on macOS use `-G Xcode`.

### Run the tests

The CMake build produces a `test_*` executable per subsystem (for example
`test_vertical_slice`, `test_ecs_query`, `test_menu_system`); run them directly.
The header-only cores also compile stand-alone with just an include path, which is
the fastest way to iterate:

```bash
g++ -std=c++17 -I src tests/test_vertical_slice.cpp -o test_vertical_slice
./test_vertical_slice
```

### Samples

Small, self-contained programs that run headless (no GL context) live in
[`samples/`](samples/):

```bash
cmake --build . --target sample_vertical_slice
./sample_vertical_slice
```

- `sample_vertical_slice` - the vertical slice end to end (import -> crowd -> rewind).
- `sample_benchmark_export` - capture FPS/frame-time/memory and export CSV + JSON.

See [`samples/README.md`](samples/README.md) for details.

---

## Tech stack

- **Language:** C++17
- **Graphics:** OpenGL, [GLFW](https://github.com/glfw/glfw),
  [GLAD](https://github.com/Dav1dde/glad), [GLM](https://github.com/g-truc/glm)
- **UI:** [Dear ImGui](https://github.com/ocornut/imgui) (docking)
- **Physics:** [Bullet3](https://github.com/bulletphysics/bullet3)
- **Audio:** OpenAL
- **Scripting:** [Lua](https://www.lua.org/) + [sol2](https://github.com/ThePhd/sol2)
- **Model/animation loading:** [Assimp](https://github.com/assimp/assimp) (OBJ, FBX)
- **Image loading:** [stb_image](https://github.com/nothings/stb)
- **Build:** CMake (dependencies fetched automatically via `FetchContent`)

---

## Project structure

```
src/
  core/          Engine core: ECS (core/ecs), sim (core/sim), events, logging, transforms
    components/  Class-based component implementations
    ecs/         Data-oriented archetype ECS + components/systems
    sim/         Fixed-step simulation, timeline/rewind, crowd, data export, state hashing
  ui/            In-engine ImGui editor & tooling (Milestone 9) and reusable UI cores
  world/         World-from-data importers (GeoJSON, TMX, SVG) and nav mesh
  ai/            Behavior trees, steering/NPCs, LLM-driven brain, quest/scene authoring
  net/           Rollback netcode and desync detection
  scripting/     Lua scripting layer (sol2) with hot reload
  audio/         OpenAL 3D audio engine and ambient zones
  scene/         Scene graph & scene management
  shaders/       GLSL shaders (shadows, bloom, SSAO, FXAA, particles, ...)
  game/          Higher-level composition (the vertical slice, sample games)
  main.cpp       Entry point / sample application
tests/           Unit tests (header-only cores; built by CMake as test_* targets)
samples/         Small runnable examples (built as sample_* targets)
docs/            Per-subsystem implementation notes
assets/          Models, textures, and other runtime assets
```

---

## Roadmap & vision

Charta's direction is to differentiate around **world-from-data**:

1. **Foundations** - a data-oriented (archetype) ECS, scripting + hot reload, and an
   in-engine ImGui editor. (Implemented.)
2. **Flagship importer** - turn 2D/vector inputs (SVG floor plans, tilemaps, GeoJSON /
   OpenStreetMap) into 3D scenes with collision and navigation. (Implemented.)
3. **Simulation & AI** - large-scale agent simulation with time control
   (pause / rewind / replay) and an AI-native authoring/NPC layer. (Implemented.)

The full strategy and rationale are in
[`EXPANSION_IDEAS.md`](EXPANSION_IDEAS.md),
[`PHONE_GAME_CONCEPT.md`](PHONE_GAME_CONCEPT.md), and
[`PHONE_GAME_DESIGN.md`](PHONE_GAME_DESIGN.md).

---

## Documentation

Top-level guides for the two audiences a 1.0 needs, plus per-header docs throughout `src/`:

- **[Player guide](docs/PLAYER_GUIDE.md)** - capture, play, share, and compete (campaign,
  weekly, versus/FFA/co-op, editor, accessibility).
- **[Developer guide: add a mechanic](docs/DEVELOPER_ADD_A_MECHANIC.md)** - the full path from
  a drawn symbol to rendered pixels (spawn vocabulary -> `DungeonGame` -> CV -> solver ->
  rollback digest -> render data), worked through the key/door mechanic.
- **[Architecture map](docs/ARCHITECTURE.md)** - the headless-core + engine-wiring model,
  determinism/replay-verifiability, and the CI gate topology.

---

## Contributing

Issues and pull requests are welcome. Development happens against feature branches;
CI (build, CodeQL analysis, and docs) runs on every push. Please keep new code
consistent with the surrounding style and add a test under `tests/` where it makes
sense - the header-only cores are designed to be unit-testable without a GL context.

---

## License

Released under the [MIT License](LICENSE). (c) 2025 Ikey168.
