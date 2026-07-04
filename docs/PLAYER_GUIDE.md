# Player guide

Doodlebound turns a drawing into a game you can play, share, and compete on. This is how.

## Capture

Draw a level on paper - walls as lines, and symbols for the pieces:

- a **triangle** is your start, a **square** is the exit, a **circle** is a coin;
- an **X** is an enemy, a **+** is a key (`+1` opens door `1`), a bar is a switch;
- spikes are hazards, a box is a pushable block.

Photograph it (or import an image, an OpenStreetMap area, or an SVG floor plan). The computer
vision pipeline binarizes the ink, vectorizes the walls, recognizes the symbols, and builds a
playable level. Every captured level is checked for **fairness** - it is guaranteed solvable
before you play, and can be auto-repaired if a symbol landed somewhere unreachable.

## Play

Move with **WASD** or a **gamepad** left stick (rebindable in settings). Collect every coin,
then reach the exit to win; avoid enemies and hazards. Keys open the doors with the matching
number, switches flip toggle walls, and you can push blocks to clear a path.

The **campaign** is a difficulty-ramped set of hand-tuned levels that introduce each mechanic in
turn. A rotating **weekly challenge** picks a level for everyone that week, so scores are
comparable.

## Share

Any level - captured or built in the in-engine **editor** - becomes a short **share code**.
Send the code; anyone can import it and get the byte-identical level. Your **replays** share the
same way, so you can send someone the exact run, not just the score.

## Compete

- **Leaderboards** rank the fastest clears; every submitted run is a replay that is
  re-simulated and verified, so times are cheat-resistant.
- **Versus** and **FFA** race players through the same level in real time over a rollback
  netcode; **co-op** shares one world across several players.
- Because the simulation is deterministic and every result is a verified replay, a spectator can
  scrub any match or run frame by frame.

## Accessibility

Independent **master / music / SFX** volumes and a mute; **colorblind-safe palettes**
(deuteranopia / protanopia / tritanopia) that keep every piece distinguishable by brightness,
not just hue; DPI-aware UI scaling; and reduced-motion and cue-subtitle options - all in
settings, and all persisted. The interface is localizable through a string table.

For how the engine is built, see [ARCHITECTURE.md](ARCHITECTURE.md); to extend it, see
[DEVELOPER_ADD_A_MECHANIC.md](DEVELOPER_ADD_A_MECHANIC.md).
