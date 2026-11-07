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
void addRoadMarkings(GeoMesh& mesh, Vec2 a, Vec2 b, float width, std::string_view highway, float roadY, bool doubleCenter = false) {
    if (width < 8.f || highway == "footway" || highway == "pedestrian" || highway == "path" || highway == "steps" || highway == "cycleway") return;
    const float y = roadY + 0.06f;
    const Color yellow{0.95f, 0.78f, 0.12f}, white{0.92f, 0.92f, 0.90f};
    const float half = width * 0.5f;
    if (doubleCenter) {
        addStripe(mesh, a, b, -0.14f, 0.12f, y, yellow, 0.f, 0.f, 3.5f);
        addStripe(mesh, a, b, 0.14f, 0.12f, y, yellow, 0.f, 0.f, 3.5f);
    } else addStripe(mesh, a, b, 0.f, 0.18f, y, yellow, 0.f, 0.f, 3.5f);
    addStripe(mesh, a, b, half - 0.3f, 0.14f, y, white, 0.f, 0.f, 3.5f);
    addStripe(mesh, a, b, -(half - 0.3f), 0.14f, y, white, 0.f, 0.f, 3.5f);
    if (width >= 10.f) {  // полосы: пунктир на четвертях ширины
        addStripe(mesh, a, b, half * 0.5f, 0.12f, y, white, 3.f, 6.f, 3.5f);
        addStripe(mesh, a, b, -half * 0.5f, 0.12f, y, white, 3.f, 6.f, 3.5f);
    }
}

void addWallQuad(GeoMesh& mesh, Vec2 a, Vec2 b, float y0, float y1, float nx, float nz, Color c, float u0, float u1, float v0, float v1, bool groundBottom = false);

void addStreetEdges(GeoMesh& mesh, Vec2 a, Vec2 b, float width, float roadY, const CityStyle& style) {
    const float length = std::hypot(b.x - a.x, b.y - a.y);
    if (length < 1.f) return;
    const float nx = -(b.y - a.y) / length, nz = (b.x - a.x) / length;
    const float half = width * 0.5f;
    for (const float sign : {-1.f, 1.f}) {
        const Vec2 left{a.x + nx * half * sign, a.y + nz * half * sign};
        const Vec2 right{b.x + nx * half * sign, b.y + nz * half * sign};
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        const auto start = mesh.vertices.size();
        addFlatPolygon(mesh, {left, right, {right.x + nx * style.sidewalkWidth * sign, right.y + nz * style.sidewalkWidth * sign},
                             {left.x + nx * style.sidewalkWidth * sign, left.y + nz * style.sidewalkWidth * sign}},
                       roadY + style.curbHeight, {0.92f, 0.91f, 0.88f}, 4.f);
        planarUv(mesh, start, 4.f);
        mesh.tag(GeoMaterial::Sidewalk, first);
        const auto curb = static_cast<std::uint32_t>(mesh.indices.size());
        addWallQuad(mesh, left, right, roadY, roadY + style.curbHeight, -nx * sign, -nz * sign, {0.7f, 0.71f, 0.69f}, 0, length / 2.f, 0, 0.04f);
        // Обратная грань: порядок контура дороги может быть любым.
        addWallQuad(mesh, right, left, roadY, roadY + style.curbHeight, nx * sign, nz * sign, {0.7f, 0.71f, 0.69f}, 0, length / 2.f, 0, 0.04f);
        mesh.tag(GeoMaterial::Terrain, curb);
    }
}

