#pragma once

#include "core/ecs/Registry.h"
#include "core/ecs/View.h"
#include "core/ecs/components/Components.h"
#include "game/DungeonGame.h" // DungeonGame, GameInput, SpawnTag (via DoodleScene)

#include <functional>
#include <string>
#include <vector>

/**
 * @file DungeonRuntime.h
 * @brief Drive the headless DungeonGame through the engine's live ECS (issue #313-#320
 *        follow-on: wire the deterministic cores into the running engine).
 *
 * The DungeonGame (#164) and everything built on it - the GeoJSON city importer (#313),
 * recognized-drawing spawns (#314/#315), keys/doors/switches/hazards (#319) and enemy
 * behaviors (#320) - are pure headless data. This is the seam that makes them "live":
 * spawnInto() creates one ECS entity per actor (player, enemies, coins, exit) with a
 * Transform + SpawnTag, and update() steps the deterministic sim and writes each actor's
 * position back onto its Transform - exactly what the renderer and the ECS systems read to
 * draw and reason about the world. A collected coin's entity is destroyed so it stops
 * being drawn.
 *
 * A caller-supplied @c decorate hook lets the live engine attach Mesh/Material/Model
 * components per spawn (by its type string) while keeping this header renderer-agnostic
 * and headlessly testable. Header-only, std + the header-only ECS / game.
 */
namespace IKore {
namespace game {

class DungeonRuntime {
public:
    /// Hook to attach render/engine components to a freshly-created entity, keyed by the
    /// spawn's type ("player" / "enemy" / "coin" / "exit"). Optional; tests pass none.
    using DecorateFn = std::function<void(ecs::Registry&, ecs::Entity, const std::string&)>;

    DungeonRuntime() = default;
    explicit DungeonRuntime(DungeonGame game) : m_game(std::move(game)) {}

    DungeonGame& game() { return m_game; }
    const DungeonGame& game() const { return m_game; }

    ecs::Entity playerEntity() const { return m_player; }
    const std::vector<ecs::Entity>& enemyEntities() const { return m_enemies; }
    const std::vector<ecs::Entity>& coinEntities() const { return m_coins; }
    ecs::Entity exitEntity() const { return m_exit; }
    bool hasExitEntity() const { return m_hasExit; }

    /// Create the ECS entities that mirror the game's actors. Idempotent per instance
    /// (call once). @p decorate, if set, is invoked per new entity for the live engine
    /// to attach meshes/materials.
    void spawnInto(ecs::Registry& reg, const DecorateFn& decorate = {}) {
        m_player = make(reg, m_game.playerPosition, "player", decorate);

        m_enemies.clear();
        for (const Enemy& e : m_game.enemies) {
            m_enemies.push_back(make(reg, e.position, "enemy", decorate));
        }

        m_coins.assign(m_game.coins.size(), ecs::Entity{});
        for (std::size_t i = 0; i < m_game.coins.size(); ++i) {
            m_coins[i] = make(reg, m_game.coins[i].position, "coin", decorate);
        }

        if (m_game.hasExit) {
            m_exit = make(reg, m_game.exitPosition, "exit", decorate);
            m_hasExit = true;
        }
    }

    /// Step the deterministic sim by @p dt with @p in, then sync every live actor's
    /// Transform and destroy any entity for a coin collected this step.
    void update(ecs::Registry& reg, const GameInput& in, float dt) {
        m_game.update(in, dt);
        syncTransforms(reg);
    }

    /// Re-sync transforms without stepping (e.g. after an external state change).
    void syncTransforms(ecs::Registry& reg) {
        setPosition(reg, m_player, m_game.playerPosition);
        for (std::size_t i = 0; i < m_enemies.size() && i < m_game.enemies.size(); ++i) {
            setPosition(reg, m_enemies[i], m_game.enemies[i].position);
        }
        for (std::size_t i = 0; i < m_coins.size() && i < m_game.coins.size(); ++i) {
            if (m_game.coins[i].collected && reg.isValid(m_coins[i])) {
                reg.destroy(m_coins[i]); // collected: stop drawing it
            }
        }
    }

private:
    ecs::Entity make(ecs::Registry& reg, const ecs::Vec3& pos, const std::string& type,
                     const DecorateFn& decorate) {
        const ecs::Entity e = reg.create();
        reg.add<ecs::Transform>(e, ecs::Transform{pos, ecs::Vec3{}, ecs::Vec3{1.0f, 1.0f, 1.0f}});
        reg.add<SpawnTag>(e, SpawnTag{type});
        if (decorate) decorate(reg, e, type);
        return e;
    }

    static void setPosition(ecs::Registry& reg, ecs::Entity e, const ecs::Vec3& pos) {
        if (reg.isValid(e) && reg.has<ecs::Transform>(e)) reg.get<ecs::Transform>(e).position = pos;
    }

    DungeonGame m_game;
    ecs::Entity m_player{};
    std::vector<ecs::Entity> m_enemies;
    std::vector<ecs::Entity> m_coins; ///< parallel to m_game.coins (invalid once destroyed).
    ecs::Entity m_exit{};
    bool m_hasExit{false};
};

} // namespace game
} // namespace IKore
