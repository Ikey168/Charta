#pragma once

#include "game/Campaign.h"    // CampaignWorld, CampaignLevel
#include "game/DoodleScene.h" // LevelSpec, Wall, Symbol
#include "game/LevelFormat.h" // toLevelJson

#include <string>
#include <vector>

/**
 * @file Tutorial.h
 * @brief Authored onboarding levels that teach each mechanic in order (issue #349).
 *
 * A short, fixed sequence of hand-authored LevelSpecs that introduce the core verbs one at
 * a time - move to the exit, collect coins, use a key on a locked door (#319), trip a
 * switch to open a wall (#319), then avoid a hazard (#320) - each with ordered on-screen
 * hint prompts. tutorialWorld() packages them as a CampaignWorld so they can be wired as
 * the campaign's first world (#347): completing them unlocks the rest of the campaign.
 *
 * Every level is clearable with the classic move/collect/exit rules the Solver (#316)
 * checks, and the mechanic-specific levels are additionally playable end to end (grab the
 * key, then pass the door; trip the switch, then pass the wall; walk around the hazard).
 * Header-only, deterministic, std only.
 */
namespace IKore {
namespace game {

/// One onboarding level: its name, the level content, and ordered hint prompts.
struct TutorialLevel {
    std::string name;
    LevelSpec spec;
    std::vector<std::string> hints;
};

namespace detail {

/// A closed rectangular room outline (walls) from (x0,z0) to (x1,z1).
inline Wall roomOutline(float x0, float z0, float x1, float z1) {
    Wall w;
    w.polyline = {ecs::Vec3{x0, 0.0f, z0}, ecs::Vec3{x1, 0.0f, z0}, ecs::Vec3{x1, 0.0f, z1},
                  ecs::Vec3{x0, 0.0f, z1}, ecs::Vec3{x0, 0.0f, z0}};
    return w;
}

inline Symbol sym(const char* type, float x, float z) {
    return Symbol{type, ecs::Vec3{x, 0.0f, z}, 0.0f};
}

} // namespace detail

/// The ordered onboarding levels (one mechanic introduced per level).
inline std::vector<TutorialLevel> tutorialLevels() {
    using detail::roomOutline;
    using detail::sym;
    std::vector<TutorialLevel> out;

    // 1. Movement: just reach the exit.
    {
        TutorialLevel t;
        t.name = "First Steps";
        t.spec.walls = {roomOutline(0, 0, 12, 8)};
        t.spec.symbols = {sym("player", 2, 2), sym("exit", 10, 6)};
        t.hints = {"Use the controls to move.", "Reach the glowing exit to finish."};
        out.push_back(std::move(t));
    }
    // 2. Collecting: gather every coin, then exit.
    {
        TutorialLevel t;
        t.name = "Shiny Things";
        t.spec.walls = {roomOutline(0, 0, 12, 8)};
        t.spec.symbols = {sym("player", 2, 2), sym("coin", 6, 4), sym("coin", 9, 2), sym("exit", 10, 6)};
        t.hints = {"Collect every coin.", "The exit opens once all coins are gathered."};
        out.push_back(std::move(t));
    }
    // 3. Keys and doors: grab the key to open the locked door (#319).
    {
        TutorialLevel t;
        t.name = "Locked In";
        t.spec.walls = {roomOutline(0, -0.9f, 18, 0.9f)};
        t.spec.symbols = {sym("player", 1.5f, 0), sym("key@1", 5, 0), sym("lock@1", 10, 0),
                          sym("exit", 16, 0)};
        t.hints = {"Grab the key.", "The matching locked door opens once you hold its key."};
        out.push_back(std::move(t));
    }
    // 4. Switches and walls: step on the switch to open the wall (#319).
    {
        TutorialLevel t;
        t.name = "Flip the Switch";
        t.spec.walls = {roomOutline(0, -0.9f, 18, 0.9f)};
        t.spec.symbols = {sym("player", 1.5f, 0), sym("switch@1", 5, 0), sym("toggle@1", 10, 0),
                          sym("exit", 16, 0)};
        t.hints = {"Step on the switch.", "It opens the wall blocking your way."};
        out.push_back(std::move(t));
    }
    // 5. Hazards: go around the spikes (#320).
    {
        TutorialLevel t;
        t.name = "Watch Your Step";
        t.spec.walls = {roomOutline(0, -4, 16, 4)};
        t.spec.symbols = {sym("player", 2, 0), sym("hazard", 8, 0), sym("exit", 14, 0)};
        t.hints = {"Spikes hurt - do not touch them.", "Go around to reach the exit."};
        out.push_back(std::move(t));
    }
    return out;
}

/// Package the onboarding levels as the campaign's first world (#347). Clearing all of them
/// (requiredToAdvance == every tutorial level) unlocks the next world.
inline CampaignWorld tutorialWorld() {
    const std::vector<TutorialLevel> levels = tutorialLevels();
    CampaignWorld w;
    w.name = "Tutorial";
    w.requiredToAdvance = static_cast<int>(levels.size());
    for (std::size_t i = 0; i < levels.size(); ++i) {
        CampaignLevel cl;
        cl.id = "tut" + std::to_string(i);
        cl.name = levels[i].name;
        cl.levelJson = toLevelJson(levels[i].spec);
        w.levels.push_back(std::move(cl));
    }
    return w;
}

} // namespace game
} // namespace IKore
