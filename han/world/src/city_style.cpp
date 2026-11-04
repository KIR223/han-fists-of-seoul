#include "han/world/city_style.h"
#include "han/core/json.h"
#include <cmath>

namespace han::world {
bool loadCityBakeInfo(const std::string& file, CityBakeInfo& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    const auto format = root["format"].asString();
    const auto& origin = root["origin"];
    if (!root.isObject() || (format != "HCL1" && format != "HCL2" && format != "HCL3") || !origin.isArray() || origin.size() != 2 ||
        !origin.at(0).isNumber() || !origin.at(1).isNumber() || !root["cell_size"].isNumber() || !root["shop_tile"].isNumber()) {
        error = file + ": некорректный индекс ячеек города";
        return false;
    }
    CityBakeInfo info;
    info.originLat = origin.at(0).asNumber(); info.originLon = origin.at(1).asNumber();
    const double cell = root["cell_size"].asNumber(), tile = root["shop_tile"].asNumber();
    if (!std::isfinite(info.originLat) || !std::isfinite(info.originLon) || std::fabs(info.originLat) > 90 || std::fabs(info.originLon) > 180 ||
        !std::isfinite(cell) || cell <= 1 || cell > 10000 || !std::isfinite(tile) || tile <= 0 || tile > 10000) {
        error = file + ": некорректные координаты или размеры города";
        return false;
    }
    info.cellSize = static_cast<float>(cell); info.shopTile = static_cast<float>(tile);
    out = info;
    return true;
}

bool loadCityStyle(const std::string& file, CityStyle& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (!root.isObject() || root["version"].asNumber() != 1) {
        error = file + ": неизвестная версия оформления города";
        return false;
    }
    CityStyle style;
    style.originLat = root["origin_lat"].asNumber();
    style.originLon = root["origin_lon"].asNumber();
    if (!root["origin_lat"].isNumber() || !root["origin_lon"].isNumber() || !std::isfinite(style.originLat) || !std::isfinite(style.originLon) ||
        style.originLat < -90 || style.originLat > 90 || style.originLon < -180 || style.originLon > 180) {
        error = file + ": некорректное начало географических координат";
        return false;
    }
    struct Field { const char* name; float* value; };
    const Field fields[] = {
        {"window_roughness", &style.windowRoughness},
        {"tree_height", &style.treeHeight}, {"tree_height_variation", &style.treeHeightVariation}, {"tree_trunk_radius", &style.treeTrunkRadius},
        {"shop_bay_width", &style.shopBayWidth}, {"blade_width", &style.bladeWidth}, {"blade_height", &style.bladeHeight},
        {"blade_offset", &style.bladeOffset}, {"blade_base", &style.bladeBase}, {"floor_height", &style.floorHeight},
        {"ac_width", &style.acWidth}, {"ac_height", &style.acHeight}, {"ac_depth", &style.acDepth}, {"ac_spacing", &style.acSpacing},
        {"sidewalk_width", &style.sidewalkWidth}, {"curb_height", &style.curbHeight}, {"crosswalk_length", &style.crosswalkLength},
        {"crosswalk_spacing", &style.crosswalkSpacing}, {"crosswalk_bar", &style.crosswalkBar},
        {"street_light_radius", &style.streetLightRadius}, {"street_light_power", &style.streetLightPower},
        {"night_exposure", &style.nightExposure}, {"day_exposure", &style.dayExposure}, {"fog_density", &style.fogDensity},
        {"specular_antialiasing", &style.specularAntiAliasing}, {"fog_height_falloff", &style.fogHeightFalloff}, {"bloom_intensity", &style.bloomIntensity},
        {"crowd_radius", &style.crowdRadius}, {"crowd_nav_radius", &style.crowdNavRadius}, {"crowd_draw_radius", &style.crowdDrawRadius},
        {"person_height", &style.personHeight}, {"car_width", &style.carWidth}, {"car_height", &style.carHeight}, {"car_length", &style.carLength},
        {"window_emission", &style.windowEmission}, {"sign_emission", &style.signEmission}, {"canopy_depth", &style.canopyDepth}, {"canopy_height", &style.canopyHeight}, {"crowd_nav_cell", &style.crowdNavCell},
        {"furniture_spacing", &style.furnitureSpacing}, {"bench_length", &style.benchLength}, {"bench_height", &style.benchHeight}, {"planter_size", &style.planterSize},
        {"headlight_power", &style.headlightPower}, {"headlight_radius", &style.headlightRadius},
        {"roof_paint_r", &style.roofR}, {"roof_paint_g", &style.roofG}, {"roof_paint_b", &style.roofB},
        {"metro_width", &style.metroWidth}, {"metro_height", &style.metroHeight}, {"metro_length", &style.metroLength}
    };
    for (const auto& field : fields) {
        const auto& value = root[field.name];
        const double n = value.asNumber();
        if (!value.isNumber() || !std::isfinite(n) || n <= 0 || n > 10000) {
            error = file + ": некорректный параметр " + field.name;
            return false;
        }
        *field.value = static_cast<float>(n);
    }
    const auto& batch = root["shadow_batch_cell_size"];
    const double batchSize = batch.asNumber();
    if (!batch.isNumber() || !std::isfinite(batchSize) || batchSize < 0 || batchSize > 10000) {
        error = file + ": некорректный параметр shadow_batch_cell_size";
        return false;
    }
    style.shadowBatchCellSize = static_cast<float>(batchSize);
    struct IntField { const char* name; int* value; int maximum; };
    for (const auto& field : {IntField{"sign_bays", &style.signBays, 32}, {"max_blades", &style.maxBlades, 4}, {"crowd_count", &style.crowdCount, 1500}, {"facade_detail_repeat", &style.facadeDetailRepeat, 8},
                              {"crowd_path_queries", &style.crowdPathQueries, 8}, {"crowd_path_expansions", &style.crowdPathExpansions, 200000}}) {
        const auto& value = root[field.name];
        const double n = value.asNumber();
        if (!value.isNumber() || !std::isfinite(n) || n < 1 || n > field.maximum || std::floor(n) != n) {
            error = file + ": некорректный параметр " + field.name;
            return false;
        }
        *field.value = static_cast<int>(n);
    }
    if (style.crosswalkBar >= style.crosswalkSpacing || style.crowdRadius >= style.crowdNavRadius || style.crowdDrawRadius > style.crowdNavRadius ||
        style.roofR > 1 || style.roofG > 1 || style.roofB > 1 || style.windowRoughness > 1 ||
        style.specularAntiAliasing > 1 || style.bloomIntensity > 1 || style.fogHeightFalloff > 1) {
        error = file + ": несовместимые размеры улиц или радиусы толпы";
        return false;
    }
    style.enabled = true;
    out = style;
    return true;
}
}  // namespace han::world
