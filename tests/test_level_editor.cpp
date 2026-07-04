// In-engine level editor - issue #363 (headless command layer).
//
// Verifies the editor's core promises: editing commands mutate a grid model; the solver
// runs live on the baked game so an author watches "unsolvable" flip to "solvable" the
// instant they carve an opening; snapshot undo/redo restores exact prior state and skips
// no-op edits; and a level round-trips through serialize()/deserialize() as stable text.
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_level_editor.cpp -o test_level_editor
//
// (Depends on the header-only game/solver cores; no engine link.)

#include "game/LevelEditor.h"

#include <cstdio>
#include <string>

using namespace IKore;
using namespace IKore::game;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

// Wall in every cell of the 8-neighbourhood around (cx, cz) - a full ring that seals the
// centre cell against a 4-connected flood (no orthogonal or diagonal leak).
static void ringAround(LevelEditor& ed, int cx, int cz) {
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            if (dx != 0 || dz != 0) ed.placeWall(cx + dx, cz + dz);
}

int main() {
    // 1. Live fairness flips unsolvable -> solvable when an opening is carved.
    {
        LevelEditor ed;
        ed.placeObject(0, 0, "player");
        ed.placeObject(2, 0, "coin");
        ed.placeObject(5, 0, "exit");
        ringAround(ed, 5, 0); // fully wall in the exit

        const SolveResult before = ed.fairness();
        CHECK(!before.solvable);         // exit is walled off
        CHECK(!before.exitReachable);

        // Carve the wall between the exit and the corridor: erase the left neighbour.
        const bool changed = ed.eraseWall(4, 0);
        CHECK(changed);

        const SolveResult after = ed.fairness();
        CHECK(after.solvable);           // now the exit (and the coin) are reachable
        CHECK(after.exitReachable);
        CHECK(after.totalCoins == 1 && after.reachableCoins == 1);
    }

    // 2. A no-op edit records no history; a real edit does.
    {
        LevelEditor ed;
        CHECK(!ed.canUndo());
        CHECK(ed.placeWall(1, 1));       // real edit
        CHECK(ed.canUndo());
        CHECK(!ed.placeWall(1, 1));      // duplicate: no change
        // Undo once returns to empty (the duplicate did not push a snapshot).
        CHECK(ed.undo());
        CHECK(ed.state().walls.empty());
        CHECK(!ed.canUndo());
    }

    // 3. Undo/redo restore exact state across a sequence of edits.
    {
        LevelEditor ed;
        ed.placeObject(0, 0, "player");
        ed.placeObject(1, 0, "coin");
        const std::string checkpoint = ed.serialize();

        ed.placeObject(3, 0, "exit");
        ed.placeWall(2, 0);
        const std::string later = ed.serialize();
        CHECK(checkpoint != later);

        // Two undos peel off the exit and the wall, back to the checkpoint.
        CHECK(ed.undo());
        CHECK(ed.undo());
        CHECK(ed.serialize() == checkpoint);

        // Redo walks forward to the later state.
        CHECK(ed.redo());
        CHECK(ed.redo());
        CHECK(ed.serialize() == later);
        CHECK(!ed.canRedo());

        // A fresh edit after undo drops the redo branch.
        CHECK(ed.undo());
        CHECK(ed.canRedo());
        ed.placeObject(9, 9, "coin");
        CHECK(!ed.canRedo());
    }

    // 4. Retyping / overwriting: an object cell and a wall cell are mutually exclusive.
    {
        LevelEditor ed;
        ed.placeObject(4, 4, "coin");
        CHECK(ed.placeWall(4, 4));                        // wall replaces the object
        CHECK(ed.state().walls.count({4, 4}) == 1);
        CHECK(ed.state().objects.count({4, 4}) == 0);
        CHECK(ed.placeObject(4, 4, "enemy"));             // object replaces the wall
        CHECK(ed.state().walls.count({4, 4}) == 0);
        CHECK(ed.state().objects.at({4, 4}) == "enemy");
    }

    // 5. serialize()/deserialize() round-trips to identical text and identical fairness.
    {
        LevelEditor ed;
        ed.placeObject(0, 0, "player");
        ed.placeObject(1, 0, "coin");
        ed.placeObject(2, 0, "key@1");
        ed.placeObject(6, 0, "exit");
        ed.placeWall(3, 0);
        ed.placeWall(3, 1);
        ed.placeWall(3, -1);

        const std::string text = ed.serialize();
        LevelEditor round = LevelEditor::deserialize(text);
        CHECK(round.serialize() == text);
        CHECK(round.state() == ed.state());
        CHECK(!round.canUndo()); // deserialize starts with no history

        const SolveResult a = ed.fairness();
        const SolveResult b = round.fairness();
        CHECK(a.solvable == b.solvable);
        CHECK(a.exitReachable == b.exitReachable);
        CHECK(a.totalCoins == b.totalCoins);
    }

    // 6. Baking maps cells to world centers at the configured cell size.
    {
        LevelEditor ed;
        ed.setCellWorld(2.0f);
        ed.placeObject(3, -2, "player");
        const DungeonGame g = ed.game();
        CHECK(g.playerPosition.x == 6.0f);   // 3 * 2.0
        CHECK(g.playerPosition.z == -4.0f);  // -2 * 2.0
    }

    if (g_failures == 0) {
        std::printf("test_level_editor: all checks passed\n");
        return 0;
    }
    std::printf("test_level_editor: %d check(s) failed\n", g_failures);
    return 1;
}
