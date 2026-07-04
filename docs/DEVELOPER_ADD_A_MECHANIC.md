# Developer guide: add a mechanic

This walks the full path a new gameplay mechanic travels, from the symbol a player draws to the
pixels it renders, using the **key / locked-door** mechanic (issue #319) as the worked example.
Read [ARCHITECTURE.md](ARCHITECTURE.md) first for the headless-core model.

Every step lives in a header-only core and is unit-tested before any rendering exists.

## 1. Spawn vocabulary

A captured or authored level is a `SceneDescription`: wall boxes plus a list of `EntitySpawn`
(`{type, position, yaw}`), defined in [`src/game/DoodleScene.h`](../src/game/DoodleScene.h).
Add your mechanic's spawn type(s) to the vocabulary. Keys and doors use indexed types so a key
opens the matching door: `key@1`, `lock@1`.

## 2. DungeonGame logic

In [`src/game/DungeonGame.h`](../src/game/DungeonGame.h):

- Add the data: a `struct Key { ecs::Vec3 position; int id; bool collected; }` and
  `struct LockedDoor { world::Box box; int id; bool open; }`, plus `std::vector<Key> keys;` /
  `std::vector<LockedDoor> lockedDoors;` on `DungeonGame`. Fields default to empty, so a level
  without the mechanic plays byte-identically to before.
- Parse the spawn in `loadGame()`: `detail::featureBase("key@1", id)` yields base `"key"` and
  `id = 1`; push a `Key`; `"lock"` pushes a `LockedDoor` built from `detail::featureBox`.
- Implement the rule in `update()`: when the player is within `playerRadius + keyRadius` of an
  uncollected key, mark it collected and open every `LockedDoor` with the same `id`; a closed
  door is solid in the collision test (`hitsWall` / `blocked`).

Keep it a pure function of the inputs - no clocks, no ambient RNG - so determinism holds.

## 3. CV symbol mapping

So a drawn symbol becomes the spawn: the recognizer maps an ink glyph to a spawn type. The
classifier lives in [`src/ml/GlyphNet.h`](../src/ml/GlyphNet.h) with a labeled corpus and an
accuracy gate in [`src/ml/GlyphCorpus.h`](../src/ml/GlyphCorpus.h); indexed ids (the `@1`) come
from digit recognition in [`src/game/GlyphDigits.h`](../src/game/GlyphDigits.h). Register the
new label so a recognized glyph emits the right `EntitySpawn.type`.

## 4. Solver / fairness

Every level must be beatable. [`src/game/Solver.h`](../src/game/Solver.h) grids the level
(walkable iff `hitsWall(center, playerRadius)` is false) and BFS/bitmask-searches for a clearing
path, returning `solvable` and a `par`. It evaluates doors at the start state - a **closed door
is impassable** - so a fair level never gates the only path behind a lock the solver cannot open.
The auto-repair pass ([`src/game/LevelRepair.h`](../src/game/LevelRepair.h)) and the live editor
fairness ([`src/game/LevelEditor.h`](../src/game/LevelEditor.h)) reuse the same solver.

## 5. Rollback digest

For rewind and replay-verification, the mechanic's state must fold into the state hash.
[`src/game/DoodleRollback.h`](../src/game/DoodleRollback.h)'s `stateDigest` hashes the player,
coins, and status; extend it to also fold key-collected and door-open flags, so a mispredicted
key pickup diverges the digest and triggers a resync, and a replay that lies about a door fails
verification.

## 6. Render data

Finally, give it a look. [`src/game/DungeonRenderData.h`](../src/game/DungeonRenderData.h)'s
`renderColorForType` maps a type string to a `RenderColor`; add `"key"` and `"lockeddoor"`
entries (and a colorblind-safe variant in
[`src/game/AudioAccessibility.h`](../src/game/AudioAccessibility.h)), and bind a sound cue in
`cueForEvent`. The renderer draws from this data; the core never mentions OpenGL.

## Checklist

- [ ] Spawn type in `DoodleScene` vocabulary and `loadGame` parsing.
- [ ] Data + rule in `DungeonGame`, defaulting inert when absent.
- [ ] CV label so a drawn glyph maps to the spawn.
- [ ] Solver treats it correctly; a headless test asserts a fair level stays solvable.
- [ ] State folded into the rollback digest.
- [ ] Render color + sound cue.
- [ ] A `tests/test_*.cpp` covering the rule, registered in `CMakeLists.txt`, green under the
      headless and sanitizer suites.
