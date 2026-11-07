// han/ai/city_crowd.h — толпа города, которая следует за камерой (этап 06 плана города): навигационная сетка вокруг камеры, двери зданий как цели,
// пересборка при смещении камеры без пересоздания прохожих. Раньше эта логика жила лямбдой в city_demo.cpp и при каждом смещении на половину радиуса
// сетки уничтожала всю толпу и создавала заново у дверей рядом с камерой — все прохожие пропадали и появлялись на глазах.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "han/ai/archetype.h"
#include "han/ai/crowd.h"
#include "han/ai/navgrid.h"
#include "han/world/geometry.h"
#include "han/world/walk_layout.h"

namespace han::ai {

struct CityCrowdSettings {
    float navRadius = 600.f;          // половина стороны навигационной сетки вокруг камеры
    float spawnRadius = 180.f;        // при первом создании прохожие появляются у дверей в этом радиусе
    float recenterFraction = 0.5f;    // сетка пересобирается, когда камера ушла от её центра дальше этой доли navRadius
    float navCell = 1.f;
    std::size_t pathExpansions = 600000;
    int pathQueriesPerFrame = 4;
    std::size_t agentCount = 280;
    std::uint32_t seed = 12345;
    // Поведение при пересборке: true — прохожие остаются, переселяются только вышедшие за сетку (вне взгляда); false — прежнее: толпа создаётся
    // заново у дверей вокруг камеры (для сравнения в аудите).
    bool keepAgents = true;
    float minRecycleDistance = 140.f;  // переселяемый прохожий появляется не ближе этого к камере
    float viewHalfAngleDegrees = 62.f;
    float farVisible = 450.f;
    // «Конвейер»: пока камера движется, далёкие невидимые прохожие переселяются вперёд по ходу вне взгляда (плотность рядом с камерой не падает).
    bool treadmill = true;
    float treadmillInterval = 1.f;      // секунд между проходами
    std::size_t treadmillBatch = 6;     // не больше стольких за проход
    float treadmillMinDistance = 0.5f;  // доля navRadius: ближе агенты не переселяются
};

struct CityCrowdStats {
    int rebuilds = 0;
    std::size_t recycled = 0;          // переселено за всё время (keepAgents)
    std::size_t lastRecycled = 0;
    std::size_t lastPois = 0;
    double lastRebuildMs = 0.0;
};

class CityCrowd {
public:
    // Все ссылки должны пережить объект: здания, раскладка (вода, проезжая часть, переходы), архетипы.
    CityCrowd(const std::vector<std::vector<world::Vec2>>& buildings, const world::WalkLayout& layout, std::vector<const Archetype*> archetypes,
              const CityCrowdSettings& settings = {});

    // Раз в кадр. direction — единичный вектор взгляда камеры (нулевой — неизвестен); hour — час суток для расписаний.
    void update(float dt, Vec2 camera, Vec2 direction, float hour);

    const Crowd* crowd() const { return crowd_.get(); }
    const NavGrid& grid() const { return grid_; }
    Vec2 center() const { return center_; }
    const CityCrowdStats& stats() const { return stats_; }
    const CityCrowdSettings& settings() const { return settings_; }

private:
    void rebuild(Vec2 camera, Vec2 direction);

    const std::vector<std::vector<world::Vec2>>& buildings_;
    const world::WalkLayout& layout_;
    std::vector<const Archetype*> archetypes_;
    CityCrowdSettings settings_;
    NavGrid grid_;
    std::unique_ptr<Crowd> crowd_;
    Vec2 center_;
    bool built_ = false;
    float treadmillTimer_ = 0.f;
    CityCrowdStats stats_;
};

}  // namespace han::ai
