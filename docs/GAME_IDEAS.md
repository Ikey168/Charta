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

For the capture-first phone concepts, one more question matters: does it have a
phone-native loop - short sessions, async social play via share codes, and a reason to
photograph a new drawing tomorrow?

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
8. **Doodle Keep** - the async raid loop: draw your own dungeon as the villain, arm it
   with the trap/enemy vocabulary, and other players raid it while you raid theirs.
   Attack/defense economies are the strongest known phone retention loop, and the
   solver/fairness gate (#316/#334) is the anti-frustration referee: a keep must be
   beatable to be published. Replays double as "watch your defense hold."
9. **Sketch Battalion** - an autobattler where the drawing is the army: unit symbols and
   formation positions come off the page, then battles resolve deterministically.
   Share codes carry whole armies; async ladders need no live opponent. The digest
   pipeline makes results provably fair.
10. **Doodle Pinball** - draw a pinball table (bumpers, lanes, flippers as symbols),
    photograph it, play it. Bullet physics plus deterministic replay leaderboards; a
    one-drawing, one-minute session that fits phones perfectly.
11. **Paper Arena Sports** - draw the pitch and its obstacles, then play air-hockey or
    2v2 soccer on it over rollback. Real-time versus (#293) and FFA are built; the
    drawing is the house-rules generator.
12. **Maze Sprint** - the purest share-code racer: draw a maze, friends speedrun it
    against your ghost. Nearly all of Doodlebound's capture path with racing instead of
    combat; weekly rotation gives it a daily-challenge cadence.
13. **Doodle Critters** - draw a creature and it comes alive: the outline becomes its
    body, drawn features map to stats, and critters battle or race async. The CV
    stretch goes beyond the fixed symbol vocabulary (arbitrary-shape capture), which
    makes it the riskiest and most magical entry in this section.
14. **Doodle Siege** - draw a castle, friends photograph counter-siege engines; volleys
    resolve in physics. Destruction simulation is the new cost; the async
    attack/defense framing reuses the Doodle Keep loop.
15. **Scribble School** - the education angle: digit/letter glyph recognition (#361)
    already exists, so "write the number to spawn that many creatures" style
    learn-to-write games are a short step. A different market with almost no new
    engine work.
16. **Party Sheet** - one photographed drawing becomes a pack of minigames (race it,
    defend it, golf it, flood-fill it). Amortizes one capture across many sessions and
    demos every mode the engine has; good pass-and-play party framing.
17. **Treasure Map** - social hide-and-seek: draw a map, hide treasure in it, share the
    code; friends explore and dig. Low-mechanics, high-sentiment; the capture pipeline
    plus fog-of-war exploration and async social plumbing that already exists.

## 2. Real-map games (OSM/GeoJSON - CityGame's lineage)

18. **Hometown Courier** - deliveries across the player's actual neighborhood; nav grid
    plus crowds as traffic and pedestrians.
19. **Outbreak: Your City** - zombie/epidemic survival where the horde is the crowd sim
    and the map is your town; rewind doubles as the "what went wrong" replay.
20. **Traffic Tycoon** - small-scope traffic/city management on real street graphs; flow
    fields are literally the tech.
21. **City Street Racer** - point-to-point racing on real roads; ghosts and the weekly
    challenge rotation reuse directly.
22. **Evacuation Commander** - serious-game angle: plan and replay crowd evacuations of
    real places. The deterministic CSV/JSON export makes it a legitimate simulation tool
    that also games well.
23. **Rooftop Runner** - parkour across extruded building footprints; stresses the
    renderer more than the sim.

## 3. Floor-plan games (SVG importer - the least-exploited pillar)

24. **The Heist** - import a floor plan, case the building, execute with guards on patrol
    routes (behavior trees), and use rewind as the core mechanic when a run goes loud.
    The best single showcase of engine identity as gameplay.
25. **Guard Duty** - the inverse: you patrol, intruders are agents; asymmetric versus
    over rollback.
26. **Escape Room Architect** - draw or import rooms, place puzzle logic (the
    keys/doors/switches vocabulary already exists), share codes as the distribution loop.
27. **Dollhouse** - ambient Sims-lite in your own home's floor plan; LLM brains give
    residents daily lives and the quest system gives them wants.
28. **Fire Drill** - evacuation puzzle in real buildings; overlaps the Evacuation
    Commander market.

## 4. Time-control-native (rewind/replay as the verb, not a feature)

29. **Ghost Cooperative** - solve levels by cooperating with your own previous runs (the
    replay system as a Braid-style mechanic).
30. **Cold Case** - a crime plays out deterministically among agents; the player scrubs
    the timeline, follows suspects, and reconstructs what happened. Uniquely buildable on
    this engine.
31. **Rewind Tactics** - real-time-with-rewind combat where undos are a budgeted
    resource.

## 5. AI-native (LLM brains + NL authoring + procedural quests)

32. **Prompt Worlds** - describe a scene in natural language, walk it, quest in it; NL
    scene authoring (#157) and procedural quests (#158) are the whole game.
33. **Living Village** - a settlement of LLM/behavior-tree NPCs whose quests emerge from
    simulated world state; the "AI town" genre with an actual deterministic sim
    underneath.
34. **AI Dungeon Master** - Doodlebound's capture plus an LLM narrator that themes,
    names, and quests the drawn dungeon. More a Doodlebound expansion than a new game.

---

## Shortlist (three slots, three pillars)

- **Fastest ship: Doodle Defense (#1)** - a mode away from existing code.
- **Best engine showcase: The Heist (#24)** - exercises the SVG pillar and makes rewind a
  mechanic, which no competitor engine can crib.
- **Biggest audience magnet: Outbreak: Your City (#19) or Hometown Courier (#18)** -
  "play in your own town" is the shareable demo the engine thesis always wanted.

Among the new phone concepts, **Doodle Keep (#8)** deserves a special mention: it is the
strongest retention loop in the capture family and nearly every system it needs (trap
vocabulary, solver gate, share codes, replays) already ships. If Doodlebound performs,
it is the natural second capture game.

All candidates incubate in-tree as headless cores behind the game-module contract
(init/tick/serialize/digest/render-list), graduating to their own repos only when they
grow store tooling, private assets, or a separate release cadence.
