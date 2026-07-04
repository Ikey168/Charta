#pragma once

#include "core/ecs/Registry.h"
#include "game/DungeonRenderData.h" // RenderColor, renderColorForType
#include "game/DungeonRuntime.h"    // DungeonRuntime::DecorateFn

#include <string>

/**
 * @file DungeonDecor.h
 * @brief Per-type mesh + material selection for the live renderer (issue #350).
 *
 * The render pass (#333) draws flat colored quads. The DungeonRuntime decorate hook (#332)
 * lets the live engine attach real render components per spawn; this supplies the "what
 * mesh, what material" decision as renderer-agnostic data so it stays headless-testable
 * (like DungeonRenderData). meshDecorator() returns a DecorateFn that attaches a RenderKind
 * (a distinct primitive mesh + the per-type material color from #333) to each spawned
 * entity, so the GL render pass can draw a recognizable per-type mesh from the entity's
 * Transform every frame. A collected coin's entity is destroyed by the runtime, so it stops
 * being drawn, exactly as before.
 *
 * Header-only, std + the header-only ECS / game. The GL draw of these kinds lives in the
 * gl-labelled tool; the headless render-data core (DungeonRenderData) is unchanged.
 */
namespace IKore {
namespace game {

/// A distinct primitive mesh per spawn/feature family (the "recognizable" shape).
enum class MeshKind { Cube, Sphere, Pyramid, Prism, Disc, Diamond };

/// The mesh a spawn/feature type draws as. Distinct for the core actor types so a level is
/// readable at a glance; related features reuse a fitting shape.
inline MeshKind meshKindForType(const std::string& type) {
    if (type == "player") return MeshKind::Sphere;
    if (type == "enemy" || (type.size() >= 5 && type.compare(0, 5, "enemy") == 0))
        return MeshKind::Pyramid;
    if (type == "coin" || type == "treasure") return MeshKind::Disc;
    if (type == "exit" || type == "door") return MeshKind::Prism;
    if (type == "key") return MeshKind::Diamond;
    if (type == "switch") return MeshKind::Disc;
    if (type == "hazard" || type == "spike") return MeshKind::Pyramid;
    // walls, locked doors, and toggle walls are blocky.
    return MeshKind::Cube;
}

/// Render components attached per entity by the decorator: a mesh kind + material color +
/// a marker size. Renderer-agnostic so it is headlessly testable.
struct RenderKind {
    MeshKind kind{MeshKind::Cube};
    RenderColor color{};
    float size{0.8f};
};

/**
 * @brief A DungeonRuntime decorate hook that attaches a RenderKind (mesh + material) to each
 *        spawned entity by its type. Pass to DungeonRuntime::spawnInto so the live engine /
 *        GL render pass can draw a real per-type mesh instead of a flat quad.
 */
inline DungeonRuntime::DecorateFn meshDecorator(float actorSize = 0.8f) {
    return [actorSize](ecs::Registry& reg, ecs::Entity e, const std::string& type) {
        reg.add<RenderKind>(e, RenderKind{meshKindForType(type), renderColorForType(type), actorSize});
    };
}

} // namespace game
} // namespace IKore
