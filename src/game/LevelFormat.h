#pragma once

#include "game/DoodleScene.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

/**
 * @file LevelFormat.h
 * @brief Level-spec and 3D-scene JSON save/load for Doodlebound (issue #163).
 *
 * Milestone 15. Defines two JSON formats and their (de)serialization:
 *   - "doodle-level": the intermediate, hand-authorable level spec (wall polylines
 *     + placed symbols + extrusion params) - what a drawing front end emits.
 *   - "doodle-scene": the emitted 3D scene the engine loads (oriented wall boxes +
 *     entity spawns) - the output of converting a level spec (#162).
 *
 * Both round-trip through save/load. The schema is documented in
 * docs/DOODLE_LEVEL_FORMAT.md, with a hand-authored sample in
 * assets/levels/sample_dungeon.level.json.
 *
 * A small self-contained JSON value + parser lives in game::detail (kept separate
 * from the world importers' parsers to avoid any coupling). Header-only and
 * dependency-free.
 */
namespace IKore {
namespace game {
namespace detail {

/// Minimal JSON value (objects, arrays, numbers, strings, bools, null).
struct Json {
    enum class Type { Null, Bool, Num, Str, Arr, Obj };
    Type type{Type::Null};
    bool boolean{false};
    double num{0.0};
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    bool isArr() const { return type == Type::Arr; }
    bool isObj() const { return type == Type::Obj; }
    double asNum() const { return type == Type::Num ? num : 0.0; }
    const std::string& asStr() const {
        static const std::string empty;
        return type == Type::Str ? str : empty;
    }
    std::size_t size() const { return arr.size(); }
    const Json& operator[](std::size_t i) const {
        static const Json null;
        return i < arr.size() ? arr[i] : null;
    }
    bool has(const std::string& key) const { return type == Type::Obj && obj.count(key) > 0; }
    const Json& at(const std::string& key) const {
        static const Json null;
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : m_s(s) {}
    bool parse(Json& out) {
        if (!value(out)) return false;
        ws();
        return m_i == m_s.size();
    }

private:
    void ws() {
        while (m_i < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_i]))) ++m_i;
    }
    bool value(Json& v) {
        ws();
        if (m_i >= m_s.size()) return false;
        const char c = m_s[m_i];
        if (c == '{') return object(v);
        if (c == '[') return array(v);
        if (c == '"') {
            v.type = Json::Type::Str;
            return string(v.str);
        }
        if (c == 't' || c == 'f') return boolean(v);
        if (c == 'n') {
            if (m_s.compare(m_i, 4, "null") == 0) {
                m_i += 4;
                v.type = Json::Type::Null;
                return true;
            }
            return false;
        }
        return number(v);
    }
    bool string(std::string& out) {
        if (m_s[m_i] != '"') return false;
        ++m_i;
        out.clear();
        while (m_i < m_s.size() && m_s[m_i] != '"') {
            char c = m_s[m_i++];
            if (c == '\\' && m_i < m_s.size()) {
                const char e = m_s[m_i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    default: out += e; break; // covers " \\ /
                }
            } else {
                out += c;
            }
        }
        if (m_i < m_s.size() && m_s[m_i] == '"') {
            ++m_i;
            return true;
        }
        return false;
    }
    bool number(Json& v) {
        char* end = nullptr;
        const double d = std::strtod(m_s.c_str() + m_i, &end);
        if (end == m_s.c_str() + m_i) return false;
        m_i = static_cast<std::size_t>(end - m_s.c_str());
        v.type = Json::Type::Num;
        v.num = d;
        return true;
    }
    bool boolean(Json& v) {
        if (m_s.compare(m_i, 4, "true") == 0) {
            m_i += 4;
            v.type = Json::Type::Bool;
            v.boolean = true;
            return true;
        }
        if (m_s.compare(m_i, 5, "false") == 0) {
            m_i += 5;
            v.type = Json::Type::Bool;
            v.boolean = false;
            return true;
        }
        return false;
    }
    bool array(Json& v) {
        v.type = Json::Type::Arr;
        ++m_i;
        ws();
        if (m_i < m_s.size() && m_s[m_i] == ']') {
            ++m_i;
            return true;
        }
        while (m_i < m_s.size()) {
            Json e;
            if (!value(e)) return false;
            v.arr.push_back(std::move(e));
            ws();
            if (m_i < m_s.size() && m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_i < m_s.size() && m_s[m_i] == ']') {
                ++m_i;
                return true;
            }
            return false;
        }
        return false;
    }
    bool object(Json& v) {
        v.type = Json::Type::Obj;
        ++m_i;
        ws();
        if (m_i < m_s.size() && m_s[m_i] == '}') {
            ++m_i;
            return true;
        }
        while (m_i < m_s.size()) {
            ws();
            if (m_i >= m_s.size() || m_s[m_i] != '"') return false;
            std::string key;
            if (!string(key)) return false;
            ws();
            if (m_i >= m_s.size() || m_s[m_i] != ':') return false;
            ++m_i;
            Json val;
            if (!value(val)) return false;
            v.obj.emplace(std::move(key), std::move(val));
            ws();
            if (m_i < m_s.size() && m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_i < m_s.size() && m_s[m_i] == '}') {
                ++m_i;
                return true;
            }
            return false;
        }
        return false;
    }

