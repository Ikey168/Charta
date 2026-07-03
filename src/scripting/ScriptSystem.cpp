#include "scripting/ScriptSystem.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "core/Logger.h"
#include "core/ecs/View.h"

namespace IKore {

using namespace IKore::ecs;

ScriptSystem::ScriptSystem() : m_lua(std::make_unique<sol::state>()) {
    m_lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    registerBindings();
}

ScriptSystem::~ScriptSystem() {
    // Release event hooks BEFORE the Lua state is destroyed: the subscription
    // callbacks hold Lua functions, and the global EventSystem outlives us.
    EventSystem& events = EventSystem::getInstance();
    for (const EventSubscription& sub : m_subscriptions) {
        events.unsubscribe(sub);
    }
    m_subscriptions.clear();
}

bool ScriptSystem::runString(const std::string& code, std::string* errorOut) {
    sol::protected_function_result result = m_lua->safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        if (errorOut) *errorOut = err.what();
        LOG_ERROR(std::string("ScriptSystem: ") + err.what());
        return false;
    }
    return true;
}

bool ScriptSystem::runFile(const std::string& path, std::string* errorOut) {
    sol::protected_function_result result = m_lua->safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        if (errorOut) *errorOut = err.what();
        LOG_ERROR(std::string("ScriptSystem: ") + err.what());
        return false;
    }
    return true;
}

void ScriptSystem::registerBindings() {
    sol::state& lua = *m_lua;

    // --- Component value types --------------------------------------------
    lua.new_usertype<Vec3>("Vec3",
        sol::factories(
            []() { return Vec3{}; },
            [](float x, float y, float z) { return Vec3{x, y, z}; }),
        "x", &Vec3::x, "y", &Vec3::y, "z", &Vec3::z);

    lua.new_usertype<Transform>("Transform",
        sol::factories([]() { return Transform{}; }),
        "position", &Transform::position,
        "rotation", &Transform::rotation,
        "scale", &Transform::scale);

    lua.new_usertype<Velocity>("Velocity",
        sol::factories([]() { return Velocity{}; }),
        "linear", &Velocity::linear,
        "angular", &Velocity::angular);

    // Entity is an opaque handle (index + generation), read-only from Lua.
    lua.new_usertype<ecs::Entity>("Entity",
        "index", sol::readonly(&ecs::Entity::index),
        "generation", sol::readonly(&ecs::Entity::generation));

    // --- world: entity + component API ------------------------------------
    sol::table world = lua["world"].get_or_create<sol::table>();
    world.set_function("create", [this]() { return m_registry.create(); });
    world.set_function("destroy", [this](ecs::Entity e) { m_registry.destroy(e); });
    world.set_function("valid", [this](ecs::Entity e) { return m_registry.isValid(e); });

    world.set_function("add_transform", [this](ecs::Entity e, sol::optional<Transform> t) {
        m_registry.add<Transform>(e, t.value_or(Transform{}));
    });
    world.set_function("has_transform", [this](ecs::Entity e) { return m_registry.has<Transform>(e); });
    world.set_function("get_transform", [this](ecs::Entity e) { return m_registry.get<Transform>(e); });
    world.set_function("set_transform", [this](ecs::Entity e, Transform t) {
        if (m_registry.has<Transform>(e)) m_registry.get<Transform>(e) = t;
    });

    world.set_function("add_velocity", [this](ecs::Entity e, sol::optional<Velocity> v) {
        m_registry.add<Velocity>(e, v.value_or(Velocity{}));
    });
    world.set_function("has_velocity", [this](ecs::Entity e) { return m_registry.has<Velocity>(e); });
    world.set_function("get_velocity", [this](ecs::Entity e) { return m_registry.get<Velocity>(e); });
    world.set_function("set_velocity", [this](ecs::Entity e, Velocity v) {
        if (m_registry.has<Velocity>(e)) m_registry.get<Velocity>(e) = v;
    });

    world.set_function("transform_count",
                       [this]() { return static_cast<int>(m_registry.view<Transform>().count()); });

    // --- events: bridge to the engine EventSystem -------------------------
    sol::table events = lua["events"].get_or_create<sol::table>();
    events.set_function("emit", sol::overload(
        [](const std::string& name) { EventSystem::getInstance().publish<double>(name, 0.0); },
        [](const std::string& name, double value) {
            EventSystem::getInstance().publish<double>(name, value);
        }));
    events.set_function("on", [this](const std::string& name, sol::protected_function callback) {
        EventSubscription sub = EventSystem::getInstance().subscribe(
            name, [callback](const std::shared_ptr<EventData>& data) {
                double value = 0.0;
                if (auto typed = std::dynamic_pointer_cast<TypedEventData<double>>(data)) {
                    value = typed->data;
                }
                sol::protected_function_result r = callback(value);
                if (!r.valid()) {
                    sol::error err = r;
                    LOG_ERROR(std::string("ScriptSystem event handler: ") + err.what());
                }
            });
        m_subscriptions.push_back(sub);
    });
}

} // namespace IKore
