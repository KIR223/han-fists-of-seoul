// contracts/combat_world.h (ICombatWorld) — интерфейс: B, реализация: A.
// Зачем: боевое ядро (game/combat) не зависит от рендера и Jolt; всё нужное от мира — здесь.
#pragma once
#include "contracts/common.h"

namespace han::contracts {
struct FighterCapsule {
    Vec3 base;  // нижняя точка оси
    float height = 1.8f, radius = 0.3f;
};

struct ICombatWorld {
    virtual Transform transformOf(EntityId fighter) const = 0;
    virtual FighterCapsule capsuleOf(EntityId fighter) const = 0;
    virtual bool lineOfSight(EntityId from, EntityId to) const = 0;
    virtual void applyImpulse(EntityId fighter, Vec3 impulse) = 0;  // отброс от удара
    virtual void setFacing(EntityId fighter, Vec3 direction) = 0;   // доворот к цели
    virtual bool isSacredAt(Vec3 position) const = 0;               // в святых местах драка невозможна
    virtual ~ICombatWorld() = default;
};
}  // namespace han::contracts
// Пример: if (!world.isSacredAt(pos)) startFight(a, b);