void addCrosswalk(GeoMesh& mesh, Vec2 center, float ux, float uz, float width, float y, const CityStyle& style) {
    const Vec2 a{center.x - ux * style.crosswalkLength * 0.5f, center.y - uz * style.crosswalkLength * 0.5f};
    const Vec2 b{center.x + ux * style.crosswalkLength * 0.5f, center.y + uz * style.crosswalkLength * 0.5f};
    for (float offset = -width * 0.5f + style.crosswalkSpacing; offset < width * 0.5f - style.crosswalkBar; offset += style.crosswalkSpacing)
        addStripe(mesh, a, b, offset, style.crosswalkBar, y, {0.92f, 0.91f, 0.87f}, 0, 0, 0);
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
void addWallQuad(GeoMesh& mesh, Vec2 a, Vec2 b, float y0, float y1, float nx, float nz, Color c, float u0, float u1, float v0, float v1, bool groundBottom) {
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

void addOrientedBox(GeoMesh& mesh, float cx, float cz, float hx, float hz, float y0, float y1, float ux, float uz, Color color, GeoMaterial material) {
    const auto begin = mesh.vertices.size();
    addBox(mesh, 0.f, 0.f, hx, hz, y0, y1, color, material, kWallTile);
    for (std::size_t i = begin; i < mesh.vertices.size(); ++i) {
        auto& v = mesh.vertices[i];
        const float x = v.position[0], z = v.position[2];
        const float nx = v.normal[0], nz = v.normal[2];
        v.position[0] = cx + x * ux - z * uz;
        v.position[2] = cz + x * uz + z * ux;
        if (!gFixed) v.position[1] += groundAt(v.position[0], v.position[2]) - groundAt(x, z);
        v.normal[0] = nx * ux - nz * uz;
        v.normal[2] = nx * uz + nz * ux;
    }
}

void addPrism(GeoMesh& mesh, float cx, float cz, float r, float y0, float y1, Color color, GeoMaterial material);

struct BuildingLook {
    GeoMaterial wall = GeoMaterial::None;
    GeoMaterial roof = GeoMaterial::Roof;
    bool shop = false;      // первый этаж — витрины и вывески
    bool parapet = true;    // бортик по краю крыши
    bool rooftop = true;    // лестничная клетка / бак на крыше
    float shopTile = 19.2f; // метров стены на одну плитку текстуры витрин
    const CityStyle* style = nullptr;
    std::uint32_t seed = 0;
};

// Здание: стены (наружу) + плоская крыша, витрины на первом этаже, парапет и надстройка на крыше. Контур — любого направления обхода.
bool addBuilding(GeoMesh& mesh, std::vector<Vec2> footprint, float height, Color wall, Color roof, const BuildingLook& look, std::vector<WallCollider>* colliders = nullptr, std::vector<Vec3>* lights = nullptr) {
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
    // Коллайдеры: по коробке на каждую стену от глубины под землёй до крыши (толщина 0,4 м, чуть длиннее стены, чтобы углы смыкались).
    if (colliders) {
        float groundLow = 1e9f;
        for (const Vec2& v : footprint) groundLow = std::min(groundLow, groundAt(v.x, v.y));
        const float bottom = gTerrain ? groundLow - 0.6f : -0.6f, top = baseTop + height;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2 a = footprint[i], b = footprint[(i + 1) % n];
            const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
            if (len < 0.3f) continue;
            WallCollider w;
            w.center = {(a.x + b.x) * 0.5f, (bottom + top) * 0.5f, (a.y + b.y) * 0.5f};
            w.half = {len * 0.5f + 0.2f, (top - bottom) * 0.5f, 0.2f};
            w.yaw = std::atan2(-dz, dx);
            colliders->push_back(w);
        }
    }
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
            if (look.style) {
                const auto& s = *look.style;
                const int bays = std::max(1, static_cast<int>(std::floor(len / s.shopBayWidth)));
                for (int bay = 0; bay < bays; ++bay) {
                    const float t0 = static_cast<float>(bay) / static_cast<float>(bays), t1 = static_cast<float>(bay + 1) / static_cast<float>(bays);
                    const int sign = static_cast<int>((look.seed + static_cast<unsigned>(i * 7 + bay)) % static_cast<unsigned>(s.signBays));
                    const float u = static_cast<float>(sign) / static_cast<float>(s.signBays), du = 1.f / static_cast<float>(s.signBays);
                    const Vec2 bayA{a.x + dx * t0, a.y + dz * t0}, bayB{a.x + dx * t1, a.y + dz * t1};
                    addWallQuad(mesh, bayA, bayB, 0.f, kShopHeight, nx, nz,
                                {1, 1, 1}, u + du, u, 0.f, 1.f, true);
                    if ((look.seed + static_cast<unsigned>(bay)) % 3 != 0) {
                        const auto start = static_cast<std::uint32_t>(mesh.vertices.size());
                        const float slope = s.acHeight * 0.22f;
                        const float normalLength = std::hypot(slope, s.canopyDepth);
                        const Color color{1, 1, 1};
                        pushVertex(mesh, bayA.x, s.canopyHeight + slope, bayA.y, nx * slope / normalLength, s.canopyDepth / normalLength, nz * slope / normalLength, color, u + du, 0.22f);
                        pushVertex(mesh, bayB.x, s.canopyHeight + slope, bayB.y, nx * slope / normalLength, s.canopyDepth / normalLength, nz * slope / normalLength, color, u, 0.22f);
                        pushVertex(mesh, bayB.x + nx * s.canopyDepth, s.canopyHeight, bayB.y + nz * s.canopyDepth, nx * slope / normalLength, s.canopyDepth / normalLength, nz * slope / normalLength, color, u, 0.27f);
                        pushVertex(mesh, bayA.x + nx * s.canopyDepth, s.canopyHeight, bayA.y + nz * s.canopyDepth, nx * slope / normalLength, s.canopyDepth / normalLength, nz * slope / normalLength, color, u + du, 0.27f);
                        mesh.indices.insert(mesh.indices.end(), {start, start + 2, start + 1, start, start + 3, start + 2,
                                                              start, start + 1, start + 2, start, start + 2, start + 3});
                        addWallQuad(mesh, {bayA.x + nx * s.canopyDepth, bayA.y + nz * s.canopyDepth}, {bayB.x + nx * s.canopyDepth, bayB.y + nz * s.canopyDepth},
                                    s.canopyHeight - slope, s.canopyHeight, nx, nz, color, u + du, u, 0.22f, 0.27f);
                    }
                }
            } else {
                const float shift = std::fmod(std::fabs(a.x * 0.37f + a.y * 0.61f), 1.f);
                addWallQuad(mesh, a, b, 0.f, kShopHeight, nx, nz, {1, 1, 1}, shift - along / kShopBay, shift - (along + len) / kShopBay, 0.f, 1.f, true);
            }
            mesh.tag(GeoMaterial::Storefront, shopFirst);
        } else if (shop) {
            const auto shopFirst = static_cast<std::uint32_t>(mesh.indices.size());
            addWallQuad(mesh, a, b, 0.f, kShopHeight, nx, nz, wall, u0, u1, (height - kShopHeight) / kWallTile, height / kWallTile, true);
            mesh.tag(look.wall, shopFirst);
        }
        along += len;
    }
    if (look.style) {
        const auto& s = *look.style;
        int blades = 0;
        for (std::size_t edge = 0; edge < n; ++edge) {
            const auto a = footprint[edge], b = footprint[(edge + 1) % n];
            const float dx = b.x - a.x, dz = b.y - a.y, length = std::hypot(dx, dz);
            if (length < s.shopBayWidth) continue;
            const float ux = dx / length, uz = dz / length, nx = uz, nz = -ux;
            // Наружные блоки кондиционеров, кронштейны и решётки: отдельный объём, а не рисунок стены.
            if ((look.seed + edge) % 3 != 0 && height > s.floorHeight * 2.f) {
                const float px = a.x + ux * std::min(length * 0.7f, s.acSpacing), pz = a.y + uz * std::min(length * 0.7f, s.acSpacing);
                const float y = s.floorHeight * (1.f + static_cast<float>((look.seed + edge) % 2)) + s.acHeight;
                if (y + s.acHeight < height) {
                    addOrientedBox(mesh, px + nx * s.acDepth, pz + nz * s.acDepth, s.acWidth * 0.5f, s.acDepth * 0.5f,
                                   y, y + s.acHeight, ux, uz, {0.68f, 0.7f, 0.69f}, GeoMaterial::Metal);
                    for (int bar = 0; bar < 3; ++bar) {
                        const float by = y + s.acHeight * (0.25f + static_cast<float>(bar) * 0.2f);
                        addOrientedBox(mesh, px + nx * s.acDepth * 1.55f, pz + nz * s.acDepth * 1.55f, s.acWidth * 0.38f, s.acDepth * 0.04f,
                                       by, by + s.acHeight * 0.045f, ux, uz, {0.18f, 0.2f, 0.2f}, GeoMaterial::Metal);
                    }
                }
            }
            if (!shop || blades >= s.maxBlades || height < s.bladeBase + s.bladeHeight) continue;
            const float cx = a.x + ux * std::min(length * 0.65f, s.shopBayWidth), cz = a.y + uz * std::min(length * 0.65f, s.shopBayWidth);
            addOrientedBox(mesh, cx + nx * s.bladeOffset * 0.5f, cz + nz * s.bladeOffset * 0.5f, s.bladeWidth * 0.10f, s.bladeOffset * 0.5f,
                           s.bladeBase + s.bladeHeight * 0.8f, s.bladeBase + s.bladeHeight * 0.84f, ux, uz, {0.23f, 0.25f, 0.27f}, GeoMaterial::Metal);
            const float offset = s.bladeOffset, half = s.bladeWidth * 0.5f;
            const Vec2 left{cx + nx * (offset - half), cz + nz * (offset - half)};
            const Vec2 right{cx + nx * (offset + half), cz + nz * (offset + half)};
            const auto first = static_cast<std::uint32_t>(mesh.indices.size());
            const float u = static_cast<float>((look.seed + edge * 3) % static_cast<unsigned>(s.signBays)) / static_cast<float>(s.signBays);
            const float du = 1.f / static_cast<float>(s.signBays);
            addWallQuad(mesh, left, right, s.bladeBase, s.bladeBase + s.bladeHeight, -ux, -uz, {1, 1, 1}, u + du, u, 0, 1);
            addWallQuad(mesh, right, left, s.bladeBase, s.bladeBase + s.bladeHeight, ux, uz, {1, 1, 1}, u + du, u, 0, 1);
            mesh.tag(GeoMaterial::BladeSign, first);
            if (lights) lights->push_back({cx + nx * s.bladeOffset, baseTop + s.bladeBase - s.acHeight, cz + nz * s.bladeOffset});
            // Горизонтальная вывеска второго этажа: целый бренд, без обрезанных букв на углу здания.
            if (height > s.floorHeight * 3.f) {
                const float y = s.floorHeight * 2.f;
                const float width = std::min(length * 0.7f, s.shopBayWidth * 1.3f);
                const Vec2 sa{cx - ux * width * 0.5f + nx * 0.06f, cz - uz * width * 0.5f + nz * 0.06f};
                const Vec2 sb{cx + ux * width * 0.5f + nx * 0.06f, cz + uz * width * 0.5f + nz * 0.06f};
                const auto signFirst = static_cast<std::uint32_t>(mesh.indices.size());
                addWallQuad(mesh, sa, sb, y, y + s.acHeight, nx, nz, {1, 1, 1}, u + du, u, 0.03f, 0.18f);
                mesh.tag(GeoMaterial::Storefront, signFirst);
            }
            ++blades;
        }
        const auto center = centroid(footprint);
        const float radius = s.acWidth * 0.65f;
        if (height < 30.f && insidePolygon(footprint, center.x - radius, center.y - radius) && insidePolygon(footprint, center.x + radius, center.y + radius))
            addPrism(mesh, center.x, center.y, radius, height + 0.15f, height + s.floorHeight * 0.55f, {0.52f, 0.57f, 0.56f}, GeoMaterial::Metal);
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
    mesh.tag(look.wall == GeoMaterial::None ? GeoMaterial::None : look.roof, roofFirst);
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
    constexpr int kSeg = 12, kRings = 7;
    const auto first = static_cast<std::uint32_t>(mesh.indices.size());
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= kRings; ++i) {
        const float v = kPi * static_cast<float>(i) / kRings, sv = std::sin(v), cv = std::cos(v);
        for (int j = 0; j <= kSeg; ++j) {
            const float u = 2.f * kPi * static_cast<float>(j) / kSeg;
            const float nx = sv * std::cos(u), ny = cv, nz = sv * std::sin(u);
            const float irregular = 1.f + 0.12f * std::sin(u * 3.f + v * 5.f) + 0.08f * std::cos(u * 5.f - v * 4.f);
            const float shade = 0.68f + 0.32f * (ny * 0.5f + 0.5f);
            pushVertex(mesh, cx + nx * r * irregular, cy + ny * r * 0.8f * irregular, cz + nz * r * irregular, nx, ny, nz,
                       {c.r * shade, c.g * shade, c.b * shade}, u * 2.f, v * 2.f);
        }
    }
    for (int i = 0; i < kRings; ++i)
        for (int j = 0; j < kSeg; ++j) {
            const std::uint32_t a = base + static_cast<std::uint32_t>(i * (kSeg + 1) + j), b = a + 1, c2 = a + static_cast<std::uint32_t>(kSeg + 1), d = c2 + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c2, b, d, c2});
        }
    mesh.tag(GeoMaterial::Leaves, first);
}

