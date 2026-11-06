// han/world/walk_layout.h — где могут ходить пешеходы (этап 04 плана города).
// Раньше навигационная сетка знала только контуры зданий: прохожие шли по проезжей части и по воде. Теперь непроходимы здания, вода и проезжая часть
// улиц шириной от 8 м (у таких улиц строитель рисует тротуары; в переулках уже 8 м ходят по мостовой), а переходы — проёмы в проезжей части
// на месте зебр, которые рисует city_builder (концы сегментов у узлов с тремя и более дорогами). Мосты и тоннели не блокируют землю под собой.
// Сетка плоская: пешеходные мосты и их навигация не моделируются. Теги footway=crossing и crossing=* в выборке OSM не сохраняются, отдельные
// пешеходные переходы не учитываются.
#pragma once
#include <string>
#include <vector>

#include "han/world/city_style.h"
#include "han/world/geometry.h"
#include "han/world/osm.h"

namespace han::world {

struct WalkLayoutSettings {
    float minCarriagewayWidth = 8.f;  // уже — мостовая проходима (переулки без тротуаров); от этой ширины проезжая часть закрыта
    float carriagewayMargin = 0.2f;   // отступ контура внутрь от кромки: клетки у самого бордюра остаются проходимыми
    float crossingMargin = 0.6f;      // выступ проёма перехода за кромку проезжей части и за зебру вдоль дороги
    float minBuildingArea = 6.f;
    int junctionSides = 8;            // сторон диска в узлах ломаной (закрывает щели на поворотах)
};

struct WalkLayout {
    std::vector<std::vector<Vec2>> buildings, water, carriageways;  // непроходимые области (метры карты: x, y = z карты)
    std::vector<std::vector<Vec2>> crossings;                       // проёмы: проходимы даже внутри carriageways

    std::vector<std::vector<Vec2>> obstacles() const;  // всё непроходимое вместе: здания, вода, проезжая часть
    // Добавляет воду из assets_bin/city/seoul_water_areas.json (мультиполигоны с островами, готовые треугольники в градусах). false — файла нет или он испорчен.
    bool addWaterAreas(const std::string& file, const GeoProjection& projection, std::string& error);
};

WalkLayout buildWalkLayout(const OsmData& data, const GeoProjection& projection, const CityStyle& style, const WalkLayoutSettings& settings = {});

}  // namespace han::world
