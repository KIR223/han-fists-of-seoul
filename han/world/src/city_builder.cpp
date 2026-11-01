#include "han/world/city_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace han::world {
namespace {
constexpr float kPi = 3.14159265f;

struct Color {
    float r, g, b;
};

Color roadColor(std::string_view highway) {
    if (highway == "footway" || highway == "pedestrian" || highway == "path" || highway == "steps" || highway == "cycleway") return {0.55f, 0.50f, 0.46f};
    if (highway == "motorway" || highway == "trunk" || highway == "primary") return {0.16f, 0.16f, 0.17f};
    return {0.22f, 0.22f, 0.23f};
}

// Цвета домов — приглушённые городские тона, выбор по номеру линии (стабильно между запусками).
Color buildingColor(std::int64_t id) {
    static const Color palette[] = {{0.72f, 0.69f, 0.64f}, {0.62f, 0.60f, 0.58f}, {0.78f, 0.74f, 0.66f}, {0.55f, 0.52f, 0.50f},
                                    {0.70f, 0.62f, 0.55f}, {0.66f, 0.68f, 0.70f}, {0.80f, 0.78f, 0.74f}, {0.60f, 0.56f, 0.52f}};
    return palette[static_cast<std::size_t>(id < 0 ? -id : id) % (sizeof(palette) / sizeof(palette[0]))];
}

void pushVertex(GeoMesh& m, float x, float y, float z, float nx, float ny, float nz, Color c) {
    m.vertices.push_back({{x, y, z}, {nx, ny, nz}, {c.r, c.g, c.b}});
}

// Горизонтальный многоугольник на высоте y, нормаль вверх. Возвращает false, если не удалось разбить.
bool addFlatPolygon(GeoMesh& mesh, const std::vector<Vec2>& poly, float y, Color color, bool faceUp = true) {
    const auto tri = triangulate(poly);
    if (tri.empty()) return false;
    std::vector<Vec2> p = poly;
    if (p.size() >= 2 && p.front().x == p.back().x && p.front().y == p.back().y) p.pop_back();
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& v : p) pushVertex(mesh, v.x, y, v.y, 0.f, faceUp ? 1.f : -1.f, 0.f, color);
    // triangulate даёт обход против часовой в осях (x, z-как-y). Если смотреть сверху (+Y вниз на карту) с осью z на юг, нужно перевернуть.
    for (std::size_t i = 0; i < tri.size(); i += 3) {
        mesh.indices.push_back(base + tri[i]);
        mesh.indices.push_back(base + tri[i + 2]);
        mesh.indices.push_back(base + tri[i + 1]);
    }
    return true;
}

// Лента дороги: прямоугольник на каждый сегмент и диск в каждом стыке. Вершины в XZ, высота y.
int addRoad(GeoMesh& mesh, const std::vector<Vec2>& line, float halfWidth, float y, Color color, int sides) {
    int segments = 0;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const Vec2 a = line[i], b = line[i + 1];
        const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
        if (len < 0.05f) continue;
        const float nx = -dz / len * halfWidth, nz = dx / len * halfWidth;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        pushVertex(mesh, a.x + nx, y, a.y + nz, 0, 1, 0, color);
        pushVertex(mesh, a.x - nx, y, a.y - nz, 0, 1, 0, color);
        pushVertex(mesh, b.x - nx, y, b.y - nz, 0, 1, 0, color);
        pushVertex(mesh, b.x + nx, y, b.y + nz, 0, 1, 0, color);
        // Нормаль вверх: порядок (0,1,2),(0,2,3) даёт вид сверху против часовой при оси z на юг, если развернуть — как в addFlatPolygon.
        mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        ++segments;
    }
    // Диски в стыках (кроме концов, где достаточно прямого среза).
    for (std::size_t i = 1; i + 1 < line.size(); ++i) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        pushVertex(mesh, line[i].x, y, line[i].y, 0, 1, 0, color);
        for (int k = 0; k < sides; ++k) {
            const float a = 2.f * kPi * static_cast<float>(k) / static_cast<float>(sides);
            pushVertex(mesh, line[i].x + std::cos(a) * halfWidth, y, line[i].y + std::sin(a) * halfWidth, 0, 1, 0, color);
        }
        for (int k = 0; k < sides; ++k) {
            const auto cur = base + 1 + static_cast<std::uint32_t>(k), nxt = base + 1 + static_cast<std::uint32_t>((k + 1) % sides);
            mesh.indices.insert(mesh.indices.end(), {base, nxt, cur});
        }
    }
    return segments;
}

