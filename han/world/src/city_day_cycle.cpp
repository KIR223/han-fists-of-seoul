#include "han/world/city_day_cycle.h"
#include "han/core/json.h"
#include <algorithm>
#include <cmath>

namespace han::world {
bool CityDayCycle::load(const std::string& file, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    const double start = root["start_hour"].asNumber(-1), seconds = root["seconds_per_day"].asNumber();
    if (!root.isObject() || root["version"].asNumber() != 1 || !root["keys"].isArray() || root["keys"].size() < 2 ||
        !std::isfinite(start) || start < 0 || start >= 24 || !std::isfinite(seconds) || seconds <= 0 || seconds > 864000) {
        error = file + ": некорректный суточный цикл";
        return false;
    }
    std::vector<CityDayKey> keys;
    for (const auto& item : root["keys"].items()) {
        for (const char* field : {"hour", "elevation", "azimuth", "kelvin", "exposure", "lit_windows"})
            if (!item[field].isNumber() || !std::isfinite(item[field].asNumber())) {
                error = file + ": отсутствует числовое поле суточного цикла";
                return false;
            }
        CityDayKey key{static_cast<float>(item["hour"].asNumber()), static_cast<float>(item["elevation"].asNumber()),
                      static_cast<float>(item["azimuth"].asNumber()), static_cast<float>(item["kelvin"].asNumber()),
                      static_cast<float>(item["exposure"].asNumber()), static_cast<float>(item["lit_windows"].asNumber())};
        if (key.hour < 0 || key.hour >= 24 || key.elevation < -90 || key.elevation > 90 || key.azimuth < 0 || key.azimuth >= 360 ||
            key.kelvin < 1000 || key.kelvin > 40000 || key.exposure <= 0 || key.exposure > 10 || key.litWindows < 0 || key.litWindows > 1) {
            error = file + ": параметры суточного цикла вне диапазона";
            return false;
        }
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.hour < b.hour; });
    for (std::size_t i = 1; i < keys.size(); ++i) if (keys[i].hour == keys[i - 1].hour) {
        error = file + ": повтор времени в суточном цикле";
        return false;
    }
    keys_ = std::move(keys); startHour_ = static_cast<float>(start); secondsPerDay_ = static_cast<float>(seconds);
    return true;
}

CityDayKey CityDayCycle::sample(float hour) const {
    if (keys_.empty() || !std::isfinite(hour)) return {};
    hour = std::fmod(hour, 24.f); if (hour < 0) hour += 24.f;
    const auto next = std::upper_bound(keys_.begin(), keys_.end(), hour, [](float h, const CityDayKey& key) { return h < key.hour; });
    const auto& b = next == keys_.end() ? keys_.front() : *next;
    const auto& a = next == keys_.begin() ? keys_.back() : *(next - 1);
    float begin = a.hour, end = b.hour, t = hour;
    if (end <= begin) end += 24.f;
    if (t < begin) t += 24.f;
    const float alpha = (t - begin) / (end - begin);
    const auto mix = [&](float x, float y) { return x + (y - x) * alpha; };
    float delta = b.azimuth - a.azimuth;
    if (delta > 180.f) delta -= 360.f;
    if (delta < -180.f) delta += 360.f;
    const float azimuth = std::fmod(a.azimuth + delta * alpha + 360.f, 360.f);
    return {hour, mix(a.elevation, b.elevation), azimuth, mix(a.kelvin, b.kelvin), mix(a.exposure, b.exposure), mix(a.litWindows, b.litWindows)};
}
}  // namespace han::world
