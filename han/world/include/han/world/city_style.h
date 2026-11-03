#pragma once
#include <string>

namespace han::world {
// Параметры художественной доводки: значения загружаются из data/city/streetscape.json.
struct CityStyle {
    float treeHeight = 0, treeHeightVariation = 0, treeTrunkRadius = 0;
    bool enabled = false;
    double originLat = 0, originLon = 0;
    float shopBayWidth = 0, bladeWidth = 0, bladeHeight = 0, bladeOffset = 0, bladeBase = 0;
    float floorHeight = 0, acWidth = 0, acHeight = 0, acDepth = 0, acSpacing = 0;
    float sidewalkWidth = 0, curbHeight = 0, crosswalkLength = 0, crosswalkSpacing = 0, crosswalkBar = 0;
    float streetLightRadius = 0, streetLightPower = 0, nightExposure = 0, dayExposure = 0, fogDensity = 0;
    float crowdRadius = 0, crowdNavRadius = 0, crowdDrawRadius = 0;
    float personHeight = 0, carWidth = 0, carHeight = 0, carLength = 0;
    float windowEmission = 0, signEmission = 0, canopyDepth = 0, canopyHeight = 0;
    float crowdNavCell = 0;
    float furnitureSpacing = 0, benchLength = 0, benchHeight = 0, planterSize = 0;
    float headlightPower = 0, headlightRadius = 0;
    float roofR = 0, roofG = 0, roofB = 0, metroWidth = 0, metroHeight = 0, metroLength = 0;
    int signBays = 0, maxBlades = 0, crowdCount = 0, facadeDetailRepeat = 0;
    float windowRoughness = 0;
    int crowdPathQueries = 0, crowdPathExpansions = 0;
};
bool loadCityStyle(const std::string& file, CityStyle& out, std::string& error);
struct CityBakeInfo {
    double originLat = 0, originLon = 0;
    float cellSize = 0, shopTile = 0;
};
bool loadCityBakeInfo(const std::string& file, CityBakeInfo& out, std::string& error);
}  // namespace han::world
