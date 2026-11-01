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

// Текстуры города несут цвет сами: вершинный цвет остаётся лёгким оттенком (множитель), чтобы фасады различались.
Color roadColor(std::string_view highway) {
    if (highway == "footway" || highway == "pedestrian" || highway == "path" || highway == "steps" || highway == "cycleway") return {1.f, 0.97f, 0.93f};
    if (highway == "motorway" || highway == "trunk" || highway == "primary") return {0.80f, 0.80f, 0.82f};
    return {1.f, 1.f, 1.f};
}
[[maybe_unused]] Color roadColorFlat(std::string_view highway) {
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

// Оттенок фасада: светлее и теплее/холоднее по номеру дома, в диапазоне 0.78..1.0 (текстура остаётся главной).
Color wallTint(std::int64_t id) {
    const Color p = buildingColor(id);
    const float k = 0.78f + 0.22f * ((p.r + p.g + p.b) / 3.f - 0.5f) / 0.3f;
    return {std::clamp(k + (p.r - p.b) * 0.3f, 0.6f, 1.f), std::clamp(k, 0.6f, 1.f), std::clamp(k - (p.r - p.b) * 0.3f, 0.6f, 1.f)};
}

// Материал стен: высокие — бетон, низкие — кирпич и штукатурка (виллы Сеула), середина вперемешку; стабильно по номеру дома.
GeoMaterial wallMaterial(std::int64_t id, float height) {
    const std::uint32_t h = static_cast<std::uint32_t>((id < 0 ? -id : id) * 2654435761u) >> 8;
    if (height >= 24.f) return GeoMaterial::WallConcrete;
    if (height >= 12.f) return h % 2 == 0 ? GeoMaterial::WallConcrete : GeoMaterial::WallPlaster;
    return h % 5 < 2 ? GeoMaterial::WallBrick : GeoMaterial::WallPlaster;
}

// Планарные UV для горизонтальных вершин, добавленных после from: метры карты / размер плитки.
void planarUv(GeoMesh& m, std::size_t from, float tile) {
    for (std::size_t i = from; i < m.vertices.size(); ++i) {
        m.vertices[i].uv[0] = m.vertices[i].position[0] / tile;
        m.vertices[i].uv[1] = m.vertices[i].position[2] / tile;
    }
}

// Размер плитки текстуры в метрах (одна плитка Poly Haven 1k ≈ 2–4 м в жизни; берём крупнее для города).
constexpr float kRoadTile = 6.f, kParkTile = 8.f, kWallTile = 6.4f, kRoofTile = 5.f;

void pushVertex(GeoMesh& m, float x, float y, float z, float nx, float ny, float nz, Color c, float u = 0.f, float v = 0.f) {
    GeoVertex g{{x, y, z}, {nx, ny, nz}, {c.r, c.g, c.b}};
    g.uv[0] = u;
    g.uv[1] = v;
    m.vertices.push_back(g);
}

// Горизонтальный многоугольник на высоте y, нормаль вверх. Возвращает false, если не удалось разбить.
bool addFlatPolygon(GeoMesh& mesh, const std::vector<Vec2>& poly, float y, Color color, float tile = kParkTile, bool faceUp = true) {
    const auto tri = triangulate(poly);
    if (tri.empty()) return false;
    std::vector<Vec2> p = poly;
    if (p.size() >= 2 && p.front().x == p.back().x && p.front().y == p.back().y) p.pop_back();
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& v : p) pushVertex(mesh, v.x, y, v.y, 0.f, faceUp ? 1.f : -1.f, 0.f, color, v.x / tile, v.y / tile);
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

// Полоса разметки вдоль отрезка a-b на расстоянии offset от оси (влево от направления a→b), шириной w, высотой y.
// dash > 0 — штрих длиной dash через gap; концы отрезка отступают на trim (перекрёстки остаются чистыми).
int addStripe(GeoMesh& mesh, Vec2 a, Vec2 b, float offset, float w, float y, Color color, float dash, float gap, float trim) {
    const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
    if (len < 2.f * trim + 1.f) return 0;
    const float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    int quads = 0;
    float t = trim;
    const float end = len - trim;
    while (t < end) {
        const float t1 = dash > 0.f ? std::min(end, t + dash) : end;
        const float cx0 = a.x + ux * t + nx * offset, cz0 = a.y + uz * t + nz * offset;
        const float cx1 = a.x + ux * t1 + nx * offset, cz1 = a.y + uz * t1 + nz * offset;
        const float hx = nx * w * 0.5f, hz = nz * w * 0.5f;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        pushVertex(mesh, cx0 + hx, y, cz0 + hz, 0, 1, 0, color);
        pushVertex(mesh, cx0 - hx, y, cz0 - hz, 0, 1, 0, color);
        pushVertex(mesh, cx1 - hx, y, cz1 - hz, 0, 1, 0, color);
        pushVertex(mesh, cx1 + hx, y, cz1 + hz, 0, 1, 0, color);
        mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        ++quads;
        if (dash <= 0.f) break;
        t += dash + gap;
    }
    return quads;
}

// Разметка по классу дороги: широкие — жёлтая сплошная осевая и белые краевые; магистрали — ещё белые пунктиры полос.
void addRoadMarkings(GeoMesh& mesh, Vec2 a, Vec2 b, float width, std::string_view highway, float roadY) {
    if (width < 8.f || highway == "footway" || highway == "pedestrian" || highway == "path" || highway == "steps" || highway == "cycleway") return;
    const float y = roadY + 0.06f;
    const Color yellow{0.95f, 0.78f, 0.12f}, white{0.92f, 0.92f, 0.90f};
    const float half = width * 0.5f;
    addStripe(mesh, a, b, 0.f, 0.18f, y, yellow, 0.f, 0.f, 3.5f);
    addStripe(mesh, a, b, half - 0.3f, 0.14f, y, white, 0.f, 0.f, 3.5f);
    addStripe(mesh, a, b, -(half - 0.3f), 0.14f, y, white, 0.f, 0.f, 3.5f);
    if (width >= 10.f) {  // полосы: пунктир на четвертях ширины
        addStripe(mesh, a, b, half * 0.5f, 0.12f, y, white, 3.f, 6.f, 3.5f);
        addStripe(mesh, a, b, -half * 0.5f, 0.12f, y, white, 3.f, 6.f, 3.5f);
    }
}

// Здание: стены (наружу) + плоская крыша. Контур — любого направления обхода.
bool addBuilding(GeoMesh& mesh, std::vector<Vec2> footprint, float height, Color wall, Color roof, GeoMaterial wallMat = GeoMaterial::None) {
    if (footprint.size() >= 2 && footprint.front().x == footprint.back().x && footprint.front().y == footprint.back().y) footprint.pop_back();
    if (footprint.size() < 3) return false;
    const auto roofTri = triangulate(footprint);
    if (roofTri.empty()) return false;
    // Приводим контур к одному направлению: signedArea в осях (x, z) > 0.
    if (signedArea(footprint) < 0) std::reverse(footprint.begin(), footprint.end());
    const auto roofTriOriented = triangulate(footprint);  // после разворота индексы другие
    const std::size_t n = footprint.size();
    const auto wallFirst = static_cast<std::uint32_t>(mesh.indices.size());
    float along = 0.f;  // пройденная длина периметра: u текстуры идёт непрерывно вдоль стены
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = footprint[i], b = footprint[(i + 1) % n];
        const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-3f) continue;
        // Контур (x, z) с signedArea > 0: внешняя нормаль ребра — (dz, -dx)/len.
        const float nx = dz / len, nz = -dx / len;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const float u0 = along / kWallTile, u1 = (along + len) / kWallTile;
        const float v1 = height / kWallTile;
        pushVertex(mesh, a.x, 0.f, a.y, nx, 0, nz, wall, u0, v1);
        pushVertex(mesh, b.x, 0.f, b.y, nx, 0, nz, wall, u1, v1);
        pushVertex(mesh, b.x, height, b.y, nx, 0, nz, wall, u1, 0.f);
        pushVertex(mesh, a.x, height, a.y, nx, 0, nz, wall, u0, 0.f);
        mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        along += len;
    }
    mesh.tag(wallMat, wallFirst);
    const auto roofFirst = static_cast<std::uint32_t>(mesh.indices.size());
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& v : footprint) pushVertex(mesh, v.x, height, v.y, 0, 1, 0, roof, v.x / kRoofTile, v.y / kRoofTile);
    for (std::size_t i = 0; i < roofTriOriented.size(); i += 3) {
        mesh.indices.push_back(base + roofTriOriented[i]);
        mesh.indices.push_back(base + roofTriOriented[i + 2]);
        mesh.indices.push_back(base + roofTriOriented[i + 1]);
    }
    mesh.tag(wallMat == GeoMaterial::None ? GeoMaterial::None : GeoMaterial::Roof, roofFirst);
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

