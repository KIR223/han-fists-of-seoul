// Заглушка combat_world.h: все бойцы в начале координат, святых мест нет, видимость всегда есть.
#pragma once
#include "contracts/combat_world.h"

namespace han::contracts::stubs {
struct CombatWorldStub final : ICombatWorld {
    Transform transformOf(EntityId) const override { return {}; }
    FighterCapsule capsuleOf(EntityId) const override { return {}; }
    bool lineOfSight(EntityId, EntityId) const override { return true; }
    void applyImpulse(EntityId, Vec3) override {}
    void setFacing(EntityId, Vec3) override {}
    bool isSacredAt(Vec3) const override { return false; }
};
}  // namespace han::contracts::stubs
