// han/world/city_builder.h — построение города из данных OSM (E10): дороги лентами, здания призмами с крышей, вода и парки
// плоскими пятнами. Результат делится по ячейкам карты (для стриминга E9). Только геометрия: названия и бренды не читаются.
#pragma once
#include <map>
#include <vector>

#include "han/world/geometry.h"
#include "han/world/osm.h"
#include "han/world/streaming.h"

namespace han::world {
struct CityBuildSettings {
    float cellSize = 256.f;
    float defaultBuildingHeight = 9.f;   // если нет ни height, ни building:levels
    float levelHeight = 3.2f;
    float maxBuildingHeight = 300.f;
    float minBuildingArea = 6.f;         // меньше (м²) — не здание (сарай, ошибка данных)
    float roadLift = 0.05f;              // дорога чуть выше земли
    bool buildingDetail = true;          // витрины, парапеты, надстройки на крышах и тротуар вокруг домов
    bool streetProps = true;             // деревья и фонари вдоль улиц
    float junctionSides = 10;            // сторон «диска» в стыках сегментов дороги (закрывает щели на поворотах)
};

// Содержимое одной ячейки города.
struct CityChunk {
    GeoMesh ground;     // дороги, парки, вода
    GeoMesh buildings;  // стены и крыши
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
