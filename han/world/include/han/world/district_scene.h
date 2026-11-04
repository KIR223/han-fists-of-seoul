#pragma once
#include <string>
#include <vector>
#include "han/world/city_builder.h"

namespace han::world {
struct DistrictScene {
    std::string id, mesh, interiors, collision, shadow;
    Vec3 origin;
    float halfWidth = 0, halfDepth = 0, terrainBlend = 0;
    float loadDistance = 0, unloadDistance = 0, interiorDistance = 0;
    std::vector<Vec3> lamps;  // локальные метры района
    std::vector<Vec3> interiorLamps;
    std::vector<std::vector<Vec2>> footprints;
    float interiorLightPower = 0, interiorLightRadius = 0;
};
// Внутренние данные статических районов; меши относительно assets_bin/cooked.
// При ошибке каталог не меняется, абсолютные пути и выход за каталог запрещены.
bool loadDistrictScenes(const std::string& file, std::vector<DistrictScene>& out, std::string& error);
float districtDistance(const DistrictScene& scene, float x, float z);
}
