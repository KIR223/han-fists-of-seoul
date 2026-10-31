// contracts/character_controller.h — поставщик: A, потребители: B (игрок и бойцы).
// Зачем: капсула персонажа со ступеньками и склонами; бой задаёт желаемую скорость, а не работает с Jolt.
#pragma once
#include "contracts/common.h"

namespace han::contracts {
struct CharacterDesc {
    float radius = 0.3f, height = 1.8f;
    float maxSlopeDegrees = 45.f, stepHeight = 0.35f, mass = 75.f;
};

struct CharacterState {
    Vec3 position, velocity;
    bool grounded = false;
};

struct ICharacterController {
    virtual EntityId create(const CharacterDesc& desc, Vec3 position) = 0;
    virtual void destroy(EntityId id) = 0;
    // Желаемая скорость на следующий шаг физики (м/с); ступеньки и склоны контроллер учитывает сам.
    virtual void setDesiredVelocity(EntityId id, Vec3 velocity) = 0;
    virtual void applyImpulse(EntityId id, Vec3 impulse) = 0;  // толчок или оттеснение от удара
    virtual CharacterState state(EntityId id) const = 0;
    virtual ~ICharacterController() = default;
};
}  // namespace han::contracts
// Пример: cc.setDesiredVelocity(player, {0, 0, 3.5f}); auto s = cc.state(player);
