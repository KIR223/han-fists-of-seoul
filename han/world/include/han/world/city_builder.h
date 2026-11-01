// han/world/city_builder.h — построение города из данных OSM (E10): дороги лентами, здания призмами с крышей, вода и парки
// плоскими пятнами. Результат делится по ячейкам карты (для стриминга E9). Только геометрия: названия и бренды не читаются.
#pragma once
#include <map>
#include <vector>

#include "han/world/geometry.h"
#include "han/world/osm.h"
#include "han/world/streaming.h"
#include "han/world/terrain.h"

namespace han::world {
struct CityBuildSettings {
    float cellSize = 256.f;
    float defaultBuildingHeight = 9.f;   // если нет ни height, ни building:levels
    float levelHeight = 3.2f;
    float maxBuildingHeight = 300.f;
    float minBuildingArea = 6.f;         // меньше (м²) — не здание (сарай, ошибка данных)
    float roadLift = 0.05f;              // дорога чуть выше земли
    const Heightmap* terrain = nullptr;   // рельеф: дороги, дома, парки и уличные объекты идут по высоте; nullptr — плоская земля (y = 0)
    bool colliders = true;                // строить коллайдеры стен зданий (CityChunk::walls)
    bool terrainMesh = true;              // добавлять сетку земли в ячейки (при заданном рельефе)
    float shopTileMeters = 19.2f;        // длина стены на одну плитку текстуры витрин (атлас брендов из 16 секций — 76,8)
    bool buildingDetail = true;          // витрины, парапеты, надстройки на крышах и тротуар вокруг домов
    bool streetProps = true;             // деревья и фонари вдоль улиц
    float junctionSides = 10;            // сторон «диска» в стыках сегментов дороги (закрывает щели на поворотах)
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Содержимое одной ячейки города.
// Тонкий коллайдер стены здания: коробка с центром, половинами размеров (x — вдоль стены) и поворотом вокруг Y (радианы, ось x коробки вдоль стены).
struct WallCollider {
    Vec3 center;
    Vec3 half;
    float yaw = 0.f;
};

struct CityChunk {
    GeoMesh ground;     // дороги, парки, вода
    GeoMesh buildings;  // стены и крыши
    std::vector<Vec3> lamps;  // головки фонарей (точки света ночью)
    std::vector<WallCollider> walls;  // коллайдеры стен зданий (физика: игрок, машины, толчки)
    int roadSegments = 0, buildingCount = 0, areaCount = 0;
    bool empty() const { return ground.empty() && buildings.empty(); }
};

struct CityStats {
    int roads = 0, buildings = 0, parks = 0, waters = 0, skipped = 0;  // skipped: слишком мелкие, не замкнутые, самопересекающиеся
    float minX = 0, maxX = 0, minZ = 0, maxZ = 0;                      // границы построенного
};

// Ширина дороги (м) по классу highway; 0 — не рисовать (например, служебные линии).
float roadWidth(std::string_view highway);
// Высота здания по тегам (height, building:levels) или значению по умолчанию.
float buildingHeight(const OsmWay& way, const CityBuildSettings& settings);

// Контуры зданий в метрах карты (x, y = z карты) без повторяющейся замыкающей вершины: непроходимые области для навигации.
std::vector<std::vector<Vec2>> buildingFootprints(const OsmData& data, const GeoProjection& projection, float minArea = 6.f);

// Строит все ячейки. Объект попадает в ячейку своего центра (дорога — по сегментам, поэтому длинная дорога делится между ячейками).
std::map<CellCoord, CityChunk> buildCity(const OsmData& data, const GeoProjection& projection, const CityBuildSettings& settings, CityStats* stats = nullptr);
}  // namespace han::world
// Пример: auto chunks = buildCity(osm, GeoProjection(37.5572, 126.9230), {}); for (auto& [cell, chunk] : chunks) upload(chunk.buildings);
