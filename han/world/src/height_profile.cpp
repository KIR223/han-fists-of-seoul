#include "han/world/height_profile.h"

#include <cmath>

#include "han/core/json.h"

namespace han::world {
float HeightProfile::heightFor(std::string_view type, float area) const {
    if (!enabled) return 0.f;
    if (type == "yes") return area <= smallMaxArea ? yesSmall : (area <= midMaxArea ? yesMid : yesLarge);
    const auto it = byType.find(std::string(type));
    return it == byType.end() ? 0.f : it->second;
}

bool loadHeightProfile(const std::string& file, HeightProfile& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (!root.isObject() || root["version"].asNumber() != 1) {
        error = file + ": неизвестная версия профиля высот";
        return false;
    }
    const auto valid = [](double v) { return std::isfinite(v) && v >= 2.5 && v <= 300.0; };
    HeightProfile p;
    const auto& types = root["by_type"];
    if (!types.isObject()) {
        error = file + ": нужен раздел by_type";
        return false;
    }
    for (const auto& [key, v] : types.members()) {
        if (!v.isNumber() || !valid(v.asNumber())) {
            error = file + ": некорректная высота типа " + key;
            return false;
        }
        p.byType[key] = static_cast<float>(v.asNumber());
    }
    const auto& y = root["yes_by_area"];
    const double a = y["small_max_m2"].asNumber(), b = y["mid_max_m2"].asNumber();
    if (!y.isObject() || !y["small_max_m2"].isNumber() || !y["mid_max_m2"].isNumber() || !(a > 0 && b > a) || !valid(y["small"].asNumber()) ||
        !valid(y["mid"].asNumber()) || !valid(y["large"].asNumber())) {
        error = file + ": некорректный раздел yes_by_area";
        return false;
    }
    p.smallMaxArea = static_cast<float>(a);
    p.midMaxArea = static_cast<float>(b);
    p.yesSmall = static_cast<float>(y["small"].asNumber());
    p.yesMid = static_cast<float>(y["mid"].asNumber());
    p.yesLarge = static_cast<float>(y["large"].asNumber());
    p.enabled = true;
    out = std::move(p);
    return true;
}
}  // namespace han::world