std::vector<std::vector<Vec2>> buildingFootprints(const OsmData& data, const GeoProjection& projection, float minArea) {
    std::vector<std::vector<Vec2>> out;
    for (const OsmWay& way : data.ways) {
        const std::string_view building = way.tag("building");
        if (building.empty() || building == "no" || !way.closed()) continue;
        std::vector<Vec2> pts;
        for (std::int64_t id : way.nodes) {
            const auto it = data.nodes.find(id);
            if (it == data.nodes.end()) continue;
            Vec2 p;
            projection.toLocal(it->second.lat, it->second.lon, p.x, p.y);
            pts.push_back(p);
        }
        if (pts.size() >= 2 && pts.front().x == pts.back().x && pts.front().y == pts.back().y) pts.pop_back();
        if (pts.size() >= 3 && std::fabs(signedArea(pts)) >= minArea) out.push_back(std::move(pts));
    }
    return out;
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
            const float height = buildingHeight(way, settings);
            if (addBuilding(chunk.buildings, pts, height, wallTint(way.id), {1.f, 1.f, 1.f}, wallMaterial(way.id, height))) {
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
                const auto vertsBefore = chunk.ground.vertices.size();
                const auto indexBefore = static_cast<std::uint32_t>(chunk.ground.indices.size());
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
                planarUv(chunk.ground, vertsBefore, kRoadTile);
                chunk.ground.tag(GeoMaterial::Road, indexBefore);
                const auto markFirst = static_cast<std::uint32_t>(chunk.ground.indices.size());
                addRoadMarkings(chunk.ground, pts[i], pts[i + 1], width, highway, lift);
                chunk.ground.tag(GeoMaterial::Marking, markFirst);
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
            const Color color = isWater ? Color{0.16f, 0.30f, 0.42f} : Color{1.f, 1.f, 1.f};
            const auto indexBefore = static_cast<std::uint32_t>(chunk.ground.indices.size());
            if (addFlatPolygon(chunk.ground, pts, isWater ? 0.02f : 0.03f, color, kParkTile)) {
                chunk.ground.tag(isWater ? GeoMaterial::Water : GeoMaterial::Park, indexBefore);
                ++chunk.areaCount;
                ++(isWater ? st.waters : st.parks);
                for (const Vec2& p : pts) grow(p.x, p.y);
            } else {
                ++st.skipped;
            }
        }
    }
    for (auto& [cell, chunk] : chunks) {
        chunk.ground.groupByMaterial();
        chunk.buildings.groupByMaterial();
    }
    if (stats) *stats = st;
    return chunks;
}
}  // namespace han::world