// Здание: стены (наружу) + плоская крыша. Контур — любого направления обхода.
bool addBuilding(GeoMesh& mesh, std::vector<Vec2> footprint, float height, Color wall, Color roof) {
    if (footprint.size() >= 2 && footprint.front().x == footprint.back().x && footprint.front().y == footprint.back().y) footprint.pop_back();
    if (footprint.size() < 3) return false;
    const auto roofTri = triangulate(footprint);
    if (roofTri.empty()) return false;
    // Приводим контур к одному направлению: signedArea в осях (x, z) > 0.
    if (signedArea(footprint) < 0) std::reverse(footprint.begin(), footprint.end());
    const auto roofTriOriented = triangulate(footprint);  // после разворота индексы другие
    const std::size_t n = footprint.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = footprint[i], b = footprint[(i + 1) % n];
        const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-3f) continue;
        // Контур (x, z) с signedArea > 0: внешняя нормаль ребра — (dz, -dx)/len.
        const float nx = dz / len, nz = -dx / len;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        pushVertex(mesh, a.x, 0.f, a.y, nx, 0, nz, wall);
        pushVertex(mesh, b.x, 0.f, b.y, nx, 0, nz, wall);
        pushVertex(mesh, b.x, height, b.y, nx, 0, nz, wall);
        pushVertex(mesh, a.x, height, a.y, nx, 0, nz, wall);
        mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& v : footprint) pushVertex(mesh, v.x, height, v.y, 0, 1, 0, roof);
    for (std::size_t i = 0; i < roofTriOriented.size(); i += 3) {
        mesh.indices.push_back(base + roofTriOriented[i]);
        mesh.indices.push_back(base + roofTriOriented[i + 2]);
        mesh.indices.push_back(base + roofTriOriented[i + 1]);
    }
    return true;
}

Vec2 centroid(const std::vector<Vec2>& p) {
    Vec2 c;
    for (const Vec2& v : p) {
        c.x += v.x;
        c.y += v.y;
    }
    const float n = p.empty() ? 1.f : static_cast<float>(p.size());
    return {c.x / n, c.y / n};
}

float parseMeters(std::string_view text) {
    if (text.empty()) return 0.f;
    const std::string s(text);
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end == s.c_str() ? 0.f : v;
}
}  // namespace

float roadWidth(std::string_view h) {
    if (h == "motorway") return 14.f;
    if (h == "trunk") return 12.f;
    if (h == "primary") return 10.f;
    if (h == "secondary") return 9.f;
    if (h == "tertiary" || h == "unclassified") return 8.f;
    if (h == "residential" || h == "living_street") return 6.f;
    if (h == "motorway_link" || h == "trunk_link" || h == "primary_link" || h == "secondary_link" || h == "tertiary_link") return 5.f;
    if (h == "service") return 4.f;
    if (h == "pedestrian") return 5.f;
    if (h == "footway" || h == "path" || h == "steps") return 2.f;
    if (h == "cycleway") return 2.5f;
    return 0.f;
}

float buildingHeight(const OsmWay& way, const CityBuildSettings& s) {
    float h = parseMeters(way.tag("height"));
    if (h <= 0.f) {
        const float levels = parseMeters(way.tag("building:levels"));
        if (levels > 0.f) h = levels * s.levelHeight + 1.f;  // + крыша и цоколь
    }
    if (h <= 0.f) h = s.defaultBuildingHeight;
    return std::clamp(h, 2.5f, s.maxBuildingHeight);
}

