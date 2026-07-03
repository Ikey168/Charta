#pragma once

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file InputMap.h
 * @brief Rebindable action -> input map with conflict detection (issue #60).
 *
 * Milestone 9 (In-Engine Editor & Tooling). The headless core behind the input
 * remapping panel: named game actions each bound to an input (a keyboard key, a
 * mouse button, or a gamepad button, identified by device + integer code). Rebinding
 * an action updates its binding in place so it takes effect immediately; bindings
 * serialize to a simple text form and round-trip to disk so they persist across
 * sessions; and conflicts (two actions on the same input) are detected so the panel
 * can flag them.
 *
 * Codes are opaque integers (the engine uses ImGui key / mouse-button values), so
 * the core needs no ImGui or GLFW and is unit-testable on its own. Header-only.
 */
namespace IKore {

enum class InputDevice { None, Keyboard, Mouse, Gamepad };

struct InputMapBinding {
    InputDevice device{InputDevice::None};
    int code{-1};

    bool bound() const { return device != InputDevice::None; }
    bool operator==(const InputMapBinding& o) const { return device == o.device && code == o.code; }
    bool operator!=(const InputMapBinding& o) const { return !(*this == o); }
};

inline const char* deviceName(InputDevice device) {
    switch (device) {
        case InputDevice::Keyboard: return "keyboard";
        case InputDevice::Mouse: return "mouse";
        case InputDevice::Gamepad: return "gamepad";
        case InputDevice::None: return "none";
    }
    return "none";
}

inline InputDevice deviceFromName(const std::string& name) {
    if (name == "keyboard") return InputDevice::Keyboard;
    if (name == "mouse") return InputDevice::Mouse;
    if (name == "gamepad") return InputDevice::Gamepad;
    return InputDevice::None;
}

/// Serialize a binding as "device:code", or "none" when unbound.
inline std::string toString(const InputMapBinding& binding) {
    if (!binding.bound()) return "none";
    return std::string(deviceName(binding.device)) + ":" + std::to_string(binding.code);
}

/// Parse a "device:code" binding; returns an unbound binding on anything invalid.
inline InputMapBinding bindingFromString(const std::string& text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) return InputMapBinding{};
    const InputDevice device = deviceFromName(text.substr(0, colon));
    if (device == InputDevice::None) return InputMapBinding{};
    try {
        return InputMapBinding{device, std::stoi(text.substr(colon + 1))};
    } catch (...) {
        return InputMapBinding{};
    }
}

class InputMap {
public:
    /// Register an action with a default binding. Duplicate names are ignored.
    /// Registration order is preserved for the panel.
    void addAction(const std::string& name, InputMapBinding defaultBinding = {}) {
        if (m_index.count(name) != 0) return;
        m_index.emplace(name, m_actions.size());
        m_actions.push_back(Action{name, defaultBinding, defaultBinding});
    }

    bool hasAction(const std::string& name) const { return m_index.count(name) != 0; }
    std::size_t actionCount() const { return m_actions.size(); }
    const std::string& actionName(std::size_t i) const { return m_actions[i].name; }
    InputMapBinding bindingAt(std::size_t i) const { return m_actions[i].binding; }

    /// Current binding for @p action (unbound if the action is unknown).
    InputMapBinding binding(const std::string& action) const {
        const Action* a = find(action);
        return a ? a->binding : InputMapBinding{};
    }

    /// Rebind @p action; takes effect immediately. No-op if the action is unknown.
    void rebind(const std::string& action, InputMapBinding binding) {
        if (Action* a = find(action)) a->binding = binding;
    }

    void unbind(const std::string& action) {
        if (Action* a = find(action)) a->binding = InputMapBinding{};
    }

    /// The first action bound to @p binding, or "" (never matches unbound).
    std::string actionFor(InputMapBinding binding) const {
        if (!binding.bound()) return std::string();
        for (const Action& a : m_actions) {
            if (a.binding == binding) return a.name;
        }
        return std::string();
    }

    /// True if another action shares @p action's (bound) input.
    bool hasConflict(const std::string& action) const {
        const Action* self = find(action);
        if (self == nullptr || !self->binding.bound()) return false;
        for (const Action& a : m_actions) {
            if (&a != self && a.binding.bound() && a.binding == self->binding) return true;
        }
        return false;
    }

    /// Names of other actions sharing @p action's binding.
    std::vector<std::string> conflictsWith(const std::string& action) const {
        std::vector<std::string> out;
        const Action* self = find(action);
        if (self == nullptr || !self->binding.bound()) return out;
        for (const Action& a : m_actions) {
            if (&a != self && a.binding.bound() && a.binding == self->binding) out.push_back(a.name);
        }
        return out;
    }

    /// True if any two actions share the same input.
    bool anyConflicts() const {
        for (std::size_t i = 0; i < m_actions.size(); ++i) {
            if (!m_actions[i].binding.bound()) continue;
            for (std::size_t j = i + 1; j < m_actions.size(); ++j) {
                if (m_actions[j].binding.bound() && m_actions[j].binding == m_actions[i].binding) {
                    return true;
                }
            }
        }
        return false;
    }

    /// Restore every action to the default it was registered with.
    void resetToDefaults() {
        for (Action& a : m_actions) a.binding = a.def;
    }

    /// Serialize as "action=device:code" lines, in registration order.
    std::string serialize() const {
        std::string out;
        for (const Action& a : m_actions) out += a.name + "=" + toString(a.binding) + "\n";
        return out;
    }

    /// Apply "action=device:code" lines to the registered actions. Unknown actions
    /// and malformed lines are ignored; blank and '#'/';' lines are skipped.
    void load(const std::string& text) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
            const std::size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            const std::string action = trim(trimmed.substr(0, eq));
            if (hasAction(action)) rebind(action, bindingFromString(trim(trimmed.substr(eq + 1))));
        }
    }

    bool saveToFile(const std::string& path) const {
        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << serialize();
        return static_cast<bool>(out);
    }

    bool loadFromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        load(ss.str());
        return true;
    }

private:
    struct Action {
        std::string name;
        InputMapBinding binding;
        InputMapBinding def;
    };

    Action* find(const std::string& name) {
        auto it = m_index.find(name);
        return it == m_index.end() ? nullptr : &m_actions[it->second];
    }
    const Action* find(const std::string& name) const {
        auto it = m_index.find(name);
        return it == m_index.end() ? nullptr : &m_actions[it->second];
    }

    static std::string trim(const std::string& s) {
        const std::size_t a = s.find_first_not_of(" \t\r\n");
        const std::size_t b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    std::vector<Action> m_actions;
    std::unordered_map<std::string, std::size_t> m_index;
};

} // namespace IKore
