#include "han/world/walk_layout.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "han/core/json.h"
#include "han/world/city_builder.h"
#include "han/world/road_profile.h"

namespace han::world {

namespace {
constexpr float kPi = 3.14159265358979f;

bool isWalkingClass(std::string_view h) { return h == "pedestrian" || h == "footway" || h == "path" || h == "steps" || h == "cycleway" || h == "service"; }
bool isLink(std::string_view h) { return h.size() > 5 && h.substr(h.size() - 5) == "_link"; }

// Прямоугольник вокруг отрезка a-b: полуширина half поперёк, extend вдоль отрезка в обе стороны.
std::vector<Vec2> rectangle(Vec2 a, Vec2 b, float half, float extend = 0.f) {
    const float dx = b.x - a.x, dz = b.y - a.y, len = std::hypot(dx, dz);
    if (len < 1e-3f) return {};
    const float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    const Vec2 p0{a.x - ux * extend, a.y - uz * extend}, p1{b.x + ux * extend, b.y + uz * extend};
    return {{p0.x + nx * half, p0.y + nz * half}, {p1.x + nx * half, p1.y + nz * half}, {p1.x - nx * half, p1.y - nz * half}, {p0.x - nx * half, p0.y - nz * half}};
}

std::vector<Vec2> disc(Vec2 c, float r, int sides) {
    std::vector<Vec2> out;
    for (int k = 0; k < sides; ++k) {
        const float a = 2.f * kPi * static_cast<float>(k) / static_cast<float>(sides);
        out.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
    return out;
}
}  // namespace

std::vector<std::vector<Vec2>> WalkLayout::obstacles() const {
    std::vector<std::vector<Vec2>> all;
    all.reserve(buildings.size() + water.size() + carriageways.size());
    all.insert(all.end(), buildings.begin(), buildings.end());
    all.insert(all.end(), water.begin(), water.end());
    all.insert(all.end(), carriageways.begin(), carriageways.end());
    return all;
}

bool WalkLayout::addWaterAreas(const std::string& file, const GeoProjection& projection, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    const auto& polygons = root["polygons"];
    if (!root.isObject() || !polygons.isArray()) {
        error = file + ": нет массива polygons";
        return false;
    }
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        const auto& triangles = polygons.at(i)["triangles"];
        for (std::size_t t = 0; t < triangles.size(); ++t) {
            const auto& tri = triangles.at(t);
            if (tri.size() != 3) continue;
            std::vector<Vec2> poly;
            for (std::size_t k = 0; k < 3; ++k) {
                Vec2 p;
                projection.toLocal(tri.at(k).at(1).asNumber(), tri.at(k).at(0).asNumber(), p.x, p.y);  // в файле [долгота, широта]
                poly.push_back(p);
            }
            water.push_back(std::move(poly));
        }
    }
    return true;
}

WalkLayout buildWalkLayout(const OsmData& data, const GeoProjection& projection, const CityStyle& style, const WalkLayoutSettings& settings) {
    WalkLayout layout;
    layout.buildings = buildingFootprints(data, projection, settings.minBuildingArea);
    const auto project = [&](const OsmWay& way) {
        std::vector<Vec2> pts;
        for (const std::int64_t id : way.nodes) {
            const auto it = data.nodes.find(id);
            if (it == data.nodes.end()) continue;
            Vec2 p;
            projection.toLocal(it->second.lat, it->second.lon, p.x, p.y);
            pts.push_back(p);
        }
        return pts;
    };
    // Степень узла — как у строителя: по ним рисуются зебры.
    std::unordered_map<std::int64_t, int> degree;
    if (style.enabled)
        for (const OsmWay& way : data.ways) {
            if (way.tag("han:visual") == "external" || roadWidth(way) < 6.f || way.tag("area") == "yes") continue;
            for (std::size_t i = 0; i + 1 < way.nodes.size(); ++i) {
                ++degree[way.nodes[i]];
                ++degree[way.nodes[i + 1]];
            }
        }
    const float crossInset = style.crosswalkLength * 0.75f + 1.f;
    for (const OsmWay& way : data.ways) {
        const std::string_view highway = way.tag("highway");
        const bool isWater = way.tag("natural") == "water" || way.tag("waterway") == "riverbank" || way.tag("landuse") == "reservoir";
        if (isWater && way.closed()) {
            auto pts = project(way);
            if (pts.size() >= 4) {
                pts.pop_back();
                layout.water.push_back(std::move(pts));
            }
            continue;
        }
        if (highway.empty() || isBridgeWay(way)) continue;
        if (!way.tag("tunnel").empty() && way.tag("tunnel") != "no") continue;
        const float width = roadWidth(way);
        if (width <= 0.f || isWalkingClass(highway)) continue;
        const bool motor = highway == "motorway" || highway == "trunk" || isLink(highway);
        if (!motor && width < settings.minCarriagewayWidth) continue;  // переулок без тротуаров: ходят по мостовой
        if (way.tag("area") == "yes") continue;
        const auto pts = project(way);
        if (pts.size() < 2) continue;
        const float half = std::max(0.5f, width * 0.5f - settings.carriagewayMargin);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            auto rect = rectangle(pts[i], pts[i + 1], half);
            if (!rect.empty()) layout.carriageways.push_back(std::move(rect));
            layout.carriageways.push_back(disc(pts[i + 1], half, settings.junctionSides));
        }
        layout.carriageways.push_back(disc(pts.front(), half, settings.junctionSides));
        // Проёмы: зебры на концах сегментов у узлов с тремя и более дорогами (как в city_builder).
        if (!style.enabled || motor || width < 8.f || pts.size() != way.nodes.size() || way.tag("han:visual") == "external") continue;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const float dx = pts[i + 1].x - pts[i].x, dz = pts[i + 1].y - pts[i].y, length = std::hypot(dx, dz);
            if (length <= crossInset * 3.f) continue;
            const float ux = dx / length, uz = dz / length;
            const float alongHalf = style.crosswalkLength * 0.5f + settings.crossingMargin, acrossHalf = width * 0.5f + settings.crossingMargin;
            const auto addCrossing = [&](Vec2 center) {
                // Прямоугольник: вдоль дороги — зебра, поперёк — вся проезжая часть с выступом на тротуары.
                auto rect = rectangle({center.x - ux * alongHalf, center.y - uz * alongHalf}, {center.x + ux * alongHalf, center.y + uz * alongHalf}, acrossHalf);
                if (!rect.empty()) layout.crossings.push_back(std::move(rect));
            };
            if (degree[way.nodes[i]] >= 3) addCrossing({pts[i].x + ux * crossInset, pts[i].y + uz * crossInset});
            if (degree[way.nodes[i + 1]] >= 3) addCrossing({pts[i + 1].x - ux * crossInset, pts[i + 1].y - uz * crossInset});
        }
    }
    return layout;
}

}  // namespace han::world
