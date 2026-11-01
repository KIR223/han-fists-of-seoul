// han/ai/world_services.h — реализация контракта contracts::IWorldServices (поставщик A): время суток и календарь, погода, район, святые места,
// заявки на спавн, поиск пути, розыск. Игровой слой B работает только через интерфейс контракта, а не с этим классом напрямую.
// Розыск подключается через WantedBackend (его реализует game/police), поэтому han_ai не зависит от игры.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "contracts/world_services.h"
#include "han/ai/navgrid.h"

namespace han::ai {

class WantedBackend {
public:
    virtual ~WantedBackend() = default;
    virtual int wantedLevel() const = 0;
    // true — преступление засчитано.
    virtual bool crime(std::uint32_t type, Vec2 where) = 0;
};

struct SpawnRequest {
    enum class Kind : std::uint8_t { Npc, Vehicle } kind = Kind::Npc;
    contracts::EntityId id = 0;
    std::uint32_t model = 0;  // архетип NPC или модель машины
    contracts::Transform at;
};

struct District {
    contracts::DistrictId id = 0;
    std::vector<Vec2> polygon;  // контур в метрах карты (x, z)
};

struct SacredZone {
    Vec2 center;
    float radius = 20.f;        // храм, церковь, мечеть: круглая зона
};

class WorldServices final : public contracts::IWorldServices {
public:
    WorldServices() = default;

    // --- настройка (игра и загрузчик мира) ---
    void setNavGrid(const NavGrid* nav) { nav_ = nav; }
    void setWantedBackend(WantedBackend* backend) { wanted_ = backend; }
    void addDistrict(District d) { districts_.push_back(std::move(d)); }
    void addSacred(SacredZone z) { sacred_.push_back(z); }
    void setWeather(contracts::Weather w) { weather_ = w; }
    void setTime(float hour, std::uint16_t dayOfYear = 0);
    // Время идёт: gameSecondsPerRealSecond — во сколько раз игровые сутки быстрее (по умолчанию 24 минуты на сутки → 60).
    void advance(float realSeconds, float gameSecondsPerRealSecond = 60.f);
    // Заявки на появление, которые игровой слой забирает и исполняет (создаёт сущности).
    std::vector<SpawnRequest> takeSpawnRequests();

    // --- IWorldServices ---
    contracts::WorldTime time() const override { return time_; }
    contracts::Weather weather() const override { return weather_; }
    contracts::DistrictId districtAt(contracts::Vec3 position) const override;
    bool isSacred(contracts::Vec3 position) const override;
    contracts::EntityId spawnNpc(std::uint32_t archetype, contracts::Vec3 position) override;
    contracts::EntityId spawnVehicle(std::uint32_t model, contracts::Transform at) override;
    std::uint32_t findPath(contracts::Vec3 from, contracts::Vec3 to, contracts::Vec3* out, std::uint32_t maxPoints) const override;
    std::uint8_t wantedLevel() const override { return wanted_ ? static_cast<std::uint8_t>(wanted_->wantedLevel()) : 0; }
    void addCrime(std::uint32_t crimeType, contracts::Vec3 where, contracts::EntityId culprit) override;

    int crimesReported() const { return crimes_; }

private:
    const NavGrid* nav_ = nullptr;
    WantedBackend* wanted_ = nullptr;
    std::vector<District> districts_;
    std::vector<SacredZone> sacred_;
    std::vector<SpawnRequest> spawns_;
    contracts::WorldTime time_;
    contracts::Weather weather_ = contracts::Weather::Clear;
    contracts::EntityId nextEntity_ = 1000;  // 0 — «никто», малые номера оставлены игровому слою
    double clockSeconds_ = 12.0 * 3600.0;
    int crimes_ = 0;
};

}  // namespace han::ai
// Пример: contracts::IWorldServices& w = services; if (w.isSacred(pos)) return; w.addCrime(1, pos, player); auto stars = w.wantedLevel();