void addTree(CityChunk& chunk, float x, float z, std::uint32_t seed, const CityBuildSettings& settings) {
    if (settings.style.enabled && settings.instanceTrees) {
        const auto& style = settings.style;
        const float height = style.treeHeight + style.treeHeightVariation * static_cast<float>(seed % 7) / 6.f;
        chunk.trees.push_back({{x, groundAt(x, z), z}, height, static_cast<float>(seed % 360) * kPi / 180.f});
        if (settings.colliders) chunk.walls.push_back({{x, groundAt(x, z) + style.treeHeight * 0.2f, z}, {style.treeTrunkRadius, style.treeHeight * 0.2f, style.treeTrunkRadius}, 0.f});
        return;
    }
    auto& mesh = chunk.buildings;
    const float h = 3.2f + static_cast<float>(seed % 5) * 0.35f, r = 1.9f + static_cast<float>((seed >> 3) % 4) * 0.3f;
    addPrism(mesh, x, z, 0.14f, 0.f, h, {0.32f, 0.22f, 0.14f}, GeoMaterial::Trunk);
    const float g = 0.26f + static_cast<float>((seed >> 5) % 5) * 0.03f;
    addCrown(mesh, x, h + r * 0.55f, z, r, {0.16f, g, 0.10f});
}

// Фонарь: столб 7 м и вынесенная к дороге головка.
void addLamp(GeoMesh& mesh, float x, float z, float towardX, float towardZ) {
    addPrism(mesh, x, z, 0.09f, 0.f, 7.f, {0.30f, 0.31f, 0.33f}, GeoMaterial::Metal);
    addBox(mesh, x + towardX * 0.7f, z + towardZ * 0.7f, 0.45f, 0.45f, 6.9f, 7.15f, {0.34f, 0.35f, 0.37f}, GeoMaterial::Metal, 4.f);
    addBox(mesh, x + towardX * 0.7f, z + towardZ * 0.7f, 0.38f, 0.32f, 6.86f, 6.91f, {1.f, 0.96f, 0.86f}, GeoMaterial::LampGlass, 1.f);
}

