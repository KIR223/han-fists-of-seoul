#include "han/world/district_scene.h"
#include "han/core/json.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

namespace han::world {
namespace {
bool finite(const core::Json& value) { return value.isNumber() && std::isfinite(value.asNumber()); }
bool meshPath(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find_first_of("\\:") != std::string::npos || !name.ends_with(".hmesh")) return false;
    for (const auto& part : std::filesystem::path(name)) if (part == ".." || part == ".") return false;
    return true;
}
bool vector(const core::Json& value, Vec3& out) {
    if (value.size() != 3 || !finite(value.at(0)) || !finite(value.at(1)) || !finite(value.at(2))) return false;
    out = {static_cast<float>(value.at(0).asNumber()), static_cast<float>(value.at(1).asNumber()), static_cast<float>(value.at(2).asNumber())};
    return std::abs(out.x) <= 100000 && std::abs(out.y) <= 10000 && std::abs(out.z) <= 100000;
}
}
bool loadDistrictScenes(const std::string& file, std::vector<DistrictScene>& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (root["version"].asNumber() != 1 || !root["scenes"].isArray() || root["scenes"].size() > 128) {
        error = file + ": неверный каталог статических районов"; return false;
    }
    std::vector<DistrictScene> scenes;
    std::set<std::string> ids;
    for (const auto& item : root["scenes"].items()) {
        DistrictScene scene;
        scene.id = item["id"].asString(); scene.mesh = item["mesh"].asString(); scene.interiors = item["interiors"].asString();
        scene.collision = item["collision"].asString();
        if (scene.id.empty() || !ids.insert(scene.id).second || !meshPath(scene.mesh) || !meshPath(scene.interiors) || !meshPath(scene.collision) || !vector(item["origin"], scene.origin)) {
            error = file + ": неверный идентификатор, меш или положение района"; return false;
        }
        if (!item["shadow"].isNull()) {
            scene.shadow = item["shadow"].asString();
            if (!item["shadow"].isString() || !meshPath(scene.shadow)) {
                error = file + ": неверный путь теневого меша района"; return false;
            }
        }
        for (const auto key : {"terrain_blend", "load_distance", "unload_distance", "interior_distance", "interior_light_power", "interior_light_radius"})
            if (!finite(item[key]) || item[key].asNumber() <= 0 || item[key].asNumber() > 10000) {
                error = file + ": неверные расстояния района"; return false;
            }
        if (item["half_size"].size() != 2 || !finite(item["half_size"].at(0)) || !finite(item["half_size"].at(1))) {
            error = file + ": неверные размеры района"; return false;
        }
        scene.halfWidth = static_cast<float>(item["half_size"].at(0).asNumber());
        scene.halfDepth = static_cast<float>(item["half_size"].at(1).asNumber());
        scene.terrainBlend = static_cast<float>(item["terrain_blend"].asNumber());
        scene.loadDistance = static_cast<float>(item["load_distance"].asNumber());
        scene.unloadDistance = static_cast<float>(item["unload_distance"].asNumber());
        scene.interiorDistance = static_cast<float>(item["interior_distance"].asNumber());
        scene.interiorLightPower = static_cast<float>(item["interior_light_power"].asNumber());
        scene.interiorLightRadius = static_cast<float>(item["interior_light_radius"].asNumber());
        if (scene.halfWidth <= 0 || scene.halfDepth <= 0 || scene.halfWidth > 5000 || scene.halfDepth > 5000 ||
            scene.loadDistance >= scene.unloadDistance || scene.interiorDistance >= scene.loadDistance) {
            error = file + ": несовместимые размеры и расстояния района"; return false;
        }
        const auto lights = item["lights"].asString();
        if (lights.rfind("data/city/", 0) != 0 || lights.find("..") != std::string::npos || lights.find_first_of("\\:") != std::string::npos) {
            error = file + ": неверный путь освещения района"; return false;
        }
        core::Json lighting;
        if (!core::loadJsonFile(lights, lighting, error)) return false;
        if (lighting["version"].asNumber() != 1 || !lighting["lights"].isArray() || lighting["lights"].size() > 10000) {
            error = lights + ": неверный список фонарей"; return false;
        }
        for (const auto& itemLight : lighting["lights"].items()) {
            Vec3 lamp;
            if (!vector(itemLight, lamp)) { error = lights + ": неверная позиция фонаря"; return false; }
            scene.lamps.push_back(lamp);
        }
        if (!lighting["interior_lights"].isArray() || lighting["interior_lights"].size() > 1000) {
            error = lights + ": неверный список внутренних светильников"; return false;
        }
        for (const auto& itemLight : lighting["interior_lights"].items()) {
            Vec3 lamp;
            if (!vector(itemLight, lamp)) { error = lights + ": неверная позиция внутреннего светильника"; return false; }
            scene.interiorLamps.push_back(lamp);
        }
        if (!lighting["footprints"].isArray() || lighting["footprints"].size() > 10000) {
            error = lights + ": неверный список контуров зданий"; return false;
        }
        for (const auto& rect : lighting["footprints"].items()) {
            if (rect.size() != 4 || !std::all_of(rect.items().begin(), rect.items().end(), finite) ||
                rect.at(0).asNumber() >= rect.at(2).asNumber() || rect.at(1).asNumber() >= rect.at(3).asNumber()) {
                error = lights + ": неверный контур здания"; return false;
            }
            const auto x0 = static_cast<float>(rect.at(0).asNumber()), z0 = static_cast<float>(rect.at(1).asNumber());
            const auto x1 = static_cast<float>(rect.at(2).asNumber()), z1 = static_cast<float>(rect.at(3).asNumber());
            scene.footprints.push_back({{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}});
        }
        scenes.push_back(std::move(scene));
    }
    out = std::move(scenes);
    return true;
}
float districtDistance(const DistrictScene& scene, float x, float z) {
    const float dx = std::max(0.f, std::abs(x - scene.origin.x) - scene.halfWidth);
    const float dz = std::max(0.f, std::abs(z - scene.origin.z) - scene.halfDepth);
    return std::hypot(dx, dz);
}
}
