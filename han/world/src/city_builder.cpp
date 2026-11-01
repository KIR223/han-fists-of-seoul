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

// Рельеф на время построения (buildCity однопоточный): все вершины поднимаются на высоту земли в своей точке, а детали зданий — на общую
// «верхнюю отметку основания» (крыши плоские): gFixed — брать gFixedBase вместо высоты земли.
const Heightmap* gTerrain = nullptr;
bool gFixed = false, gHaveBase = false;
float gFixedBase = 0.f;
float groundAt(float x, float z) { return gTerrain ? gTerrain->heightAt(x, z) : 0.f; }

void pushVertex(GeoMesh& m, float x, float y, float z, float nx, float ny, float nz, Color c, float u = 0.f, float v = 0.f) {
    y += gFixed ? gFixedBase : groundAt(x, z);
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

Vec2 centroid(const std::vector<Vec2>& p) {
    Vec2 c;
    for (const Vec2& v : p) {
        c.x += v.x;
        c.y += v.y;
    }
    const float n = p.empty() ? 1.f : static_cast<float>(p.size());
    return {c.x / n, c.y / n};
}

// Точка внутри многоугольника (чётно-нечётное правило).
bool insidePolygon(const std::vector<Vec2>& poly, float x, float z) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if ((poly[i].y > z) != (poly[j].y > z) && x < (poly[j].x - poly[i].x) * (z - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    }
    return in;
}

// Вертикальная плоскость a-b от y0 до y1 с нормалью (nx, nz); UV: u — метры вдоль / uTile, v — (y1 - y) / vTile + vOffset.
void addWallQuad(GeoMesh& mesh, Vec2 a, Vec2 b, float y0, float y1, float nx, float nz, Color c, float u0, float u1, float v0, float v1, bool groundBottom = false) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const bool saved = gFixed;
    gFixed = gHaveBase && !groundBottom;  // низ стены садится на землю, верх и промежуточные отметки — на основание здания
    if (groundBottom && gTerrain) y0 -= 0.5f;  // стена уходит под землю: на склоне и рядом с чуть опущенной сеткой земли щелей не видно
    pushVertex(mesh, a.x, y0, a.y, nx, 0, nz, c, u0, v1);
    pushVertex(mesh, b.x, y0, b.y, nx, 0, nz, c, u1, v1);
    gFixed = gHaveBase;
    pushVertex(mesh, b.x, y1, b.y, nx, 0, nz, c, u1, v0);
    pushVertex(mesh, a.x, y1, a.y, nx, 0, nz, c, u0, v0);
    gFixed = saved;
    mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

// Параллелепипед (без низа) для крыш: баки, лестничные клетки, кондиционеры.
void addBox(GeoMesh& mesh, float cx, float cz, float hx, float hz, float y0, float y1, Color c, GeoMaterial material, float tile) {
    const auto first = static_cast<std::uint32_t>(mesh.indices.size());
    const Vec2 p[4] = {{cx - hx, cz - hz}, {cx + hx, cz - hz}, {cx + hx, cz + hz}, {cx - hx, cz + hz}};
    const float nxs[4] = {0.f, 1.f, 0.f, -1.f}, nzs[4] = {-1.f, 0.f, 1.f, 0.f};
    for (int i = 0; i < 4; ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % 4];
        const float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        addWallQuad(mesh, a, b, y0, y1, nxs[i], nzs[i], c, 0.f, len / tile, 0.f, (y1 - y0) / tile);
    }
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& v : p) pushVertex(mesh, v.x, y1, v.y, 0, 1, 0, c, v.x / tile, v.y / tile);
    mesh.indices.insert(mesh.indices.end(), {base, base + 3, base + 2, base, base + 2, base + 1});
    mesh.tag(material, first);
}

struct BuildingLook {
    GeoMaterial wall = GeoMaterial::None;
    bool shop = false;      // первый этаж — витрины и вывески
    bool parapet = true;    // бортик по краю крыши
    bool rooftop = true;    // лестничная клетка / бак на крыше
    float shopTile = 19.2f; // метров стены на одну плитку текстуры витрин
};

