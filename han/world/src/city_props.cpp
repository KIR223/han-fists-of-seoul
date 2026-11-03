#include "han/world/city_props.h"
#include "han/core/json.h"
#include <cmath>

namespace han::world {
bool loadCityVehicleModels(const std::string& file, CityVehicleModels& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (root["version"].asNumber() != 1 || !root["meshes"].isArray() || root["meshes"].size() == 0 || root["meshes"].size() > 32) {
        error = file + ": неверный каталог моделей транспорта"; return false;
    }
    CityVehicleModels models;
    if (!root["warmup_seconds"].isNumber() || !std::isfinite(root["warmup_seconds"].asNumber()) ||
        root["warmup_seconds"].asNumber() < 0 || root["warmup_seconds"].asNumber() > 120) {
        error = file + ": неверное время подготовки трафика"; return false;
    }
    models.warmupSeconds = static_cast<float>(root["warmup_seconds"].asNumber());
    for (const auto& item : root["meshes"].items()) {
        const auto name = item.asString();
        if (name.size() < 7 || !name.ends_with(".hmesh") || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos) {
            error = file + ": неверное имя модели транспорта"; return false;
        }
        models.meshes.push_back(name);
    }
    for (const auto key : {"lod_near", "lod_mid", "lod_far", "shadow_radius"})
        if (!root[key].isNumber() || !std::isfinite(root[key].asNumber()) || root[key].asNumber() <= 0 || root[key].asNumber() > 1000) {
            error = file + ": неверные расстояния транспорта"; return false;
        }
    models.lodNear = static_cast<float>(root["lod_near"].asNumber()); models.lodMid = static_cast<float>(root["lod_mid"].asNumber());
    models.lodFar = static_cast<float>(root["lod_far"].asNumber()); models.shadowRadius = static_cast<float>(root["shadow_radius"].asNumber());
    if (!(models.lodNear < models.lodMid && models.lodMid < models.lodFar && models.shadowRadius <= models.lodFar)) {
        error = file + ": несовместимые расстояния транспорта"; return false;
    }
    out = std::move(models);
    return true;
}
bool loadCityVegetation(const std::string& file, CityVegetation& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    CityVegetation vegetation;
    vegetation.mesh = root["mesh"].asString();
    if (root["version"].asNumber() != 1 || !vegetation.mesh.ends_with(".hmesh") || vegetation.mesh.find_first_of("/\\:") != std::string::npos || vegetation.mesh.find("..") != std::string::npos) {
        error = file + ": некорректный меш растительности"; return false;
    }
    for (const char* key : {"draw_radius", "shadow_radius", "lod_near", "lod_far", "max_draws"})
        if (!root[key].isNumber() || !std::isfinite(root[key].asNumber()) || root[key].asNumber() <= 0 || root[key].asNumber() > 1000) {
            error = file + ": некорректный параметр растительности " + key; return false;
        }
    vegetation.drawRadius = static_cast<float>(root["draw_radius"].asNumber());
    vegetation.shadowRadius = static_cast<float>(root["shadow_radius"].asNumber());
    vegetation.lodNear = static_cast<float>(root["lod_near"].asNumber());
    vegetation.lodFar = static_cast<float>(root["lod_far"].asNumber());
    const double count = root["max_draws"].asNumber();
    if (vegetation.lodNear >= vegetation.lodFar || vegetation.lodFar >= vegetation.drawRadius || vegetation.shadowRadius > vegetation.drawRadius || std::floor(count) != count) {
        error = file + ": несовместимые расстояния LOD или бюджет деревьев"; return false;
    }
    vegetation.maxDraws = static_cast<int>(count);
    out = std::move(vegetation);
    return true;
}
bool loadCityProps(const std::string& file, CityPropCatalog& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    const double count = root["max_draws"].asNumber();
    if (root["version"].asNumber() != 1 || !root["props"].isArray() || root["props"].size() > 32 ||
        !root["max_draws"].isNumber() || !std::isfinite(count) || count < 1 || count > 256 || std::floor(count) != count) {
        error = file + ": некорректный каталог пропсов"; return false;
    }
    CityPropCatalog catalog;
    catalog.maxDraws = static_cast<int>(count);
    for (const auto& item : root["props"].items()) {
        CityPropSpec prop;
        prop.mesh = item["mesh"].asString();
        if (!prop.mesh.ends_with(".hmesh") || prop.mesh.find_first_of("/\\:") != std::string::npos || prop.mesh.find("..") != std::string::npos || !item["shop"].isBool()) {
            error = file + ": недопустимое имя меша или тип места"; return false;
        }
        for (const auto& key : {"scale", "draw_radius", "lod_distance", "anchor_stride", "offset_x", "offset_z"}) {
            const auto& v = item[key];
            if (!v.isNumber() || !std::isfinite(v.asNumber()) || std::fabs(v.asNumber()) > 1000) {
                error = file + ": некорректный параметр пропса " + key; return false;
            }
        }
        prop.scale = static_cast<float>(item["scale"].asNumber());
        prop.drawRadius = static_cast<float>(item["draw_radius"].asNumber());
        prop.lodDistance = static_cast<float>(item["lod_distance"].asNumber());
        prop.offsetX = static_cast<float>(item["offset_x"].asNumber());
        prop.offsetZ = static_cast<float>(item["offset_z"].asNumber());
        const double stride = item["anchor_stride"].asNumber();
        if (prop.scale <= 0 || prop.scale > 10 || prop.drawRadius <= 0 || prop.drawRadius > 300 || prop.lodDistance <= 0 || prop.lodDistance > prop.drawRadius || stride < 1 || std::floor(stride) != stride) {
            error = file + ": размеры или частота пропса вне диапазона"; return false;
        }
        prop.anchorStride = static_cast<int>(stride);
        prop.shop = item["shop"].asBool();
        catalog.props.push_back(std::move(prop));
    }
    out = std::move(catalog);
    return true;
}
}