std::map<CellCoord, CityChunk> buildCity(const OsmData& data, const GeoProjection& projection, const CityBuildSettings& settings, CityStats* stats) {
    std::map<CellCoord, CityChunk> chunks;
    CityStats st;
    CellGrid grid(settings.cellSize);
    bool haveBounds = false;
    const auto grow = [&](float x, float z) {
        if (!haveBounds) {
            st.minX = st.maxX = x;
            st.minZ = st.maxZ = z;
            haveBounds = true;
        }
        st.minX = std::min(st.minX, x);
        st.maxX = std::max(st.maxX, x);
        st.minZ = std::min(st.minZ, z);
        st.maxZ = std::max(st.maxZ, z);
    };
    const auto project = [&](const OsmWay& way) {
        std::vector<Vec2> pts;
        pts.reserve(way.nodes.size());
        for (std::int64_t id : way.nodes) {
            const auto it = data.nodes.find(id);
            if (it == data.nodes.end()) continue;
            Vec2 p;
            projection.toLocal(it->second.lat, it->second.lon, p.x, p.y);
            pts.push_back(p);
        }
        return pts;
    };
    const auto chunkAt = [&](Vec2 p) -> CityChunk& { return chunks[grid.cellOf(p.x, p.y)]; };

    for (const OsmWay& way : data.ways) {
        const std::string_view highway = way.tag("highway"), building = way.tag("building");
        const bool isWater = way.tag("natural") == "water" || way.tag("waterway") == "riverbank" || way.tag("landuse") == "reservoir";
        const bool isPark = way.tag("leisure") == "park" || way.tag("leisure") == "garden" || way.tag("landuse") == "grass" || way.tag("landuse") == "forest" ||
                            way.tag("natural") == "wood";
        if (!building.empty() && building != "no") {
            auto pts = project(way);
            if (!way.closed() || pts.size() < 4 || std::fabs(signedArea(pts)) < settings.minBuildingArea) {
                ++st.skipped;
                continue;
            }
            CityChunk& chunk = chunkAt(centroid(pts));
            if (addBuilding(chunk.buildings, pts, buildingHeight(way, settings), buildingColor(way.id), {0.30f, 0.30f, 0.32f})) {
                ++chunk.buildingCount;
                ++st.buildings;
                for (const Vec2& p : pts) grow(p.x, p.y);
            } else {
                ++st.skipped;
            }
        } else if (!highway.empty()) {
            const float width = roadWidth(highway);
            if (width <= 0.f) continue;
            auto pts = project(way);
            if (pts.size() < 2) continue;
            // Дорога делится по ячейкам: каждый сегмент идёт в ячейку своей середины, стыки добавляются вместе с сегментом.
            const float lift = settings.roadLift + (14.f - width) * 0.002f;  // шире — ниже, чтобы перекрытия не мерцали
            const Color color = roadColor(highway);
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const std::vector<Vec2> seg = {pts[i], pts[i + 1]};
                const Vec2 mid{(pts[i].x + pts[i + 1].x) * 0.5f, (pts[i].y + pts[i + 1].y) * 0.5f};
                CityChunk& chunk = chunkAt(mid);
                chunk.roadSegments += addRoad(chunk.ground, seg, width * 0.5f, lift, color, static_cast<int>(settings.junctionSides));
                // Диск в конце сегмента, чтобы поворот не оставлял щелей (концы дороги тоже получают круглую «шапку»).
                const auto base = static_cast<std::uint32_t>(chunk.ground.vertices.size());
                const int sides = static_cast<int>(settings.junctionSides);
                pushVertex(chunk.ground, pts[i + 1].x, lift, pts[i + 1].y, 0, 1, 0, color);
                for (int k = 0; k < sides; ++k) {
                    const float a = 2.f * kPi * static_cast<float>(k) / static_cast<float>(sides);
                    pushVertex(chunk.ground, pts[i + 1].x + std::cos(a) * width * 0.5f, lift, pts[i + 1].y + std::sin(a) * width * 0.5f, 0, 1, 0, color);
                }
                for (int k = 0; k < sides; ++k)
                    chunk.ground.indices.insert(chunk.ground.indices.end(), {base, base + 1 + static_cast<std::uint32_t>((k + 1) % sides), base + 1 + static_cast<std::uint32_t>(k)});
                grow(pts[i].x, pts[i].y);
                grow(pts[i + 1].x, pts[i + 1].y);
            }
            ++st.roads;
        } else if (isWater || isPark) {
            auto pts = project(way);
            if (!way.closed() || pts.size() < 4) {
                ++st.skipped;
                continue;
            }
            CityChunk& chunk = chunkAt(centroid(pts));
            const Color color = isWater ? Color{0.16f, 0.30f, 0.42f} : Color{0.30f, 0.46f, 0.26f};
            if (addFlatPolygon(chunk.ground, pts, isWater ? 0.02f : 0.03f, color)) {
                ++chunk.areaCount;
                ++(isWater ? st.waters : st.parks);
                for (const Vec2& p : pts) grow(p.x, p.y);
            } else {
                ++st.skipped;
            }
        }
    }
    if (stats) *stats = st;
    return chunks;
}
}  // namespace han::world
