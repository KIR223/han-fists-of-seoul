// Заглушка physics_query.h: лучи ничего не находят, оверлапы пусты.
#pragma once
#include "contracts/physics_query.h"

namespace han::contracts::stubs {
struct PhysicsQueryStub final : IPhysicsQuery {
    RayHit raycast(Vec3, Vec3, std::uint32_t) const override { return {}; }
    RayHit sweepCapsule(Vec3, Vec3, float, float, std::uint32_t) const override { return {}; }
    std::uint32_t overlapSphere(Vec3, float, EntityId*, std::uint32_t, std::uint32_t) const override { return 0; }
};
}  // namespace han::contracts::stubs