    const std::string& m_s;
    std::size_t m_i{0};
};

inline bool parse(const std::string& text, Json& out) { return JsonParser(text).parse(out); }

/// Compact JSON number: integral magnitudes without a decimal point.
inline std::string numToStr(double v) {
    char buf[40];
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    } else {
        std::snprintf(buf, sizeof(buf), "%.6g", v);
    }
    return buf;
}

inline std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default: o += c; break;
        }
    }
    return o;
}

inline std::string vec3Json(const ecs::Vec3& v) {
    return "[" + numToStr(v.x) + ", " + numToStr(v.y) + ", " + numToStr(v.z) + "]";
}

} // namespace detail

// --- doodle-level (the hand-authorable level spec) -------------------------

/// Serialize a LevelSpec to the "doodle-level" JSON format.
inline std::string toLevelJson(const LevelSpec& s) {
    std::string o = "{\n  \"format\": \"doodle-level\",\n  \"version\": 1,\n";
    o += "  \"wallHeight\": " + detail::numToStr(s.wallHeight) + ",\n";
    o += "  \"wallThickness\": " + detail::numToStr(s.wallThickness) + ",\n";
    o += "  \"walls\": [";
    for (std::size_t i = 0; i < s.walls.size(); ++i) {
        o += i ? "," : "";
        o += "\n    {\"polyline\": [";
        const std::vector<ecs::Vec3>& pl = s.walls[i].polyline;
        for (std::size_t j = 0; j < pl.size(); ++j) {
            if (j) o += ", ";
            o += "[" + detail::numToStr(pl[j].x) + ", " + detail::numToStr(pl[j].z) + "]";
        }
        o += "]}";
    }
    o += s.walls.empty() ? "]" : "\n  ]";
    o += ",\n  \"symbols\": [";
    for (std::size_t i = 0; i < s.symbols.size(); ++i) {
        o += i ? "," : "";
        const Symbol& sy = s.symbols[i];
        o += "\n    {\"type\": \"" + detail::escape(sy.type) + "\", \"x\": " +
             detail::numToStr(sy.position.x) + ", \"z\": " + detail::numToStr(sy.position.z) +
             ", \"yaw\": " + detail::numToStr(sy.yaw) + "}";
    }
    o += s.symbols.empty() ? "]" : "\n  ]";
    o += "\n}\n";
    return o;
}

