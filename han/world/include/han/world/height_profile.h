// han/world/height_profile.h — высота зданий без тегов height/building:levels из данных (data/city/height_profile.json, этап 08).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

namespace han::world {
struct HeightProfile {
    bool enabled = false;
    std::unordered_map<std::string, float> byType;  // building=<тип> → высота, м
    float smallMaxArea = 150.f, midMaxArea = 600.f;  // границы площади для building=yes, м²
    float yesSmall = 0.f, yesMid = 0.f, yesLarge = 0.f;

    // Высота по типу и площади основания; 0 — профиль ничего не знает (применяется высота по умолчанию).
    float heightFor(std::string_view buildingType, float footprintArea) const;
};

bool loadHeightProfile(const std::string& file, HeightProfile& out, std::string& error);
}  // namespace han::world
