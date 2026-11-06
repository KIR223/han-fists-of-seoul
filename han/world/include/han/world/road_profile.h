// han/world/road_profile.h — профиль высоты дорожного полотна мостов и эстакад (этап 03 плана города).
// Рельеф DEM под рекой или развязкой не является высотой полотна: дорога с тегом bridge поднимается над землёй на габарит по слою (layer),
// а на концах плавно съезжает на землю с ограниченным продольным уклоном. Высота хранится по узлам OSM, поэтому соседние пути,
// делящие узел (съезды, продолжение моста), получают одну и ту же отметку. Разноуровневые пересечения без общего узла не связываются.
// Модель — упрощённая (линейный уклон между узлами, габарит по слою — допущение, не данные OSM); туннели пока не обрабатываются.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "han/world/osm.h"
#include "han/world/terrain.h"

namespace han::world {

struct RoadProfileSettings {
    float maxGrade = 0.07f;                // наибольший продольный уклон съезда и пролёта (доля)
    float clearanceLayer1 = 6.f;           // м над землёй под полотном: layer 1 (по умолчанию для bridge)
    float clearanceLayer2 = 10.f;          // layer 2
    float clearanceLayer3 = 14.f;          // layer 3 и выше
    float maxHeightAboveGround = 30.f;     // выше этого над рельефом полотно не поднимается: узел переходит на высоту рельефа (крутой склон)
    float deckThickness = 1.2f;            // толщина пролётного строения
    float pierSpacing = 40.f;              // шаг опор вдоль пролёта
    float pierSize = 1.6f;                 // сторона квадратной опоры
    float minPierClearance = 2.5f;         // ниже этого габарита опор нет
    float colliderStep = 2.5f;             // длина одной ступеньки коллайдера полотна (коробки без наклона: уклон ступеньками)
    float colliderThickness = 0.6f;        // толщина коробки коллайдера полотна
};

// Читает настройки из JSON (data/city/road_profile.json). Ошибки не меняют out.
bool loadRoadProfileSettings(const std::string& file, RoadProfileSettings& out, std::string& error);

// Дорога на мосту: тег bridge не пуст и не "no", тег highway задан.
bool isBridgeWay(const OsmWay& way);
// Значение тега layer; для моста без тега — 1.
int wayLayer(const OsmWay& way);

struct RoadProfileStats {
    int bridgeWays = 0, bridgeNodes = 0, anchorNodes = 0;
    int buriedNodes = 0;               // полотно ниже рельефа более чем на 0,5 м (остаточные случаи после перехода узлов на рельеф)
    int terrainFollowingNodes = 0;     // узлы моста, перешедшие на высоту рельефа: якоря несовместимы по уклону или габарит выше предела
    int tunnelWays = 0;                // туннели пока рисуются по земле (не обработаны)
    float maxClearance = 0.f;          // наибольшая высота полотна над рельефом в узлах
    int steepSegments = 0;             // участки длиннее 10 м круче 12% (в основном узлы на рельефе: полотно между ними идёт прямой)
    int solverPasses = 0;              // проходов решателя до устойчивого набора узлов на рельефе
    bool converged = true;             // false — проходы кончились раньше устойчивого набора (границы могут нарушаться)
    std::int64_t worstGradeNode = 0;   // узел OSM у участка с наибольшим уклоном (для разбора данных)
    float maxGradeUsed = 0.f;          // наибольший уклон на участках, где есть расчётный (свободный) узел; участки между узлами на земле — рельеф, а не профиль
};

class RoadProfile {
public:
    bool empty() const { return deck_.empty(); }
    bool hasNode(std::int64_t osmNode) const { return deck_.count(osmNode) != 0; }
    // Узел перешёл на высоту рельефа (профиль к нему неприменим); участок между двумя такими узлами рисуется как обычная дорога по земле.
    bool followsTerrain(std::int64_t osmNode) const { return terrain_.count(osmNode) != 0; }
    // Высоту узла задал профиль (не якорь на земле и не узел на рельефе).
    bool isProfileNode(std::int64_t osmNode) const { return free_.count(osmNode) != 0; }
    // Абсолютная высота полотна в узле OSM; NaN, если узел не на мосту.
    float height(std::int64_t osmNode) const;
    const std::unordered_map<std::int64_t, float>& nodes() const { return deck_; }
    const RoadProfileStats& stats() const { return stats_; }

private:
    std::unordered_map<std::int64_t, float> deck_;
    std::unordered_set<std::int64_t> terrain_, free_;
    RoadProfileStats stats_;
    friend RoadProfile computeRoadProfile(const OsmData&, const GeoProjection&, const Heightmap*, const RoadProfileSettings&);
};

// Считает высоты полотна мостов. terrain может быть nullptr (плоская земля, y = 0).
RoadProfile computeRoadProfile(const OsmData& data, const GeoProjection& projection, const Heightmap* terrain, const RoadProfileSettings& settings);

}  // namespace han::world