// Здание: стены (наружу) + плоская крыша, витрины на первом этаже, парапет и надстройка на крыше. Контур — любого направления обхода.
bool addBuilding(GeoMesh& mesh, std::vector<Vec2> footprint, float height, Color wall, Color roof, const BuildingLook& look) {
    if (footprint.size() >= 2 && footprint.front().x == footprint.back().x && footprint.front().y == footprint.back().y) footprint.pop_back();
    if (footprint.size() < 3) return false;
    const auto roofTri = triangulate(footprint);
    if (roofTri.empty()) return false;
    // Приводим контур к одному направлению: signedArea в осях (x, z) > 0.
    if (signedArea(footprint) < 0) std::reverse(footprint.begin(), footprint.end());
    const auto roofTriOriented = triangulate(footprint);  // после разворота индексы другие
    const std::size_t n = footprint.size();
    // Основание: крыша плоская на высоте самой высокой точки земли под зданием; низ стен идёт по склону.
    float baseTop = 0.f;
    if (gTerrain) {
        baseTop = -1e9f;
        for (const Vec2& v : footprint) baseTop = std::max(baseTop, groundAt(v.x, v.y));
    }
    struct BaseScope {
        BaseScope(float base) { gHaveBase = gFixed = true; gFixedBase = base; }
        ~BaseScope() { gHaveBase = gFixed = false; gFixedBase = 0.f; }
    } baseScope(baseTop);
    constexpr float kShopHeight = 3.6f;  // высота витрины
    const float kShopBay = look.shopTile;  // ширина плитки вывесок (секции по 4,8 м)
    const bool shop = look.shop && height > kShopHeight + 2.f;
    float along = 0.f;  // пройденная длина периметра: u текстуры идёт непрерывно вдоль стены
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = footprint[i], b = footprint[(i + 1) % n];
        const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-3f) continue;
        // Контур (x, z) с signedArea > 0: внешняя нормаль ребра — (dz, -dx)/len.
        const float nx = dz / len, nz = -dx / len;
        const float u0 = along / kWallTile, u1 = (along + len) / kWallTile;
        const float yBottom = shop ? kShopHeight : 0.f;
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        addWallQuad(mesh, a, b, yBottom, height, nx, nz, wall, u0, u1, 0.f, (height - yBottom) / kWallTile, !shop);
        mesh.tag(look.wall, first);
        if (shop && len > 2.f) {
            const auto shopFirst = static_cast<std::uint32_t>(mesh.indices.size());
            // Витрина: нейтральный цвет (оттенок дома не красит вывески), u — в плитках по 19,2 м; со сдвигом по дому, чтобы вывески различались.
            const float shift = std::fmod(std::fabs(a.x * 0.37f + a.y * 0.61f), 1.f);
            addWallQuad(mesh, a, b, 0.f, kShopHeight, nx, nz, {1.f, 1.f, 1.f}, shift + along / kShopBay, shift + (along + len) / kShopBay, 0.f, 1.f, true);
            mesh.tag(GeoMaterial::Storefront, shopFirst);
        } else if (shop) {
            const auto shopFirst = static_cast<std::uint32_t>(mesh.indices.size());
            addWallQuad(mesh, a, b, 0.f, kShopHeight, nx, nz, wall, u0, u1, (height - kShopHeight) / kWallTile, height / kWallTile, true);
            mesh.tag(look.wall, shopFirst);
        }
        along += len;
    }
    // Парапет: бортик 0,8 м по краю крыши (наружная и внутренняя грани).
    if (look.parapet && height < 60.f) {
        const float rim = 0.8f;
        along = 0.f;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = footprint[i], b = footprint[(i + 1) % n];
            const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
            if (len < 1e-3f) continue;
            const float nx = dz / len, nz = -dx / len;
            const float u0 = along / kWallTile, u1 = (along + len) / kWallTile;
            const auto first = static_cast<std::uint32_t>(mesh.indices.size());
            addWallQuad(mesh, a, b, height, height + rim, nx, nz, wall, u0, u1, 0.f, rim / kWallTile);
            const Vec2 ai{a.x - nx * 0.25f, a.y - nz * 0.25f}, bi{b.x - nx * 0.25f, b.y - nz * 0.25f};  // внутренняя грань со стороны крыши
            addWallQuad(mesh, bi, ai, height, height + rim, -nx, -nz, wall, u0, u1, 0.f, rim / kWallTile);
            const auto capBase = static_cast<std::uint32_t>(mesh.vertices.size());  // верхняя кромка бортика
            pushVertex(mesh, a.x, height + rim, a.y, 0, 1, 0, wall, u0, 0.f);
            pushVertex(mesh, b.x, height + rim, b.y, 0, 1, 0, wall, u1, 0.f);
            pushVertex(mesh, bi.x, height + rim, bi.y, 0, 1, 0, wall, u1, 0.04f);
            pushVertex(mesh, ai.x, height + rim, ai.y, 0, 1, 0, wall, u0, 0.04f);
            mesh.indices.insert(mesh.indices.end(), {capBase, capBase + 2, capBase + 1, capBase, capBase + 3, capBase + 2});
            mesh.tag(look.wall == GeoMaterial::None ? GeoMaterial::None : GeoMaterial::WallConcrete, first);
            along += len;
        }
    }
    const auto roofFirst = static_cast<std::uint32_t>(mesh.indices.size());
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& v : footprint) pushVertex(mesh, v.x, height, v.y, 0, 1, 0, roof, v.x / kRoofTile, v.y / kRoofTile);
    for (std::size_t i = 0; i < roofTriOriented.size(); i += 3) {
        mesh.indices.push_back(base + roofTriOriented[i]);
        mesh.indices.push_back(base + roofTriOriented[i + 2]);
        mesh.indices.push_back(base + roofTriOriented[i + 1]);
    }
    mesh.tag(look.wall == GeoMaterial::None ? GeoMaterial::None : GeoMaterial::Roof, roofFirst);
    // Надстройка на крыше: лестничная клетка / бак 3,4 x 3,4 м, если четыре угла внутри контура и крыша достаточно большая.
    if (look.rooftop && look.wall != GeoMaterial::None && std::fabs(signedArea(footprint)) > 90.f && height > 7.f) {
        const Vec2 c = centroid(footprint);
        const float h = 1.7f;
        if (insidePolygon(footprint, c.x - h, c.y - h) && insidePolygon(footprint, c.x + h, c.y - h) && insidePolygon(footprint, c.x + h, c.y + h) &&
            insidePolygon(footprint, c.x - h, c.y + h))
            addBox(mesh, c.x, c.y, h, h, height, height + 2.6f, {0.85f, 0.85f, 0.85f}, GeoMaterial::WallConcrete, kWallTile);
    }
    return true;
}

