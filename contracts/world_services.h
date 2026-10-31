// contracts/world_services.h — поставщик: A, потребители: B (бой, миссии, телефон).
// Зачем: время, погода, район, святые места, спавн, навигация, розыск — единый вход в мир.
#pragma once
#include <cstdint>

#include "contracts/common.h"

namespace han::contracts {
enum class Weather : std::uint8_t { Clear, Cloudy, Rain, Fog, Snow, Storm };
enum class Season : std::uint8_t { Spring, Summer, Autumn, Winter };
using DistrictId = std::uint16_t;

struct WorldTime {
    float hour = 12.f;
    std::uint16_t dayOfYear = 0;
    std::uint8_t weekday = 0;
    Season season = Season::Spring;
};

struct IWorldServices {
    virtual WorldTime time() const = 0;
    virtual Weather weather() const = 0;
    virtual DistrictId districtAt(Vec3 position) const = 0;
    virtual bool isSacred(Vec3 position) const = 0;  // драка запрещена
    virtual EntityId spawnNpc(std::uint32_t archetype, Vec3 position) = 0;
    virtual EntityId spawnVehicle(std::uint32_t model, Transform at) = 0;
    // Путь по навмешу; возвращает число точек, записанных в out (0 — пути нет).
    virtual std::uint32_t findPath(Vec3 from, Vec3 to, Vec3* out, std::uint32_t maxPoints) const = 0;
    virtual std::uint8_t wantedLevel() const = 0;  // 0..5 звёзд
    virtual void addCrime(std::uint32_t crimeType, Vec3 where, EntityId culprit) = 0;
    virtual ~IWorldServices() = default;
};
}  // namespace han::contracts
// Пример: if (world.isSacred(pos)) return; world.addCrime(kAssault, pos, player);
