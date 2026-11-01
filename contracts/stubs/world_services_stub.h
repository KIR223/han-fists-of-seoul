// Заглушка world_services.h: полдень, ясно, район 0, не священно, розыска нет, путь — прямая из двух точек.
#pragma once
#include "contracts/world_services.h"

namespace han::contracts::stubs {
struct WorldServicesStub final : IWorldServices {
    WorldTime time() const override { return {}; }
    Weather weather() const override { return Weather::Clear; }
    DistrictId districtAt(Vec3) const override { return 0; }
    bool isSacred(Vec3) const override { return false; }
    EntityId spawnNpc(std::uint32_t, Vec3) override { return ++next_; }
    EntityId spawnVehicle(std::uint32_t, Transform) override { return ++next_; }
    std::uint32_t findPath(Vec3 from, Vec3 to, Vec3* out, std::uint32_t maxPoints) const override {
        if (maxPoints < 2) return 0;
        out[0] = from;
        out[1] = to;
        return 2;
    }
    std::uint8_t wantedLevel() const override { return 0; }
    void addCrime(std::uint32_t, Vec3, EntityId) override {}

private:
    EntityId next_ = 0;
};
}  // namespace han::contracts::stubs