// Тротуар вокруг здания: полоса шириной width наружу по каждому ребру (с продлением на ширину, чтобы углы не оставляли щелей).
void addApron(GeoMesh& mesh, std::vector<Vec2> footprint, float width, float y) {
    if (footprint.size() >= 2 && footprint.front().x == footprint.back().x && footprint.front().y == footprint.back().y) footprint.pop_back();
    if (footprint.size() < 3) return;
    if (signedArea(footprint) < 0) std::reverse(footprint.begin(), footprint.end());
    const auto first = static_cast<std::uint32_t>(mesh.indices.size());
    const auto vertsBefore = mesh.vertices.size();
    const std::size_t n = footprint.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = footprint[i], b = footprint[(i + 1) % n];
        const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
        if (len < 0.5f) continue;
        const float ux = dx / len, uz = dz / len, nx = uz, nz = -ux;  // наружная нормаль ребра
        const Vec2 a0{a.x - ux * width, a.y - uz * width}, b0{b.x + ux * width, b.y + uz * width};
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const Color c{1.f, 1.f, 1.f};
        pushVertex(mesh, a0.x, y, a0.y, 0, 1, 0, c);
        pushVertex(mesh, b0.x, y, b0.y, 0, 1, 0, c);
        pushVertex(mesh, b0.x + nx * width, y, b0.y + nz * width, 0, 1, 0, c);
        pushVertex(mesh, a0.x + nx * width, y, a0.y + nz * width, 0, 1, 0, c);
        mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    for (std::size_t i = vertsBefore; i < mesh.vertices.size(); ++i) {
        mesh.vertices[i].uv[0] = mesh.vertices[i].position[0] / 4.f;
        mesh.vertices[i].uv[1] = mesh.vertices[i].position[2] / 4.f;
    }
    mesh.tag(GeoMaterial::Sidewalk, first);
}

