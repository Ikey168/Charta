#pragma once

#include "core/ecs/Registry.h"
#include "core/ecs/View.h"
#include "core/ecs/components/Components.h"
#include "game/DungeonGame.h" // DungeonGame, world::Box, SpawnTag (via DoodleScene)

#include <string>
#include <vector>

/**
 * @file DungeonRenderData.h
 * @brief Build the renderer's draw list for a live DungeonRuntime (issue #333).
 *
 * DungeonRuntime (the engine-wiring bridge) spawns the actors as ECS Transform+SpawnTag
 * entities; the static level features (walls, doors, switches, hazards, keys) live on the
 * DungeonGame. This turns both into a flat, renderer-agnostic draw list - one colored,
 * sized instance per thing to draw - so a render pass can iterate it and issue one draw per
 * instance. Keeping this as data makes the "what to draw" logic unit-testable without a GL
 * context (the actual GL draw consumes the list); the accompanying gl-labelled tool
 * renders it offscreen.
 *
 * Collected coins disappear because their ECS entity is destroyed by the runtime; a
 * collected key / opened door / flipped toggle-wall disappears because it is read from the
 * game's live state. Header-only, std + the header-only ECS / game.
 */
namespace IKore {
namespace game {

struct RenderColor {
    float r{0.8f}, g{0.8f}, b{0.8f};
};

struct RenderInstance {
    ecs::Vec3 position{}; ///< world center (XZ; y is the box center for statics).
    ecs::Vec3 size{};     ///< world size (statics use the box size; actors a marker size).
    RenderColor color{};
    std::string type;     ///< "wall"/"player"/"enemy"/"coin"/"exit"/"key"/... (for material choice).
};

struct RenderStyle {
    float actorSize{0.8f};   ///< marker size for point actors/features.
    RenderColor wall{0.50f, 0.50f, 0.55f};
};

/// A deterministic per-type color for a spawn/feature type (default material palette).
inline RenderColor renderColorForType(const std::string& type) {
    if (type == "player") return {0.20f, 0.80f, 0.35f};
    if (type == "enemy" || (type.size() >= 5 && type.compare(0, 5, "enemy") == 0))
        return {0.85f, 0.20f, 0.20f};
    if (type == "coin" || type == "treasure") return {0.92f, 0.85f, 0.20f};
    if (type == "exit" || type == "door") return {0.25f, 0.45f, 0.92f};
    if (type == "key") return {0.95f, 0.80f, 0.20f};
    if (type == "lockeddoor" || type == "lock") return {0.60f, 0.40f, 0.20f};
    if (type == "switch") return {0.40f, 0.85f, 0.90f};
    if (type == "toggle" || type == "togglewall") return {0.55f, 0.55f, 0.62f};
    if (type == "hazard" || type == "spike") return {0.90f, 0.30f, 0.10f};
    return {0.80f, 0.80f, 0.80f};
}

/**
 * @brief Build the draw list: static level geometry/features from @p game, plus the live
 *        actors (player/enemies/coins/exit) from their ECS Transform+SpawnTag entities in
 *        @p reg. Only still-relevant features are emitted (a locked door that has opened, a
 *        toggle wall that is off, a collected key, and a collected coin's destroyed entity
 *        are all omitted).
 */
inline std::vector<RenderInstance> buildRenderList(ecs::Registry& reg, const DungeonGame& game,
                                                   const RenderStyle& style = {}) {
    std::vector<RenderInstance> out;
    const ecs::Vec3 marker{style.actorSize, style.actorSize, style.actorSize};

    // Static geometry / features from the game.
    for (const world::Box& b : game.walls) out.push_back({b.center, b.size, style.wall, "wall"});
    for (const LockedDoor& d : game.lockedDoors) {
        if (!d.open) out.push_back({d.box.center, d.box.size, renderColorForType("lockeddoor"), "lockeddoor"});
    }
    for (const ToggleWall& w : game.toggleWalls) {
        if (w.solid) out.push_back({w.box.center, w.box.size, renderColorForType("togglewall"), "togglewall"});
    }
    for (const Key& k : game.keys) {
        if (!k.collected) out.push_back({k.position, marker, renderColorForType("key"), "key"});
    }
    for (const Switch& s : game.switches) {
        out.push_back({s.position, marker, renderColorForType("switch"), "switch"});
    }
    for (const Hazard& h : game.hazards) {
        out.push_back({h.position, marker, renderColorForType("hazard"), "hazard"});
    }

    // Live actors from the ECS (coins vanish when their entity is destroyed on pickup).
    reg.view<ecs::Transform, SpawnTag>().each([&](ecs::Entity, ecs::Transform& t, SpawnTag& tag) {
        out.push_back({t.position, marker, renderColorForType(tag.type), tag.type});
    });
    return out;
}

/// Count the instances of a given type in a draw list (for tests / diagnostics).
inline int countRenderType(const std::vector<RenderInstance>& list, const std::string& type) {
    int n = 0;
    for (const RenderInstance& i : list) {
        if (i.type == type) ++n;
    }
    return n;
}

} // namespace game
} // namespace IKore
