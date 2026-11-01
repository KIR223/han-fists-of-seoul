// han/world/geometry.h — плоская геометрия для построения города (E10): площадь, триангуляция многоугольника, сетка вершин.
#pragma once
#include <cstdint>
#include <vector>

namespace han::world {
struct Vec2 {
    float x = 0, y = 0;
};

// Знаковая площадь: > 0, если вершины идут против часовой стрелки (в осях x вправо, y вверх).
float signedArea(const std::vector<Vec2>& polygon);

// Триангуляция простого многоугольника (без дыр) методом отсечения «ушей». Любое направление обхода; повторяющаяся замыкающая
// вершина допустима. Возвращает тройки индексов в исходный массив (после удаления замыкающей вершины) против часовой стрелки.
// Пустой результат — многоугольник вырожден или самопересекается настолько, что отсечь уши не удалось.
std::vector<std::uint32_t> triangulate(const std::vector<Vec2>& polygon);

struct GeoVertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2] = {0, 0};  // метры карты / размер плитки текстуры: плитка повторяется без швов между объектами
};

// Материал части сетки: рендер выбирает по нему текстуру (han_world от графики не зависит).
enum class GeoMaterial : unsigned char { None, Road, Park, Water, WallPlaster, WallBrick, WallConcrete, Roof, Marking, Storefront, Sidewalk, Trunk, Leaves, Metal, Terrain };
constexpr int kGeoMaterialCount = 15;

// Подряд идущие индексы одного материала.
struct GeoRange {
    std::uint32_t firstIndex = 0, indexCount = 0;
    GeoMaterial material = GeoMaterial::None;
};

// Сетка города на стороне CPU; рендер превращает её в MeshData (han_world от графики не зависит).
struct GeoMesh {
    std::vector<GeoVertex> vertices;
    std::vector<std::uint32_t> indices;  // треугольники против часовой стрелки, если смотреть снаружи
    std::vector<GeoRange> ranges;        // пусто — материал не задан; иначе покрывают индексы подряд
    bool empty() const { return indices.empty(); }
    void append(const GeoMesh& other);
    // Помечает индексы с firstIndex до конца материалом (соседние части одного материала сливаются).
    void tag(GeoMaterial material, std::uint32_t firstIndex);
    // Переставляет треугольники так, чтобы каждый материал занимал одну часть (на ячейку выходит одна часть на материал, а не две на дом).
    void groupByMaterial();
};
}  // namespace han::world
// Пример: auto tri = triangulate(footprint); // tri.size() == 3 * (footprint.size() - 2) для простого многоугольника