// Шестигранная призма: столбы, стволы.
void addPrism(GeoMesh& mesh, float cx, float cz, float r, float y0, float y1, Color c, GeoMaterial material) {
    const auto first = static_cast<std::uint32_t>(mesh.indices.size());
    constexpr int kSides = 6;
    for (int k = 0; k < kSides; ++k) {
        const float a0 = 2.f * kPi * static_cast<float>(k) / kSides, a1 = 2.f * kPi * static_cast<float>(k + 1) / kSides;
        const float am = 0.5f * (a0 + a1);
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const float nx = std::cos(am), nz = std::sin(am);
        pushVertex(mesh, cx + std::cos(a0) * r, y0, cz + std::sin(a0) * r, nx, 0, nz, c);
        pushVertex(mesh, cx + std::cos(a1) * r, y0, cz + std::sin(a1) * r, nx, 0, nz, c);
        pushVertex(mesh, cx + std::cos(a1) * r, y1, cz + std::sin(a1) * r, nx, 0, nz, c);
        pushVertex(mesh, cx + std::cos(a0) * r, y1, cz + std::sin(a0) * r, nx, 0, nz, c);
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    mesh.tag(material, first);
}

// Крона дерева: сплюснутая UV-сфера радиуса r с центром (cx, cy, cz).
void addCrown(GeoMesh& mesh, float cx, float cy, float cz, float r, Color c) {
    constexpr int kSeg = 8, kRings = 5;
    const auto first = static_cast<std::uint32_t>(mesh.indices.size());
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= kRings; ++i) {
        const float v = kPi * static_cast<float>(i) / kRings, sv = std::sin(v), cv = std::cos(v);
        for (int j = 0; j <= kSeg; ++j) {
            const float u = 2.f * kPi * static_cast<float>(j) / kSeg;
            const float nx = sv * std::cos(u), ny = cv, nz = sv * std::sin(u);
            const float shade = 0.75f + 0.25f * (ny * 0.5f + 0.5f);  // верх светлее: объём без освещения
            pushVertex(mesh, cx + nx * r, cy + ny * r * 0.8f, cz + nz * r, nx, ny, nz, {c.r * shade, c.g * shade, c.b * shade});
        }
    }
    for (int i = 0; i < kRings; ++i)
        for (int j = 0; j < kSeg; ++j) {
            const std::uint32_t a = base + static_cast<std::uint32_t>(i * (kSeg + 1) + j), b = a + 1, c2 = a + static_cast<std::uint32_t>(kSeg + 1), d = c2 + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c2, b, d, c2});
        }
    mesh.tag(GeoMaterial::Leaves, first);
}

void addTree(GeoMesh& mesh, float x, float z, std::uint32_t seed) {
    const float h = 3.2f + static_cast<float>(seed % 5) * 0.35f, r = 1.9f + static_cast<float>((seed >> 3) % 4) * 0.3f;
    addPrism(mesh, x, z, 0.14f, 0.f, h, {0.32f, 0.22f, 0.14f}, GeoMaterial::Trunk);
    const float g = 0.26f + static_cast<float>((seed >> 5) % 5) * 0.03f;
    addCrown(mesh, x, h + r * 0.55f, z, r, {0.16f, g, 0.10f});
}

