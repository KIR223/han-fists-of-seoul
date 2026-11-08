// han/world/landmarks.h — высотные ориентиры, которых нет в выгрузке OSM (этап 08e плана).
// В выгрузке города нет отношений и building:part, поэтому самая высокая башня района Чамсиль и другие ориентиры отсутствуют или сильно занижены.
// Ориентир — простая многоугольная призма заданной высоты в заданной точке (упрощение: без сужения и фасадных деталей), добавляется в OsmData
// как обычное здание, и дальше его видят запекание, дальний силуэт, коллайдеры и навигация. Данные — data/city/landmarks.json.
// Это геометрия реальных сооружений без названий (названия в игру не попадают: бренды и реальные имена запрещены).
#pragma once
#include <string>
#include <vector>

#include "han/world/osm.h"

namespace han::world {

struct Landmark {
    std::string id;              // служебный идентификатор без реального названия
    double lat = 0, lon = 0;
    float radiusMeters = 20.f;   // радиус описанной окружности призмы
    int sides = 8;
    float heightMeters = 100.f;
    float skipIfTallerWithin = 150.f;  // если рядом уже есть здание не ниже 0,8 высоты ориентира — ориентир не добавляется
    std::string source;          // откуда высота (для проверки данных)
};

bool loadLandmarks(const std::string& file, std::vector<Landmark>& out, std::string& error);

struct LandmarkStats {
    int added = 0, skippedPresent = 0;
};

// Добавляет в data узлы и линии ориентиров. Идентификаторы узлов и линий — отрицательные (из диапазона, которого нет в OSM).
LandmarkStats applyLandmarks(OsmData& data, const std::vector<Landmark>& landmarks);

}  // namespace han::world
