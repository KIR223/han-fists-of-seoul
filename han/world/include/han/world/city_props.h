#pragma once
#include <string>
#include <vector>

namespace han::world {
struct CityPropSpec {
    std::string mesh;
    float scale = 1, drawRadius = 0, lodDistance = 0, offsetX = 0, offsetZ = 0;
    int anchorStride = 1;
    bool shop = false;
};
struct CityPropCatalog {
    int maxDraws = 0;
    std::vector<CityPropSpec> props;
};
// Относительные имена hmesh в assets_bin/cooked. При ошибке прежний каталог сохраняется.
bool loadCityProps(const std::string& file, CityPropCatalog& out, std::string& error);
struct CityVegetation {
    std::string mesh;
    float drawRadius = 0, shadowRadius = 0, lodNear = 0, lodFar = 0;
    int maxDraws = 0;
};
bool loadCityVegetation(const std::string& file, CityVegetation& out, std::string& error);
struct CityVehicleModels {
    std::vector<std::string> meshes;
    float lodNear = 0, lodMid = 0, lodFar = 0, shadowRadius = 0, warmupSeconds = 0;
};
bool loadCityVehicleModels(const std::string& file, CityVehicleModels& out, std::string& error);
}