// Фонарь: столб 7 м и вынесенная к дороге головка.
void addLamp(GeoMesh& mesh, float x, float z, float towardX, float towardZ) {
    addPrism(mesh, x, z, 0.09f, 0.f, 7.f, {0.30f, 0.31f, 0.33f}, GeoMaterial::Metal);
    addBox(mesh, x + towardX * 0.7f, z + towardZ * 0.7f, 0.45f, 0.45f, 6.9f, 7.15f, {0.34f, 0.35f, 0.37f}, GeoMaterial::Metal, 4.f);
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
    gTerrain = settings.terrain && settings.terrain->valid() ? settings.terrain : nullptr;
    gFixed = gHaveBase = false;
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

    std::vector<std::vector<Vec2>> footprints;  // построенные здания: деревья и фонари не ставятся внутрь
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
            BuildingLook look;
            look.wall = wallMaterial(way.id, height);
            const std::uint32_t seed = static_cast<std::uint32_t>((way.id < 0 ? -way.id : way.id) * 2246822519u) >> 7;
            look.shop = settings.buildingDetail && height <= 45.f && seed % 4 != 0;  // у трёх домов из четырёх на первом этаже лавки
            look.parapet = settings.buildingDetail && seed % 7 != 0;
            look.rooftop = settings.buildingDetail;
            look.shopTile = settings.shopTileMeters;
            if (addBuilding(chunk.buildings, pts, height, wallTint(way.id), {1.f, 1.f, 1.f}, look)) {
                if (settings.buildingDetail) addApron(chunk.ground, pts, 1.6f, 0.09f);
                footprints.push_back(pts);
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
    // Уличные объекты: деревья и фонари вдоль дорог (после зданий, чтобы не попадать внутрь).
    const auto insideAnyBuilding = [&](float x, float z) {
        for (const auto& fp : footprints) {
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (const Vec2& p : fp) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minZ = std::min(minZ, p.y);
                maxZ = std::max(maxZ, p.y);
            }
            if (x < minX - 1.f || x > maxX + 1.f || z < minZ - 1.f || z > maxZ + 1.f) continue;
            if (insidePolygon(fp, x, z)) return true;
        }
        return false;
    };
    if (settings.streetProps) {
        for (const OsmWay& way : data.ways) {
            const std::string_view highway = way.tag("highway");
            const float width = highway.empty() ? 0.f : roadWidth(highway);
            if (width < 6.f || highway == "motorway" || highway == "trunk" || highway == "service") continue;
            const auto pts = project(way);
            float sinceTree = 8.f, sinceLamp = 15.f;
            int side = 1;
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const Vec2 a = pts[i], b = pts[i + 1];
                const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
                if (len < 1.f) continue;
                const float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
                for (float t = 0.f; t < len; t += 2.f) {
                    sinceTree += 2.f;
                    sinceLamp += 2.f;
                    const float px = a.x + ux * t, pz = a.y + uz * t;
                    if (sinceTree >= 20.f) {
                        const float off = width * 0.5f + 2.6f;
                        const float tx = px + nx * off * static_cast<float>(side), tz = pz + nz * off * static_cast<float>(side);
                        if (!insideAnyBuilding(tx, tz)) {
                            addTree(chunkAt({tx, tz}).buildings, tx, tz, static_cast<std::uint32_t>(std::fabs(tx * 31.f + tz * 17.f)));
                            side = -side;
                        }
                        sinceTree = 0.f;
                    }
                    if (sinceLamp >= 32.f) {
                        const float off = width * 0.5f + 1.2f;
                        const float s = static_cast<float>(-side);
                        const float lx = px + nx * off * s, lz = pz + nz * off * s;
                        if (!insideAnyBuilding(lx, lz)) {
                            CityChunk& lampChunk = chunkAt({lx, lz});
                            addLamp(lampChunk.buildings, lx, lz, -nx * s, -nz * s);
                            lampChunk.lamps.push_back({lx - nx * s * 0.7f, 6.8f + groundAt(lx, lz), lz - nz * s * 0.7f});
                        }
                        sinceLamp = 0.f;
                    }
                }
            }
        }
    }
    // Парки и леса: деревья на редкой сетке с разбросом (шаг 12 м), только внутри контура.
    if (settings.streetProps) {
        for (const OsmWay& way : data.ways) {
            const bool park = way.tag("leisure") == "park" || way.tag("leisure") == "garden" || way.tag("landuse") == "forest" || way.tag("natural") == "wood" ||
                              way.tag("landuse") == "grass";
            if (!park || !way.closed()) continue;
            const auto pts = project(way);
            if (pts.size() < 4) continue;
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (const Vec2& p : pts) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minZ = std::min(minZ, p.y);
                maxZ = std::max(maxZ, p.y);
            }
            if ((maxX - minX) * (maxZ - minZ) > 4.0e6f) continue;  // огромные лесные массивы — не в городском слое
            const bool wood = way.tag("natural") == "wood" || way.tag("landuse") == "forest";
            const float step = wood ? 9.f : 13.f;
            int placed = 0;
            for (float x = minX; x < maxX && placed < 400; x += step)
                for (float z = minZ; z < maxZ && placed < 400; z += step) {
                    const auto h = static_cast<std::uint32_t>(std::fabs(x * 12.9898f + z * 78.233f) * 1000.f);
                    const float jx = x + (static_cast<float>(h % 100) / 100.f - 0.5f) * step * 0.7f;
                    const float jz = z + (static_cast<float>((h / 100) % 100) / 100.f - 0.5f) * step * 0.7f;
                    if (!insidePolygon(pts, jx, jz) || insideAnyBuilding(jx, jz)) continue;
                    addTree(chunkAt({jx, jz}).buildings, jx, jz, h);
                    ++placed;
                }
        }
    }
    // Земля: сетка по высотам во всех ячейках с городом и на одну ячейку вокруг (иначе у края карты обрыв).
    if (gTerrain && settings.terrainMesh && !chunks.empty()) {
        TerrainSettings ts;
        ts.cellSize = settings.cellSize;
        const CellCoord lo = grid.cellOf(st.minX, st.minZ), hi = grid.cellOf(st.maxX, st.maxZ);
        for (int cx = lo.x - 1; cx <= hi.x + 1; ++cx)
            for (int cz = lo.z - 1; cz <= hi.z + 1; ++cz) {
                const GeoMesh land = buildTerrainCell(*gTerrain, {cx, cz}, ts);
                if (!land.empty()) chunks[{cx, cz}].ground.append(land);
            }
    }
    for (auto& [cell, chunk] : chunks) {
        chunk.ground.groupByMaterial();
        chunk.buildings.groupByMaterial();
    }
    if (stats) *stats = st;
    gTerrain = nullptr;
    return chunks;
}
}  // namespace han::world
