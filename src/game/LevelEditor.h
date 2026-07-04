#pragma once

#include "game/DungeonGame.h" // DungeonGame, SceneDescription, EntitySpawn, loadGame, world::Box
#include "game/Solver.h"      // solve, SolveResult, SolveOptions

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * @file LevelEditor.h
 * @brief Headless, command-driven in-engine level editor (issue #363).
 *
 * The engine already turns a drawing into a playable DungeonGame; this is the other
 * direction - authoring a level cell by cell and checking, live, that it is fair. The
 * editor is a pure command layer over a grid model (a set of wall cells and a map of
 * object cells), with snapshot undo/redo, so the same logic drives an on-screen editor
 * and a headless test. game() bakes the current model into a DungeonGame (wall cells
 * become boxes, object cells become spawns) and fairness() runs the solver on it, so an
 * author sees "unsolvable" flip to "solvable" the instant they carve an opening. A level
 * round-trips through serialize()/deserialize() as stable, sorted text.
 *
 * Header-only, std only (plus the header-only game/solver cores).
 */
namespace IKore {
namespace game {

/// Grid cell coordinate (gx, gz). Objects and walls are keyed by cell.
using EditorCell = std::pair<int, int>;

/// The full editable model: everything a snapshot needs to capture for undo/redo.
struct EditorState {
    float cellWorld{2.0f};                     ///< world units per grid cell.
    std::set<EditorCell> walls;                ///< occupied wall cells.
    std::map<EditorCell, std::string> objects; ///< object spawn type per cell (player/exit/coin/...).

    bool operator==(const EditorState& o) const {
        return cellWorld == o.cellWorld && walls == o.walls && objects == o.objects;
    }
};

/**
 * @brief A command layer over an EditorState with snapshot undo/redo.
 *
 * Every mutator that actually changes the model pushes the pre-change state onto the undo
 * stack and clears the redo stack; a no-op edit (placing a wall that is already there)
 * changes nothing and records nothing, so undo/redo never step over phantom edits.
 */
class LevelEditor {
public:
    LevelEditor() = default;

    const EditorState& state() const { return m_state; }
    float cellWorld() const { return m_state.cellWorld; }
    void setCellWorld(float w) {
        if (w == m_state.cellWorld) return;
        pushUndo();
        m_state.cellWorld = w;
    }

    // --- Editing commands (return true if the model changed) ------------------

    bool placeWall(int gx, int gz) {
        const EditorCell c{gx, gz};
        if (m_state.walls.count(c)) return false;
        pushUndo();
        m_state.walls.insert(c);
        // A wall and an object never share a cell: placing a wall clears any object there.
        m_state.objects.erase(c);
        return true;
    }

    bool eraseWall(int gx, int gz) {
        const EditorCell c{gx, gz};
        if (!m_state.walls.count(c)) return false;
        pushUndo();
        m_state.walls.erase(c);
        return true;
    }

    /// Place (or retype) an object spawn at a cell. Clears any wall in that cell.
    bool placeObject(int gx, int gz, const std::string& type) {
        const EditorCell c{gx, gz};
        auto it = m_state.objects.find(c);
        if (it != m_state.objects.end() && it->second == type && !m_state.walls.count(c))
            return false;
        pushUndo();
        m_state.objects[c] = type;
        m_state.walls.erase(c);
        return true;
    }

    bool eraseObject(int gx, int gz) {
        const EditorCell c{gx, gz};
        if (!m_state.objects.count(c)) return false;
        pushUndo();
        m_state.objects.erase(c);
        return true;
    }

    void clear() {
        if (m_state.walls.empty() && m_state.objects.empty()) return;
        pushUndo();
        m_state.walls.clear();
        m_state.objects.clear();
    }

    // --- Undo / redo ----------------------------------------------------------

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    bool undo() {
        if (m_undo.empty()) return false;
        m_redo.push_back(m_state);
        m_state = m_undo.back();
        m_undo.pop_back();
        return true;
    }

    bool redo() {
        if (m_redo.empty()) return false;
        m_undo.push_back(m_state);
        m_state = m_redo.back();
        m_redo.pop_back();
        return true;
    }

    // --- Baking / evaluation --------------------------------------------------

    /// World center of a grid cell.
    ecs::Vec3 cellCenter(int gx, int gz) const {
        return ecs::Vec3{gx * m_state.cellWorld, 0.0f, gz * m_state.cellWorld};
    }

    /// Bake the current model into a playable DungeonGame: wall cells become full-cell
    /// boxes, object cells become spawns of their type at the cell center.
    SceneDescription scene() const {
        SceneDescription out;
        const float s = m_state.cellWorld;
        for (const EditorCell& w : m_state.walls) {
            world::Box b;
            b.center = ecs::Vec3{w.first * s, kWallHeight * 0.5f, w.second * s};
            b.size = ecs::Vec3{s, kWallHeight, s};
            b.yaw = 0.0f;
            out.wallBoxes.push_back(b);
        }
        for (const auto& kv : m_state.objects)
            out.spawns.push_back(EntitySpawn{kv.second, cellCenter(kv.first.first, kv.first.second), 0.0f});
        return out;
    }

    DungeonGame game() const { return loadGame(scene()); }

    /// Live fairness: run the solver on the current model.
    SolveResult fairness(const SolveOptions& opt = {}) const { return solve(game(), opt); }

    // --- Serialization (stable, sorted, round-trippable) ----------------------

    std::string serialize() const {
        std::ostringstream out;
        out << "leveleditor 1\n";
        out << "cell " << trimFloat(m_state.cellWorld) << "\n";
        for (const EditorCell& w : m_state.walls) // std::set -> sorted order
            out << "wall " << w.first << " " << w.second << "\n";
        for (const auto& kv : m_state.objects) // std::map -> sorted order
            out << "obj " << kv.first.first << " " << kv.first.second << " " << kv.second << "\n";
        return out.str();
    }

    /// Replace the model with one parsed from @p text. Undo/redo history is preserved,
    /// with the pre-load state pushed so the load itself is undoable.
    bool load(const std::string& text) {
        EditorState parsed;
        parsed.walls.clear();
        parsed.objects.clear();
        std::istringstream in(text);
        std::string tok;
        while (in >> tok) {
            if (tok == "leveleditor") {
                std::string v;
                in >> v;
            } else if (tok == "cell") {
                in >> parsed.cellWorld;
            } else if (tok == "wall") {
                int x = 0, z = 0;
                in >> x >> z;
                parsed.walls.insert({x, z});
            } else if (tok == "obj") {
                int x = 0, z = 0;
                std::string type;
                in >> x >> z >> type;
                if (!type.empty()) parsed.objects[{x, z}] = type;
            }
        }
        pushUndo();
        m_state = parsed;
        return true;
    }

    /// Build a fresh editor from serialized text (no history).
    static LevelEditor deserialize(const std::string& text) {
        LevelEditor ed;
        ed.load(text);
        ed.m_undo.clear();
        ed.m_redo.clear();
        return ed;
    }

private:
    static constexpr float kWallHeight = 3.0f;

    void pushUndo() {
        m_undo.push_back(m_state);
        m_redo.clear();
    }

    /// Compact float text ("2" not "2.000000") that still parses back exactly for the
    /// simple cell sizes an editor uses.
    static std::string trimFloat(float v) {
        std::ostringstream ss;
        ss << v;
        return ss.str();
    }

    EditorState m_state;
    std::vector<EditorState> m_undo;
    std::vector<EditorState> m_redo;
};

} // namespace game
} // namespace IKore
