// han/ai/district_catalog.h — районы города (G6): контуры и настроение из data/world/districts/*.json (схема contracts/schemas/district.schema.json).
// Настроение (ambience) — цветокоррекция, неон, свет фонарей, плотность толпы и музыка района; рендер и звук плавно переходят между районами.
#pragma once
#include <string>
#include <vector>

#include "han/ai/navgrid.h"
#include "han/ai/world_services.h"

namespace han::ai {

struct Ambience {
    std::string lut = "neutral";
    float saturation = 1.0f, contrast = 1.2f, neon = 1.0f, crowdDensity = 1.0f;
    float lampColor[3] = {1.0f, 0.8f, 0.55f};
    std::string musicKey;
};

struct DistrictInfo {
    std::string id, nameKey, kind;
    contracts::DistrictId numericId = 0;
    std::vector<Vec2> polygon;
    bool sacredAllowed = false;
    int danger = 0;
    std::vector<std::string> tags;
    Ambience ambience;
};

class DistrictCatalog {
public:
    // Читает все *.json из папки. Ошибки отдельных файлов копятся в error, остальные грузятся; false — папки нет или есть ошибки.
    bool loadDirectory(const std::string& dir, std::string& error);
    const std::vector<DistrictInfo>& all() const { return districts_; }
    // Район, внутри чьего контура лежит точка; nullptr — вне районов.
    const DistrictInfo* infoAt(Vec2 p) const;
    const DistrictInfo* byId(const std::string& id) const;
    // Передаёт контуры в мировые сервисы (districtAt).
    void registerIn(WorldServices& services) const;
    // Настроение в точке (запас — нейтральное).
    Ambience ambienceAt(Vec2 p) const;

private:
    std::vector<DistrictInfo> districts_;
};

// Плавный переход настроения: current приближается к target с постоянной времени tau секунд (цвет lamp — покомпонентно, строки — по цели).
void blendAmbience(Ambience& current, const Ambience& target, float dt, float tau = 1.5f);

}  // namespace han::ai
// Пример: DistrictCatalog c; c.loadDirectory("data/world/districts", err); c.registerIn(services); blendAmbience(mood, c.ambienceAt(player), dt);