void addStreetFurniture(CityChunk& chunk, float x, float z, float ux, float uz, const CityStyle& style, bool bench, bool colliders) {
    const float h = style.benchHeight, length = style.benchLength;
    if (bench) {
        for (int slat = 0; slat < 3; ++slat) {
            const float offset = (static_cast<float>(slat) - 1.f) * h * 0.22f;
            addOrientedBox(chunk.buildings, x - uz * offset, z + ux * offset, length * 0.5f, h * 0.09f, h, h * 1.12f,
                           ux, uz, {0.38f, 0.24f, 0.13f}, GeoMaterial::Trunk);
        }
        addOrientedBox(chunk.buildings, x + uz * h * 0.34f, z - ux * h * 0.34f, length * 0.5f, h * 0.06f, h * 1.2f, h * 1.95f,
                       ux, uz, {0.38f, 0.24f, 0.13f}, GeoMaterial::Trunk);
        for (const float side : {-1.f, 1.f})
            addOrientedBox(chunk.buildings, x + ux * length * 0.34f * side, z + uz * length * 0.34f * side, h * 0.07f, h * 0.40f, 0, h,
                           ux, uz, {0.16f, 0.18f, 0.19f}, GeoMaterial::Metal);
        if (colliders) chunk.walls.push_back({{x, groundAt(x, z) + h, z}, {length * 0.5f, h, h * 0.40f}, std::atan2(-uz, ux)});
    } else {
        const float half = style.planterSize * 0.5f;
        addOrientedBox(chunk.buildings, x, z, half, half, 0, style.planterSize * 0.65f, ux, uz, {0.72f, 0.72f, 0.69f}, GeoMaterial::Terrain);
        addCrown(chunk.buildings, x, style.planterSize * 0.85f, z, half * 1.15f, {0.17f, 0.31f, 0.11f});
        if (colliders) chunk.walls.push_back({{x, groundAt(x, z) + style.planterSize * 0.325f, z}, {half, style.planterSize * 0.325f, half}, std::atan2(-uz, ux)});
    }
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

float roadWidth(const OsmWay& way) {
    const float fallback = roadWidth(way.tag("highway"));
    if (fallback <= 0) return 0;
    const float specified = parseMeters(way.tag("width"));
    return std::isfinite(specified) && specified >= 1.f && specified <= 80.f ? specified : fallback;
}

float buildingHeight(const OsmWay& way, const CityBuildSettings& s, float footprintArea) {
    float h = parseMeters(way.tag("height"));
    if (h <= 0.f) {
        const float levels = parseMeters(way.tag("building:levels"));
        if (levels > 0.f) h = levels * s.levelHeight + 1.f;  // + крыша и цоколь
    }
    if (h <= 0.f) h = s.heights.heightFor(way.tag("building"), footprintArea);
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

// --- Мосты и эстакады (этап 03): конструкция под полотном и коллайдеры. Высоты полотна считает computeRoadProfile (road_profile.h).
// Четырёхугольник с нормалью: порядок вершин подбирается так, чтобы лицевая сторона смотрела по (nx, ny, nz).
void addSolidQuad(GeoMesh& m, const float p[4][3], const float uv[4][2], float nx, float ny, float nz, Color c) {
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (int k = 0; k < 4; ++k) pushVertex(m, p[k][0], p[k][1], p[k][2], nx, ny, nz, c, uv[k][0], uv[k][1]);
    const float e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]}, e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
    const float cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
    if (cx * nx + cy * ny + cz * nz >= 0.f) m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    else m.indices.insert(m.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

float deckHeightAt(float x, float z, Vec2 a, Vec2 b, float hA, float hB) {
    const float dx = b.x - a.x, dz = b.y - a.y, len2 = std::max(dx * dx + dz * dz, 1e-6f);
    const float t = std::clamp(((x - a.x) * dx + (z - a.y) * dz) / len2, 0.f, 1.f);
    return hA + (hB - hA) * t;
}

// Балка (бока и низ) и опоры под одним сегментом полотна, коллайдеры полотна и опор. Вершины задаются в абсолютной высоте.
void addBridgeStructure(CityChunk& chunk, Vec2 a, Vec2 b, float hA, float hB, float width, const CityBuildSettings& settings, CityStats& st) {
    const RoadProfileSettings& rp = settings.roadProfile;
    const float dx = b.x - a.x, dz = b.y - a.y, length = std::hypot(dx, dz);
    if (length < 0.5f) return;
    const float ux = dx / length, uz = dz / length, nx = -uz, nz = ux, hw = width * 0.5f;
    const Color concrete{0.62f, 0.62f, 0.60f};
    const float tile = kWallTile;
    const bool savedFixed = gFixed;
    const float savedBase = gFixedBase;
    gFixed = true;
    gFixedBase = 0.f;
    const auto firstIndex = static_cast<std::uint32_t>(chunk.ground.indices.size());
    const float t0 = 0.f, t1 = length / tile, v1 = rp.deckThickness / tile;
    const float th = rp.deckThickness;
    {
        const float side[4][3] = {{a.x + nx * hw, hA, a.y + nz * hw}, {b.x + nx * hw, hB, b.y + nz * hw}, {b.x + nx * hw, hB - th, b.y + nz * hw}, {a.x + nx * hw, hA - th, a.y + nz * hw}};
        const float uv[4][2] = {{t0, 0}, {t1, 0}, {t1, v1}, {t0, v1}};
        addSolidQuad(chunk.ground, side, uv, nx, 0.f, nz, concrete);
    }
    {
        const float side[4][3] = {{a.x - nx * hw, hA, a.y - nz * hw}, {b.x - nx * hw, hB, b.y - nz * hw}, {b.x - nx * hw, hB - th, b.y - nz * hw}, {a.x - nx * hw, hA - th, a.y - nz * hw}};
        const float uv[4][2] = {{t0, 0}, {t1, 0}, {t1, v1}, {t0, v1}};
        addSolidQuad(chunk.ground, side, uv, -nx, 0.f, -nz, concrete);
    }
    {
        const float bottom[4][3] = {{a.x + nx * hw, hA - th, a.y + nz * hw}, {b.x + nx * hw, hB - th, b.y + nz * hw}, {b.x - nx * hw, hB - th, b.y - nz * hw}, {a.x - nx * hw, hA - th, a.y - nz * hw}};
        const float uv[4][2] = {{t0, 0}, {t1, 0}, {t1, width / tile}, {t0, width / tile}};
        addSolidQuad(chunk.ground, bottom, uv, 0.f, -1.f, 0.f, concrete);
    }
    // Опоры: квадратные колонны от земли до низа балки, если под полотном есть габарит.
    const int pierCount = std::max(length > rp.pierSpacing * 0.6f ? 1 : 0, static_cast<int>(length / rp.pierSpacing));
    const float half = rp.pierSize * 0.5f;
    for (int k = 0; k < pierCount; ++k) {
        const float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(pierCount);
        const float px = a.x + dx * t, pz = a.y + dz * t;
        const float deckBottom = hA + (hB - hA) * t - th;
        const float ground = groundAt(px, pz);
        if (deckBottom - ground < rp.minPierClearance) continue;
        const float corners[4][2] = {{px + ux * half + nx * half, pz + uz * half + nz * half}, {px + ux * half - nx * half, pz + uz * half - nz * half},
                                     {px - ux * half - nx * half, pz - uz * half - nz * half}, {px - ux * half + nx * half, pz - uz * half + nz * half}};
        const float v = (deckBottom - ground) / tile;
        for (int side = 0; side < 4; ++side) {
            const int next = (side + 1) % 4;
            const float quad[4][3] = {{corners[side][0], deckBottom, corners[side][1]}, {corners[next][0], deckBottom, corners[next][1]},
                                      {corners[next][0], ground, corners[next][1]}, {corners[side][0], ground, corners[side][1]}};
            const float uv[4][2] = {{0, 0}, {rp.pierSize / tile, 0}, {rp.pierSize / tile, v}, {0, v}};
            const float mx = (corners[side][0] + corners[next][0]) * 0.5f - px, mz = (corners[side][1] + corners[next][1]) * 0.5f - pz, ml = std::max(std::hypot(mx, mz), 1e-4f);
            addSolidQuad(chunk.ground, quad, uv, mx / ml, 0.f, mz / ml, concrete);
        }
        if (settings.colliders) {
            chunk.walls.push_back({{px, (deckBottom + ground) * 0.5f, pz}, {half, (deckBottom - ground) * 0.5f, half}, std::atan2(-uz, ux)});
            ++st.piers;
        }
    }
    chunk.ground.tag(GeoMaterial::WallConcrete, firstIndex);
    gFixed = savedFixed;
    gFixedBase = savedBase;
    // Коллайдеры полотна: коробки без наклона, уклон — ступеньками по colliderStep (верх коробки на высоте полотна в её центре).
    if (settings.colliders) {
        const int steps = std::max(1, static_cast<int>(std::ceil(length / rp.colliderStep)));
        const float stepLength = length / static_cast<float>(steps);
        for (int k = 0; k < steps; ++k) {
            const float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(steps);
            const float cx = a.x + dx * t, cz = a.y + dz * t, top = hA + (hB - hA) * t + settings.roadLift;
            chunk.walls.push_back({{cx, top - rp.colliderThickness * 0.5f, cz}, {stepLength * 0.55f, rp.colliderThickness * 0.5f, hw}, std::atan2(-uz, ux)});
            ++st.deckColliders;
        }
    }
    ++st.bridgeSegments;
}

std::map<CellCoord, CityChunk> buildCity(const OsmData& data, const GeoProjection& projection, const CityBuildSettings& settings, CityStats* stats) {
    std::map<CellCoord, CityChunk> chunks;
    CityStats st;
    gTerrain = settings.terrain && settings.terrain->valid() ? settings.terrain : nullptr;
    gFixed = gHaveBase = false;
    RoadProfile ownProfile;
    if (settings.bridges && !settings.roadProfileData) ownProfile = computeRoadProfile(data, projection, gTerrain, settings.roadProfile);
    const RoadProfile& profile = !settings.bridges ? ownProfile : (settings.roadProfileData ? *settings.roadProfileData : ownProfile);
    st.bridgeWays = profile.stats().bridgeWays;
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
    std::unordered_map<std::int64_t, int> roadDegree;
    if (settings.style.enabled) {
        for (const auto& way : data.ways) {
            if (way.tag("han:visual") == "external" || roadWidth(way) < 6.f || way.tag("area") == "yes") continue;
            for (std::size_t i = 0; i + 1 < way.nodes.size(); ++i) {
                ++roadDegree[way.nodes[i]];
                ++roadDegree[way.nodes[i + 1]];
            }
        }
    }

    std::vector<std::vector<Vec2>> footprints;  // построенные здания: деревья и фонари не ставятся внутрь
    for (const OsmWay& way : data.ways) {
        if (way.tag("han:visual") == "external") continue;
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
            const float height = buildingHeight(way, settings, static_cast<float>(std::fabs(signedArea(pts))));
            BuildingLook look;
            look.wall = wallMaterial(way.id, height);
            const std::uint32_t seed = static_cast<std::uint32_t>((way.id < 0 ? -way.id : way.id) * 2246822519u) >> 7;
            if (settings.style.enabled) {
                if (look.wall == GeoMaterial::WallBrick) look.wall = seed % 2 ? GeoMaterial::WallRedBrick : GeoMaterial::WallWornBrick;
                if (look.wall == GeoMaterial::WallConcrete && seed % 3 == 0) look.wall = GeoMaterial::WallCladding;
            }
            look.shop = settings.buildingDetail && height <= 45.f && seed % 4 != 0;  // у трёх домов из четырёх на первом этаже лавки
            look.parapet = settings.buildingDetail && seed % 7 != 0;
            look.rooftop = settings.buildingDetail;
            look.shopTile = settings.shopTileMeters;
            look.style = settings.style.enabled ? &settings.style : nullptr;
            look.seed = seed;
            Color roof{1, 1, 1};
            if (settings.style.enabled && height < settings.style.floorHeight * 6.f && seed % 3 != 0) {
                look.roof = GeoMaterial::RoofPaint;
                roof = {settings.style.roofR, settings.style.roofG, settings.style.roofB};
            }
            if (addBuilding(chunk.buildings, pts, height, wallTint(way.id), roof, look, settings.colliders ? &chunk.walls : nullptr, settings.style.enabled ? &chunk.lamps : nullptr)) {
                if (settings.buildingDetail) addApron(chunk.ground, pts, 1.6f, 0.09f);
                footprints.push_back(pts);
                ++chunk.buildingCount;
                ++st.buildings;
                for (const Vec2& p : pts) grow(p.x, p.y);
            } else {
                ++st.skipped;
            }
        } else if (!highway.empty()) {
            const float width = roadWidth(way);
            if (width <= 0.f) continue;
            auto pts = project(way);
            if (pts.size() < 2) continue;
            const bool walking = highway == "pedestrian" || highway == "footway" || highway == "path" || highway == "steps" || highway == "cycleway";
            if (walking && way.closed() && way.tag("area") == "yes") {
                auto& chunk = chunkAt(centroid(pts));
                const auto first = static_cast<std::uint32_t>(chunk.ground.indices.size());
                if (addFlatPolygon(chunk.ground, pts, settings.roadLift + 0.10f, {0.94f, 0.92f, 0.88f}, 4.f)) {
                    chunk.ground.tag(settings.style.enabled ? GeoMaterial::Plaza : GeoMaterial::Sidewalk, first);
                    if (settings.style.enabled && settings.streetProps) {
                        const auto center = centroid(pts);
                        for (std::size_t edge = 0; edge + 1 < pts.size(); ++edge) {
                            const float length = std::hypot(pts[edge + 1].x - pts[edge].x, pts[edge + 1].y - pts[edge].y);
                            if (length < settings.style.benchLength * 2.f) continue;
                            const float x = ((pts[edge].x + pts[edge + 1].x) * 0.5f) * 0.85f + center.x * 0.15f;
                            const float z = ((pts[edge].y + pts[edge + 1].y) * 0.5f) * 0.85f + center.y * 0.15f;
                            addStreetFurniture(chunk, x, z, (pts[edge + 1].x - pts[edge].x) / length, (pts[edge + 1].y - pts[edge].y) / length,
                                               settings.style, edge % 2 == 0, settings.colliders);
                        }
                    }
                    ++chunk.areaCount;
                    for (const auto& p : pts) grow(p.x, p.y);
                }
                continue;
            }
            // Дорога делится по ячейкам: каждый сегмент идёт в ячейку своей середины, стыки добавляются вместе с сегментом.
            const float lift = settings.roadLift + (14.f - width) * 0.002f;  // шире — ниже, чтобы перекрытия не мерцали
            const Color color = roadColor(highway);
            const bool bridgeWay = !profile.empty() && isBridgeWay(way) && pts.size() == way.nodes.size() && profile.hasNode(way.nodes.front());
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                // Полотно с балкой — если хотя бы один конец рассчитан профилем; между двумя узлами на земле — только пологий участок без узлов на рельефе,
                // иначе (крутой склон) дорога идёт по земле, как обычная.
                const bool nodesKnown = bridgeWay && profile.hasNode(way.nodes[i]) && profile.hasNode(way.nodes[i + 1]);
                const bool profiled = nodesKnown && (profile.isProfileNode(way.nodes[i]) || profile.isProfileNode(way.nodes[i + 1]));
                const bool gentle = nodesKnown && !profile.followsTerrain(way.nodes[i]) && !profile.followsTerrain(way.nodes[i + 1]) &&
                                    std::fabs(profile.height(way.nodes[i]) - profile.height(way.nodes[i + 1])) <= 0.12f * std::max(1.f, std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y));
                const bool deck = profiled || gentle;
                const float deckA = deck ? profile.height(way.nodes[i]) : 0.f, deckB = deck ? profile.height(way.nodes[i + 1]) : 0.f;
                const std::vector<Vec2> seg = {pts[i], pts[i + 1]};
                const Vec2 mid{(pts[i].x + pts[i + 1].x) * 0.5f, (pts[i].y + pts[i + 1].y) * 0.5f};
                CityChunk& chunk = chunkAt(mid);
                const auto vertsBefore = chunk.ground.vertices.size();
                const auto indexBefore = static_cast<std::uint32_t>(chunk.ground.indices.size());
                if (deck) {  // полотно моста: высота задаётся профилем, а не рельефом; локальные подъёмы вершин остаются относительно полотна
                    gFixed = true;
                    gFixedBase = 0.f;
                }
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
                planarUv(chunk.ground, vertsBefore, walking ? 4.f : kRoadTile);
                chunk.ground.tag(walking ? GeoMaterial::Sidewalk : GeoMaterial::Road, indexBefore);
                if (settings.style.enabled && width >= 8.f && highway != "motorway" && highway != "trunk")
                    addStreetEdges(chunk.ground, pts[i], pts[i + 1], width, lift, settings.style);
                const auto markFirst = static_cast<std::uint32_t>(chunk.ground.indices.size());
                addRoadMarkings(chunk.ground, pts[i], pts[i + 1], width, highway, lift, settings.style.enabled);
                if (settings.style.enabled && width >= 8.f && highway != "motorway" && highway != "trunk" && pts.size() == way.nodes.size()) {
                    const float dx = pts[i + 1].x - pts[i].x, dz = pts[i + 1].y - pts[i].y, length = std::hypot(dx, dz);
                    const float inset = settings.style.crosswalkLength * 0.75f + 1.f;
                    if (length > inset * 3.f) {
                        if (roadDegree[way.nodes[i]] >= 3) addCrosswalk(chunk.ground, {pts[i].x + dx / length * inset, pts[i].y + dz / length * inset}, dx / length, dz / length, width, lift + 0.08f, settings.style);
                        if (roadDegree[way.nodes[i + 1]] >= 3) addCrosswalk(chunk.ground, {pts[i + 1].x - dx / length * inset, pts[i + 1].y - dz / length * inset}, dx / length, dz / length, width, lift + 0.08f, settings.style);
                    }
                }
                chunk.ground.tag(GeoMaterial::Marking, markFirst);
                if (deck) {
                    gFixed = false;
                    for (std::size_t vi = vertsBefore; vi < chunk.ground.vertices.size(); ++vi) {
                        auto& v = chunk.ground.vertices[vi];
                        v.position[1] += deckHeightAt(v.position[0], v.position[2], pts[i], pts[i + 1], deckA, deckB);
                    }
                    addBridgeStructure(chunk, pts[i], pts[i + 1], deckA, deckB, width, settings, st);
                }
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
            const Color color = isWater ? Color{0.16f, 0.30f, 0.42f} : Color{1.f, 1.f, 1.f};
            // Вода лежит ровной плоскостью на отметке берегов (самые низкие точки контура), чуть ниже земли; парки идут по рельефу.
            const bool savedFixed = gFixed;
            if (isWater && gTerrain) {
                float level = 1e9f;
                for (const Vec2& p : pts) level = std::min(level, groundAt(p.x, p.y));
                gFixed = true;
                gFixedBase = level - 0.4f;
            }
            // Длинная река/парк должны присутствовать у каждого берега, а не только в ячейке своего центра.
            bool built = false;
            const auto triangles = triangulate(pts);
            for (std::size_t index = 0; index < triangles.size(); index += 3) {
                const std::vector<Vec2> triangle = {pts[triangles[index]], pts[triangles[index + 1]], pts[triangles[index + 2]]};
                const CellCoord lo = grid.cellOf(std::min({triangle[0].x, triangle[1].x, triangle[2].x}), std::min({triangle[0].y, triangle[1].y, triangle[2].y}));
                const CellCoord hi = grid.cellOf(std::max({triangle[0].x, triangle[1].x, triangle[2].x}), std::max({triangle[0].y, triangle[1].y, triangle[2].y}));
                for (int x = lo.x; x <= hi.x; ++x) for (int z = lo.z; z <= hi.z; ++z) {
                    const float left = static_cast<float>(x) * settings.cellSize, top = static_cast<float>(z) * settings.cellSize;
                    const auto part = clipConvexPolygon(triangle, left, top, left + settings.cellSize, top + settings.cellSize);
                    if (part.size() < 3 || std::fabs(signedArea(part)) < 1e-5f) continue;
                    auto& chunk = chunks[{x, z}];
                    const auto first = static_cast<std::uint32_t>(chunk.ground.indices.size());
                    if (addFlatPolygon(chunk.ground, part, isWater ? 0.02f : 0.03f, color, kParkTile)) {
                        chunk.ground.tag(isWater ? GeoMaterial::Water : GeoMaterial::Park, first);
                        ++chunk.areaCount;
                        built = true;
                    }
                }
            }
            gFixed = savedFixed;
            gFixedBase = 0.f;
            if (built) {
                ++(isWater ? st.waters : st.parks);
                for (const Vec2& p : pts) grow(p.x, p.y);
            } else {
                ++st.skipped;
            }
        }
    }
    // Уличные объекты: деревья и фонари вдоль дорог (после зданий, чтобы не попадать внутрь).
    struct FootprintBounds { float minX, maxX, minZ, maxZ; };
    std::vector<FootprintBounds> footprintBounds;
    std::map<CellCoord, std::vector<std::size_t>> footprintCells;
    footprintBounds.reserve(footprints.size());
    for (std::size_t index = 0; index < footprints.size(); ++index) {
        FootprintBounds bounds{1e9f, -1e9f, 1e9f, -1e9f};
        for (const auto& p : footprints[index]) {
            bounds.minX = std::min(bounds.minX, p.x); bounds.maxX = std::max(bounds.maxX, p.x);
            bounds.minZ = std::min(bounds.minZ, p.y); bounds.maxZ = std::max(bounds.maxZ, p.y);
        }
        footprintBounds.push_back(bounds);
        const auto first = grid.cellOf(bounds.minX - 1.f, bounds.minZ - 1.f), last = grid.cellOf(bounds.maxX + 1.f, bounds.maxZ + 1.f);
        for (int z = first.z; z <= last.z; ++z) for (int x = first.x; x <= last.x; ++x) footprintCells[{x, z}].push_back(index);
    }
    const auto insideAnyBuilding = [&](float x, float z) {
        const auto found = footprintCells.find(grid.cellOf(x, z));
        if (found == footprintCells.end()) return false;
        for (const auto index : found->second) {
            const auto bounds = footprintBounds[index];
            if (x < bounds.minX - 1.f || x > bounds.maxX + 1.f || z < bounds.minZ - 1.f || z > bounds.maxZ + 1.f) continue;
            if (insidePolygon(footprints[index], x, z)) return true;
        }
        return false;
    };
    if (settings.streetProps) {
        for (const OsmWay& way : data.ways) {
            if (way.tag("han:visual") == "external") continue;
            const std::string_view highway = way.tag("highway");
            const float width = highway.empty() ? 0.f : roadWidth(way);
            if (width < 6.f || highway == "motorway" || highway == "trunk" || highway == "service") continue;
            if (!profile.empty() && isBridgeWay(way)) continue;  // деревья и фонари у земли не ставятся вдоль полотна на высоте
            const auto pts = project(way);
            float sinceTree = 8.f, sinceLamp = 15.f, sinceFurniture = 0.f;
            int side = 1;
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const Vec2 a = pts[i], b = pts[i + 1];
                const float dx = b.x - a.x, dz = b.y - a.y, len = std::sqrt(dx * dx + dz * dz);
                if (len < 1.f) continue;
                const float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
                for (float t = 0.f; t < len; t += 2.f) {
                    sinceTree += 2.f;
                    sinceLamp += 2.f;
                    sinceFurniture += 2.f;
                    const float px = a.x + ux * t, pz = a.y + uz * t;
                    if (sinceTree >= 20.f) {
                        const float off = width * 0.5f + 2.6f;
                        const float tx = px + nx * off * static_cast<float>(side), tz = pz + nz * off * static_cast<float>(side);
                        if (!insideAnyBuilding(tx, tz)) {
                            addTree(chunkAt({tx, tz}), tx, tz, static_cast<std::uint32_t>(std::fabs(tx * 31.f + tz * 17.f)), settings);
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
                    if (settings.style.enabled && sinceFurniture >= settings.style.furnitureSpacing) {
                        const float off = width * 0.5f + settings.style.sidewalkWidth * 0.6f;
                        const float fx = px + nx * off, fz = pz + nz * off;
                        if (!insideAnyBuilding(fx, fz) && !insideAnyBuilding(fx + ux * settings.style.benchLength, fz + uz * settings.style.benchLength))
                            addStreetFurniture(chunkAt({fx, fz}), fx, fz, ux, uz, settings.style, static_cast<unsigned>(way.id) % 2 == 0, settings.colliders);
                        sinceFurniture = 0.f;
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
                    addTree(chunkAt({jx, jz}), jx, jz, h, settings);
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
