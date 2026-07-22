# Game Ideas for the Charta Engine

A comprehensive candidate list for games beyond Doodlebound, written at the start of the
post-1.0 cycle. Every idea is grounded in systems the engine already ships: the capture/CV
pipeline, the OSM/GeoJSON, SVG floor-plan, and TMX importers, the deterministic
rewind/replay sim, rollback netcode, flow-field crowds, the solver/fairness gate,
behavior-tree and LLM brains, and the share-code/leaderboard/weekly-rotation loop.

Context docs: the engine thesis lives in [EXPANSION_IDEAS.md](../EXPANSION_IDEAS.md); the
Doodlebound concept and its capture moat live in
[PHONE_GAME_CONCEPT.md](../PHONE_GAME_CONCEPT.md) and
[PHONE_GAME_DESIGN.md](../PHONE_GAME_DESIGN.md). New games incubate in-tree as headless
cores following the DungeonGame/CityGame pattern described in
[ARCHITECTURE.md](ARCHITECTURE.md).

## How to read the list

Each entry names the systems it reuses. The engine-fit question for any candidate is:

- Does it use an input pipeline the engine uniquely owns (capture, maps, floor plans)?
- Does the deterministic core buy it something (fair leaderboards, async play, replays,
  rewind as a mechanic)?
- Can its logic live in a headless core under the existing CI gates (determinism fuzz,
  solver/fairness, soak, golden images)?

---

## 1. Capture-first (photo of a drawing - Doodlebound's moat)

1. **Doodle Defense** - draw the map, then tower-defense it. The headless TD mode on room
   topology already exists (#271); the smallest step to a second shipped game.
2. **Doodle Kart** - draw a racetrack, photograph it, race it. Ghost replays (#272) and
   rollback versus are built; drawn-line-to-track is a new but narrow CV problem.
3. **Paper Golf** - draw a minigolf hole, putt with Bullet physics; par/star ratings
   (#346) and the weekly rotation slot straight in. Highly shareable.
4. **Doodle Tactics** - turn-based squad tactics on drawn maps. The deterministic core
   makes async play-by-share-code trivial; the solver generalizes to fairness-checking
   encounters.
5. **Sketch Arena** - draw an arena, 3-4 player FFA in it. FFA over rollback (#318) is
   done; this is mostly content and balance.
6. **Marble Doodle** - draw ramps and walls, drop a marble, Rube Goldberg to the goal.
   Deterministic replay gives cheat-proof leaderboards for free.
7. **Doodle Habitat** - draw an enclosure, creatures live in it (crowds + behavior trees
   as a fishtank/god-game). Low mechanics, high charm.

## 2. Real-map games (OSM/GeoJSON - CityGame's lineage)

8. **Hometown Courier** - deliveries across the player's actual neighborhood; nav grid
   plus crowds as traffic and pedestrians.
9. **Outbreak: Your City** - zombie/epidemic survival where the horde is the crowd sim
   and the map is your town; rewind doubles as the "what went wrong" replay.
10. **Traffic Tycoon** - small-scope traffic/city management on real street graphs; flow
    fields are literally the tech.
11. **City Street Racer** - point-to-point racing on real roads; ghosts and the weekly
    challenge rotation reuse directly.
12. **Evacuation Commander** - serious-game angle: plan and replay crowd evacuations of
    real places. The deterministic CSV/JSON export makes it a legitimate simulation tool
    that also games well.
13. **Rooftop Runner** - parkour across extruded building footprints; stresses the
    renderer more than the sim.

## 3. Floor-plan games (SVG importer - the least-exploited pillar)

14. **The Heist** - import a floor plan, case the building, execute with guards on patrol
    routes (behavior trees), and use rewind as the core mechanic when a run goes loud.
    The best single showcase of engine identity as gameplay.
15. **Guard Duty** - the inverse: you patrol, intruders are agents; asymmetric versus
    over rollback.
16. **Escape Room Architect** - draw or import rooms, place puzzle logic (the
    keys/doors/switches vocabulary already exists), share codes as the distribution loop.
17. **Dollhouse** - ambient Sims-lite in your own home's floor plan; LLM brains give
    residents daily lives and the quest system gives them wants.
18. **Fire Drill** - evacuation puzzle in real buildings; overlaps the Evacuation
    Commander market.

## 4. Time-control-native (rewind/replay as the verb, not a feature)

19. **Ghost Cooperative** - solve levels by cooperating with your own previous runs (the
    replay system as a Braid-style mechanic).
20. **Cold Case** - a crime plays out deterministically among agents; the player scrubs
    the timeline, follows suspects, and reconstructs what happened. Uniquely buildable on
    this engine.
21. **Rewind Tactics** - real-time-with-rewind combat where undos are a budgeted
    resource.

## 5. AI-native (LLM brains + NL authoring + procedural quests)

22. **Prompt Worlds** - describe a scene in natural language, walk it, quest in it; NL
    scene authoring (#157) and procedural quests (#158) are the whole game.
23. **Living Village** - a settlement of LLM/behavior-tree NPCs whose quests emerge from
    simulated world state; the "AI town" genre with an actual deterministic sim
    underneath.
24. **AI Dungeon Master** - Doodlebound's capture plus an LLM narrator that themes,
    names, and quests the drawn dungeon. More a Doodlebound expansion than a new game.

---

## Shortlist (three slots, three pillars)

- **Fastest ship: Doodle Defense (#1)** - a mode away from existing code.
- **Best engine showcase: The Heist (#14)** - exercises the SVG pillar and makes rewind a
  mechanic, which no competitor engine can crib.
- **Biggest audience magnet: Outbreak: Your City (#9) or Hometown Courier (#8)** - "play
  in your own town" is the shareable demo the engine thesis always wanted.

All candidates incubate in-tree as headless cores behind the game-module contract
(init/tick/serialize/digest/render-list), graduating to their own repos only when they
grow store tooling, private assets, or a separate release cadence.