/// Parse a "doodle-level" JSON string into a LevelSpec. Returns false if invalid.
inline bool fromLevelJson(const std::string& text, LevelSpec& out) {
    detail::Json root;
    if (!detail::parse(text, root) || !root.isObj()) return false;
    out = LevelSpec{};
    out.wallHeight = root.has("wallHeight") ? static_cast<float>(root.at("wallHeight").asNum()) : 3.0f;
    out.wallThickness =
        root.has("wallThickness") ? static_cast<float>(root.at("wallThickness").asNum()) : 0.2f;

    const detail::Json& walls = root.at("walls");
    if (walls.isArr()) {
        for (std::size_t i = 0; i < walls.size(); ++i) {
            const detail::Json& pl = walls[i].at("polyline");
            Wall wall;
            if (pl.isArr()) {
                for (std::size_t j = 0; j < pl.size(); ++j) {
                    const detail::Json& pt = pl[j];
                    if (pt.isArr() && pt.size() >= 2) {
                        wall.polyline.push_back(ecs::Vec3{static_cast<float>(pt[0].asNum()), 0.0f,
                                                          static_cast<float>(pt[1].asNum())});
                    }
                }
            }
            out.walls.push_back(std::move(wall));
        }
    }

    const detail::Json& syms = root.at("symbols");
    if (syms.isArr()) {
        for (std::size_t i = 0; i < syms.size(); ++i) {
            const detail::Json& s = syms[i];
            Symbol sym;
            sym.type = s.at("type").asStr();
            sym.position = ecs::Vec3{static_cast<float>(s.at("x").asNum()), 0.0f,
                                     static_cast<float>(s.at("z").asNum())};
            sym.yaw = s.has("yaw") ? static_cast<float>(s.at("yaw").asNum()) : 0.0f;
            out.symbols.push_back(std::move(sym));
        }
    }
    return true;
}

// --- doodle-scene (the emitted 3D scene the engine loads) ------------------

/// Serialize a converted SceneDescription to the "doodle-scene" JSON format.
inline std::string toSceneJson(const SceneDescription& scene) {
    std::string o = "{\n  \"format\": \"doodle-scene\",\n  \"version\": 1,\n";
    o += "  \"walls\": [";
    for (std::size_t i = 0; i < scene.wallBoxes.size(); ++i) {
        o += i ? "," : "";
        const world::Box& b = scene.wallBoxes[i];
        o += "\n    {\"center\": " + detail::vec3Json(b.center) + ", \"size\": " +
             detail::vec3Json(b.size) + ", \"yaw\": " + detail::numToStr(b.yaw) + "}";
    }
    o += scene.wallBoxes.empty() ? "]" : "\n  ]";
    o += ",\n  \"spawns\": [";
    for (std::size_t i = 0; i < scene.spawns.size(); ++i) {
        o += i ? "," : "";
        const EntitySpawn& s = scene.spawns[i];
        o += "\n    {\"type\": \"" + detail::escape(s.type) + "\", \"position\": " +
             detail::vec3Json(s.position) + ", \"yaw\": " + detail::numToStr(s.yaw) + "}";
    }
    o += scene.spawns.empty() ? "]" : "\n  ]";
    o += "\n}\n";
    return o;
}

/// Parse a "doodle-scene" JSON string into a SceneDescription (boxes + spawns).
/// The triangle mesh is derived, so it is left empty here.
inline bool fromSceneJson(const std::string& text, SceneDescription& out) {
    detail::Json root;
    if (!detail::parse(text, root) || !root.isObj()) return false;
    out = SceneDescription{};
    auto readVec3 = [](const detail::Json& a) {
        return ecs::Vec3{static_cast<float>(a[0].asNum()), static_cast<float>(a[1].asNum()),
                         static_cast<float>(a[2].asNum())};
    };

    const detail::Json& walls = root.at("walls");
    if (walls.isArr()) {
        for (std::size_t i = 0; i < walls.size(); ++i) {
            const detail::Json& w = walls[i];
            world::Box b;
            b.center = readVec3(w.at("center"));
            b.size = readVec3(w.at("size"));
            b.yaw = static_cast<float>(w.at("yaw").asNum());
            out.wallBoxes.push_back(b);
        }
    }
    const detail::Json& spawns = root.at("spawns");
    if (spawns.isArr()) {
        for (std::size_t i = 0; i < spawns.size(); ++i) {
            const detail::Json& s = spawns[i];
            EntitySpawn spawn;
            spawn.type = s.at("type").asStr();
            spawn.position = readVec3(s.at("position"));
            spawn.yaw = static_cast<float>(s.at("yaw").asNum());
            out.spawns.push_back(spawn);
        }
    }
    return true;
}

} // namespace game
} // namespace IKore
