#pragma once

#include "core/ecs/Registry.h"
#include "core/ecs/components/Components.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

/**
 * @file GeoJsonImporter.h
 * @brief Import GeoJSON / OpenStreetMap extracts into buildings + roads (#150).
 *
 * Milestone 11 - the third World-from-Data importer. Parses a GeoJSON
 * FeatureCollection and produces a renderer-agnostic City:
 *   - buildings: Polygon footprints extruded to a height (from a `height` tag,
 *     or `building:levels` x floor height, or a default),
 *   - roads: LineString geometry with a width from the `highway` type, with
 *     corner smoothing (Chaikin) and shared-endpoint intersection detection,
 *   - regions: water/park Polygons as flat slabs.
 *
 * Coordinates ([lon, lat] degrees) are projected to local meters with an
 * equirectangular projection around the first coordinate, mapped onto the world
 * XZ plane (buildings extrude +Y). emitToRegistry() creates a box entity per
 * building/region and a flat box per road segment.
 *
 * Self-contained (a small JSON parser is included) and pure std + the header-only
 * ECS, so it is unit-tested in isolation. Full building extrusion meshes, road
 * junction fills, and rendering are the follow-on.
 */
namespace IKore {
namespace world {

struct Building {
    std::vector<ecs::Vec3> footprint; // projected, y = 0
    float height{0.0f};
    ecs::Vec3 center{};               // bounding-box center (y = height/2)
    ecs::Vec3 size{};                 // bounding-box extents (y = height)
};

struct Road {
    std::vector<ecs::Vec3> centerline; // projected + smoothed, y = 0
    float width{0.0f};
    std::string kind;                  // highway class (motorway/residential/footway/...)
};

/// What a Region represents, so downstream can treat water as dangerous, parks as walkable
/// green space, etc. (issue #365).
enum class RegionKind { Generic, Water, Park, Grass };

struct Region { // water / park / grass, as a flat slab
    std::vector<ecs::Vec3> footprint;
    ecs::Vec3 center{};
    ecs::Vec3 size{};
    RegionKind kind{RegionKind::Generic};
};

/// A point-of-interest node imported from a Point feature (amenity/shop/tourism/...) - a
/// map-faithful place to seed an objective (issue #365).
struct Poi {
    ecs::Vec3 position{};
    std::string category; // the tag value (e.g. "restaurant", "fountain"), or "poi"
    std::string name;
};

/// A barrier way (wall/fence/hedge) imported from a tagged LineString - a thin obstacle the
/// player collides with (issue #365).
struct Barrier {
    std::vector<ecs::Vec3> line; // projected + smoothed, y = 0
    float width{0.5f};
    std::string kind;            // wall/fence/hedge/...
};

struct City {
    std::vector<Building> buildings;
    std::vector<Road> roads;
    std::vector<Region> regions;
    std::vector<Poi> pois;        // point features (#365)
    std::vector<Barrier> barriers; // barrier ways (#365)
    std::vector<ecs::Vec3> intersections; // nodes shared by >= 2 roads
};

struct GeoImportOptions {
    float scale{1.0f};                 // meters -> world units
    float floorHeight{3.0f};           // per building level
    float defaultBuildingHeight{6.0f}; // when no height/levels tag
    float roadThickness{0.05f};
    float regionThickness{0.02f};
    int roadSmoothingIterations{1};
    float barrierWidth{0.5f};          // default barrier thickness when the kind is unknown (#365)
    int barrierSmoothingIterations{0}; // barriers are usually straight; no smoothing by default
};

namespace detail {

// ---- Minimal JSON value + parser (GeoJSON subset) -----------------------
struct Json {
    enum class Type { Null, Bool, Num, Str, Arr, Obj };
    Type type{Type::Null};
    bool boolean{false};
    double num{0.0};
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    bool isNum() const { return type == Type::Num; }
    bool isStr() const { return type == Type::Str; }
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
    /// Numeric value whether stored as a number or a numeric string.
    double asNumber() const {
        if (type == Type::Num) return num;
        if (type == Type::Str) return std::strtod(str.c_str(), nullptr);
        return 0.0;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : m_s(src) {}
    Json parse() {
        Json root;
        value(root);
        return root;
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
            m_i += (m_s.compare(m_i, 4, "null") == 0) ? 4 : 1;
            v.type = Json::Type::Null;
            return true;
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
        if (m_i < m_s.size() && m_s[m_i] == '"') ++m_i;
        return true;
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
        ++m_i; // [
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
        ++m_i; // {
        ws();
        if (m_i < m_s.size() && m_s[m_i] == '}') {
            ++m_i;
            return true;
        }
        while (m_i < m_s.size()) {
            ws();
            std::string key;
            if (m_i >= m_s.size() || m_s[m_i] != '"') return false;
            string(key);
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

// ---- Geo helpers --------------------------------------------------------
struct GeoOrigin {
    double lon0{0.0}, lat0{0.0};
    bool set{false};
};

inline float roadWidthFor(const std::string& highway) {
    if (highway == "motorway" || highway == "trunk") return 12.0f;
    if (highway == "primary") return 8.0f;
    if (highway == "secondary") return 7.0f;
    if (highway == "tertiary" || highway == "residential") return 5.0f;
    if (highway == "service") return 4.0f;
    if (highway == "footway" || highway == "path" || highway == "pedestrian") return 2.0f;
    return 5.0f;
}

/// Classify a polygon region from its natural / leisure / landuse tags (#365).
inline RegionKind regionKindFor(const std::string& natural, const std::string& leisure,
                                const std::string& landuse) {
    if (natural == "water" || natural == "wetland" || landuse == "reservoir" ||
        landuse == "basin")
        return RegionKind::Water;
    if (leisure == "park" || leisure == "garden") return RegionKind::Park;
    if (landuse == "grass" || landuse == "meadow" || natural == "grassland" ||
        leisure == "pitch")
        return RegionKind::Grass;
    return RegionKind::Generic;
}

/// Thickness of a barrier way from its kind (#365).
inline float barrierWidthFor(const std::string& barrier, float fallback) {
    if (barrier == "wall" || barrier == "city_wall" || barrier == "retaining_wall") return 0.5f;
    if (barrier == "fence" || barrier == "guard_rail") return 0.3f;
    if (barrier == "hedge") return 0.6f;
    return fallback;
}

inline std::vector<ecs::Vec3> chaikin(const std::vector<ecs::Vec3>& pts, int iterations) {
    std::vector<ecs::Vec3> cur = pts;
    for (int it = 0; it < iterations && cur.size() >= 3; ++it) {
        std::vector<ecs::Vec3> out;
        out.reserve(cur.size() * 2);
        out.push_back(cur.front());
        for (std::size_t i = 0; i + 1 < cur.size(); ++i) {
            const ecs::Vec3& p = cur[i];
            const ecs::Vec3& q = cur[i + 1];
            out.push_back({0.75f * p.x + 0.25f * q.x, 0.0f, 0.75f * p.z + 0.25f * q.z});
            out.push_back({0.25f * p.x + 0.75f * q.x, 0.0f, 0.25f * p.z + 0.75f * q.z});
        }
        out.push_back(cur.back());
        cur = std::move(out);
    }
    return cur;
}

} // namespace detail

inline City importString(const std::string& geojson, const GeoImportOptions& options = {}) {
    using detail::Json;
    City city;

    const Json root = detail::JsonParser(geojson).parse();
    const Json& features = root.at("features");
    if (features.type != Json::Type::Arr) return city;

    // Projection origin = first coordinate found.
    detail::GeoOrigin origin;
    auto firstCoord = [&](const Json& coords) {
        const Json* c = &coords;
        while (c->type == Json::Type::Arr && c->size() > 0 && (*c)[0].type == Json::Type::Arr) {
            c = &(*c)[0];
        }
        if (c->type == Json::Type::Arr && c->size() >= 2 && !origin.set) {
            origin.lon0 = (*c)[0].asNumber();
            origin.lat0 = (*c)[1].asNumber();
            origin.set = true;
        }
    };
    for (std::size_t i = 0; i < features.size() && !origin.set; ++i) {
        firstCoord(features[i].at("geometry").at("coordinates"));
    }

    constexpr double kMetersPerDegLat = 111320.0;
    const double metersPerDegLon = kMetersPerDegLat * std::cos(origin.lat0 * 3.14159265358979323846 / 180.0);
    const float s = options.scale;
    auto project = [&](const Json& pt) -> ecs::Vec3 {
        const double lon = pt[0].asNumber();
        const double lat = pt[1].asNumber();
        return ecs::Vec3{static_cast<float>((lon - origin.lon0) * metersPerDegLon) * s, 0.0f,
                         static_cast<float>((lat - origin.lat0) * kMetersPerDegLat) * s};
    };
    auto projectRing = [&](const Json& ring) {
        std::vector<ecs::Vec3> pts;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            if (ring[i].type == Json::Type::Arr && ring[i].size() >= 2) pts.push_back(project(ring[i]));
        }
        return pts;
    };
    auto bounds = [](const std::vector<ecs::Vec3>& pts, ecs::Vec3& center, ecs::Vec3& size) {
        if (pts.empty()) return;
        float minX = pts[0].x, maxX = pts[0].x, minZ = pts[0].z, maxZ = pts[0].z;
        for (const ecs::Vec3& p : pts) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minZ = std::min(minZ, p.z);
            maxZ = std::max(maxZ, p.z);
        }
        center = ecs::Vec3{(minX + maxX) * 0.5f, 0.0f, (minZ + maxZ) * 0.5f};
        size = ecs::Vec3{maxX - minX, 0.0f, maxZ - minZ};
    };

    std::map<std::pair<long, long>, std::pair<int, ecs::Vec3>> nodeUse; // for intersections

    for (std::size_t fi = 0; fi < features.size(); ++fi) {
        const Json& feat = features[fi];
        const Json& geom = feat.at("geometry");
        const Json& props = feat.at("properties");
        const std::string gtype = geom.at("type").asStr();
        const Json& coords = geom.at("coordinates");

        const bool isBuilding = props.has("building") || props.has("building:levels") ||
                                (props.has("height") && gtype == "Polygon");
        const std::string natural = props.at("natural").asStr();
        const std::string leisure = props.at("leisure").asStr();
        const std::string landuse = props.at("landuse").asStr();
        const RegionKind regionKind = detail::regionKindFor(natural, leisure, landuse);
        const bool isRegion = regionKind != RegionKind::Generic;

        if (gtype == "Polygon" && isBuilding) {
            std::vector<ecs::Vec3> ring = projectRing(coords[0]);
            if (ring.size() < 3) continue;
            float height = options.defaultBuildingHeight;
            if (props.has("height")) {
                height = static_cast<float>(props.at("height").asNumber());
            } else if (props.has("building:levels")) {
                height = static_cast<float>(props.at("building:levels").asNumber()) * options.floorHeight;
            } else if (props.has("levels")) {
                height = static_cast<float>(props.at("levels").asNumber()) * options.floorHeight;
            }
            Building b;
            b.footprint = ring;
            b.height = height;
            bounds(ring, b.center, b.size);
            b.center.y = height * 0.5f;
            b.size.y = height;
            city.buildings.push_back(std::move(b));
        } else if (gtype == "Polygon" && isRegion) {
            std::vector<ecs::Vec3> ring = projectRing(coords[0]);
            if (ring.size() < 3) continue;
            Region r;
            r.footprint = ring;
            r.kind = regionKind;
            bounds(ring, r.center, r.size);
            r.center.y = -options.regionThickness * 0.5f;
            r.size.y = options.regionThickness;
            city.regions.push_back(std::move(r));
        } else if (gtype == "LineString" && props.has("highway")) {
            std::vector<ecs::Vec3> line = projectRing(coords);
            if (line.size() < 2) continue;
            Road road;
            road.kind = props.at("highway").asStr();
            road.width = detail::roadWidthFor(road.kind);
            road.centerline = detail::chaikin(line, options.roadSmoothingIterations);
            // Record endpoints for intersection detection (use pre-smoothing ends).
            for (const ecs::Vec3& end : {line.front(), line.back()}) {
                const std::pair<long, long> key{std::lround(end.x * 100.0f), std::lround(end.z * 100.0f)};
                auto& use = nodeUse[key];
                use.first += 1;
                use.second = end;
            }
            city.roads.push_back(std::move(road));
        } else if (gtype == "LineString" && props.has("barrier")) {
            std::vector<ecs::Vec3> line = projectRing(coords);
            if (line.size() < 2) continue;
            Barrier bar;
            bar.kind = props.at("barrier").asStr();
            bar.width = detail::barrierWidthFor(bar.kind, options.barrierWidth);
            bar.line = detail::chaikin(line, options.barrierSmoothingIterations);
            city.barriers.push_back(std::move(bar));
        } else if (gtype == "Point" && coords.size() >= 2) {
            Poi poi;
            poi.position = project(coords);
            for (const char* tag : {"amenity", "shop", "tourism", "leisure", "natural",
                                    "historic", "man_made"}) {
                if (props.has(tag)) {
                    poi.category = props.at(tag).asStr();
                    break;
                }
            }
            if (poi.category.empty()) poi.category = "poi";
            poi.name = props.at("name").asStr();
            city.pois.push_back(std::move(poi));
        }
    }

    for (const auto& entry : nodeUse) {
        if (entry.second.first >= 2) city.intersections.push_back(entry.second.second);
    }

    return city;
}

/// Emit boxes for buildings/regions and flat boxes per road segment. Returns count.
inline std::size_t emitToRegistry(const City& city, ecs::Registry& registry,
                                  const GeoImportOptions& options = {}) {
    std::size_t created = 0;
    auto emit = [&](const ecs::Vec3& center, const ecs::Vec3& size, float yaw) {
        ecs::Entity e = registry.create();
        registry.add<ecs::Transform>(e, ecs::Transform{center, ecs::Vec3{0.0f, yaw, 0.0f}, size});
        ++created;
    };
    for (const Building& b : city.buildings) emit(b.center, b.size, 0.0f);
    for (const Region& r : city.regions) emit(r.center, r.size, 0.0f);
    for (const Road& road : city.roads) {
        for (std::size_t i = 0; i + 1 < road.centerline.size(); ++i) {
            const ecs::Vec3& a = road.centerline[i];
            const ecs::Vec3& b = road.centerline[i + 1];
            const float dx = b.x - a.x, dz = b.z - a.z;
            const float length = std::sqrt(dx * dx + dz * dz);
            if (length <= 0.0f) continue;
            emit(ecs::Vec3{(a.x + b.x) * 0.5f, options.roadThickness * 0.5f, (a.z + b.z) * 0.5f},
                 ecs::Vec3{length, options.roadThickness, road.width}, std::atan2(dz, dx));
        }
    }
    return created;
}

} // namespace world
} // namespace IKore
