#include "han/ai/life_profile.h"

#include <algorithm>
#include <cmath>

#include "han/core/json.h"

namespace han::ai {

float LifeProfile::trafficDensityAt(float hour) const {
    if (trafficHours.empty()) return 1.f;
    float h = std::fmod(hour, 24.f);
    if (h < 0.f) h += 24.f;
    if (h <= trafficHours.front().first) return trafficHours.front().second;
    for (std::size_t i = 0; i + 1 < trafficHours.size(); ++i) {
        const auto& a = trafficHours[i];
        const auto& b = trafficHours[i + 1];
        if (h >= a.first && h <= b.first) {
            const float span = b.first - a.first;
            return span > 1e-6f ? a.second + (b.second - a.second) * ((h - a.first) / span) : b.second;
        }
    }
    return trafficHours.back().second;
}

namespace {
bool number(const core::Json& j, const char* key, double lo, double hi, double& out, std::string& error, const std::string& file) {
    const auto& v = j[key];
    const double n = v.asNumber();
    if (!v.isNumber() || !std::isfinite(n) || n < lo || n > hi) {
        error = file + ": некорректный параметр " + key;
        return false;
    }
    out = n;
    return true;
}
}  // namespace

bool loadLifeProfile(const std::string& file, LifeProfile& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (!root.isObject() || root["version"].asNumber() != 1) {
        error = file + ": неизвестная версия профиля жизни";
        return false;
    }
    LifeProfile p;
    const auto& t = root["traffic"];
    const auto& c = root["crowd"];
    if (!t.isObject() || !c.isObject()) {
        error = file + ": нужны разделы traffic и crowd";
        return false;
    }
    double v = 0;
    if (!number(t, "max_cars", 0, 1000, v, error, file)) return false;
    p.traffic.maxCars = static_cast<int>(v);
    if (!number(t, "spawn_radius", 50, 5000, v, error, file)) return false;
    p.traffic.spawnRadius = static_cast<float>(v);
    if (!number(t, "despawn_radius", 50, 5000, v, error, file)) return false;
    p.traffic.despawnRadius = static_cast<float>(v);
    if (!number(t, "despawn_in_view_radius", 0, 8000, v, error, file)) return false;
    p.traffic.despawnInViewRadius = static_cast<float>(v);
    if (!number(t, "spawn_min_distance_in_view", 0, 2000, v, error, file)) return false;
    p.traffic.spawnMinDistanceInView = static_cast<float>(v);
    if (!number(t, "spawn_min_distance_behind", 0, 500, v, error, file)) return false;
    p.traffic.spawnMinDistanceBehind = static_cast<float>(v);
    if (!number(t, "view_half_angle_degrees", 10, 180, v, error, file)) return false;
    p.traffic.viewHalfAngleDegrees = static_cast<float>(v);
    if (!number(t, "spawn_gap", 5, 200, v, error, file)) return false;
    p.traffic.spawnGap = static_cast<float>(v);
    if (p.traffic.despawnRadius < p.traffic.spawnRadius || (p.traffic.despawnInViewRadius > 0.f && p.traffic.despawnInViewRadius < p.traffic.despawnRadius) ||
        p.traffic.spawnMinDistanceInView > p.traffic.spawnRadius) {
        error = file + ": радиусы должны идти по порядку: появление ≤ удаление ≤ удаление видимых; в поле зрения появление не дальше радиуса появления";
        return false;
    }
    p.traffic.weightByLength = true;
    const auto& w = t["class_weight"];
    const char* names[4] = {"motorway", "primary", "secondary", "local"};
    for (int i = 0; i < 4; ++i) {
        if (!number(w, names[i], 0, 100, v, error, file)) return false;
        p.traffic.classWeight[i] = static_cast<float>(v);
    }
    const auto& hours = t["hour_density"];
    if (!hours.isArray() || hours.size() < 2) {
        error = file + ": hour_density — массив пар [час, доля], не меньше двух";
        return false;
    }
    float previous = -1.f;
    for (std::size_t i = 0; i < hours.size(); ++i) {
        const auto& pair = hours.at(i);
        const double h = pair.at(0).asNumber(), d = pair.at(1).asNumber();
        if (!pair.isArray() || pair.size() != 2 || !std::isfinite(h) || !std::isfinite(d) || h < 0 || h > 24 || d < 0 || d > 4 || static_cast<float>(h) <= previous) {
            error = file + ": hour_density — пары [час 0..24 по возрастанию, доля 0..4]";
            return false;
        }
        previous = static_cast<float>(h);
        p.trafficHours.emplace_back(static_cast<float>(h), static_cast<float>(d));
    }
    if (!number(c, "agent_count", 0, 5000, v, error, file)) return false;
    p.crowd.agentCount = static_cast<std::size_t>(v);
    if (!number(c, "nav_radius", 100, 2000, v, error, file)) return false;
    p.crowd.navRadius = static_cast<float>(v);
    if (!number(c, "recenter_fraction", 0.1, 0.9, v, error, file)) return false;
    p.crowd.recenterFraction = static_cast<float>(v);
    if (!number(c, "spawn_radius", 20, 2000, v, error, file)) return false;
    p.crowd.spawnRadius = static_cast<float>(v);
    if (!number(c, "min_recycle_distance", 0, 2000, v, error, file)) return false;
    p.crowd.minRecycleDistance = static_cast<float>(v);
    if (!number(c, "far_visible", 50, 3000, v, error, file)) return false;
    p.crowd.farVisible = static_cast<float>(v);
    if (!number(c, "view_half_angle_degrees", 10, 180, v, error, file)) return false;
    p.crowd.viewHalfAngleDegrees = static_cast<float>(v);
    if (!number(c, "treadmill_interval", 0.1, 60, v, error, file)) return false;
    p.crowd.treadmillInterval = static_cast<float>(v);
    if (!number(c, "treadmill_batch", 0, 500, v, error, file)) return false;
    p.crowd.treadmillBatch = static_cast<std::size_t>(v);
    if (!number(c, "treadmill_min_distance", 0, 1, v, error, file)) return false;
    p.crowd.treadmillMinDistance = static_cast<float>(v);
    if (p.crowd.spawnRadius > p.crowd.navRadius) {
        error = file + ": crowd.spawn_radius не может быть больше nav_radius";
        return false;
    }
    out = std::move(p);
    return true;
}

}  // namespace han::ai
